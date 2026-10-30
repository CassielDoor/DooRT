#include <cstddef>
#include <stdint.h>
#include <new>
#include <string.h>

#include "doort_fault.hpp"
#include "doort_memory_manage.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_async_connect.hpp"

#include "doort_config.h"

extern dPort dp;
extern volatile dgState gState;
extern dkScheduler kScheduler;

void dTaskControlBlock::dTCBSetStackSize(uint32_t stackSize) {
    dtStackSize = stackSize;
}

void dTaskControlBlock::dTCBSetStackPtr(void* addr) {
    dtStackBase = addr;
}

void dTaskControlBlock::dTCBSetStackPointer(void *sp) {
    dtStackPointer = sp;
}

void* dTaskControlBlock::dTCBGetStackPointer(void) {
    return dtStackPointer;
}

void* dTaskControlBlock::dTCBGetStackBase(void) {
    return dtStackBase;
}

uint32_t dTaskControlBlock::dTCBGetStackSize(void) {
    return dtStackSize;
}

//任务入口包装: 启动时先排空该任务的延迟信号回调, 再进入用户函数
static void daTaskEntryWrapper(void* arg) {
    dTaskControlBlock* t = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();
    daConnectSignalDrainTask((dTaskRef)t);
    t->dtUserFunction(arg);
}

dTaskControlBlock* dNewTask(dTaskParams *taskParams) {
    if (taskParams == nullptr) return nullptr;
    if (taskParams->dtStackSize < DOORT_SUGGESTION_MIN_STACK)
        dLog("[warning]: Task stack small, stack size is: %d.\n", taskParams->dtStackSize);
    if (taskParams->dtStackSize < PHYSICAL_MIN_STACK)
        dfInitializationFault_TaskStackTooSmall();

    // 分配 TCB 对象, 有参构造
    dTaskControlBlock* newTask = dmKernelPlacementNew<dTaskControlBlock>(8,taskParams->dtFunc,taskParams->dtPriority);

    // 分配任务栈, 直接分配内存
    uint32_t stackSize = dmCalcAlignedTotalSize(taskParams->dtStackSize, 8, 1);
    void* stackMem = dmKernelPlacementNew(8, stackSize);

    newTask->dTCBSetStackSize(stackSize);
    newTask->dTCBSetStackPtr(stackMem);

    uint32_t *stackTop = (uint32_t *)((uint8_t *)(newTask->dTCBGetStackBase()) + newTask->dTCBGetStackSize());
    // 任务入口包一层: 启动时先排空延迟信号回调, 再跑用户函数
    uint32_t *initialSP = dp.dpInitTaskStack(stackTop, daTaskEntryWrapper, taskParams->dtParameters);
    newTask->dTCBSetStackPointer(initialSP);

    newTask->dtUniqueTaskID = gState.dgsTaskNum;
    gState.dgsTaskNum++;
    newTask->dtStatusCode = DOORT_TASK_STATUS_READY;

    return newTask;
}


/****************************** 任务注册相关 ******************************/

//统计优先级信息
static int collectPriorityInfo(dTaskControlBlock *taskHead, PriorityRegistry *allTaskPriority) {
    int priorityNum = 0;
    dTaskControlBlock *temp = taskHead;

    while(temp != nullptr) {
        int priority;
        if(temp->dtPriority <= -SPECIAL_PRIORITY_OFFSET) dfInitializationFault_UnsupportedPriority();
        //任务优先级钳位在-SPECIAL_PRIORITY_OFFSET到128之内
        priority = temp->dtPriority + SPECIAL_PRIORITY_OFFSET;      //硬件保留区
        if(!allTaskPriority[priority].isEnable) {
            allTaskPriority[priority].isEnable = true;
            priorityNum++;
        }
        allTaskPriority[priority].taskNum++;
        temp = temp->nextTCB;
    }
    //已拿完所有优先级数量
    gState.dgsPrioritiesNum = ++priorityNum;
    return priorityNum;
}

//分配队列头内存
static void allocQueueHeads(uint8_t priorityNum) {
    gState.dgsTaskQueneHead = dmKernelPlacementNewArray<dTaskQueueHead>(4, priorityNum);
}

//将任务挂载到对应优先级队列
static void distributeTasksToQueues(dTaskControlBlock *taskHead, const PriorityRegistry *allTaskPriority) {
    uint8_t nowPrioritiesPosition = 0;
    dTaskControlBlock *temp = nullptr;
    dTaskControlBlock *tempLinkedListTail = nullptr;

    //挂载
    for(int i = 1; i<PRIORITIES_TOTAL_NUMBER; i++) {
        if(allTaskPriority[i].isEnable) {
            temp = taskHead;
            tempLinkedListTail = nullptr;
            nowPrioritiesPosition++;
            gState.dgsTaskQueneHead[nowPrioritiesPosition].queuePriority = i - SPECIAL_PRIORITY_OFFSET;
            gState.dgsTaskQueneHead[nowPrioritiesPosition].taskNum = allTaskPriority[i].taskNum;    //补上任务计数, 否则永远为0导致调度器误入多任务分支
            while(temp != nullptr) {
                dTaskControlBlock *tempNode;
                tempNode = temp->nextTCB;       //提前保存下一个节点，避免摘除后丢失
                if(temp->dtPriority == i - SPECIAL_PRIORITY_OFFSET) {
                    //从原链表摘除对应节点
                    if(temp == taskHead) {
                        // 摘除的是头节点：更新头指针
                        taskHead = tempNode;
                        if(tempNode != nullptr) tempNode->prevTCB = nullptr;
                    }
                    else {
                        // 摘除中间或尾节点
                        if(temp->prevTCB != nullptr) temp->prevTCB->nextTCB = tempNode;
                        if(tempNode != nullptr) tempNode->prevTCB = temp->prevTCB;
                    }
                    //挂入当前优先级队列
                    temp->nextTCB = nullptr;
                    temp->prevTCB = tempLinkedListTail;
                    if(tempLinkedListTail == nullptr) gState.dgsTaskQueneHead[nowPrioritiesPosition].body = temp;
                    else tempLinkedListTail->nextTCB = temp;
                    tempLinkedListTail = temp;      //更新队列尾指针
                }
                temp = tempNode;
            }
        }
    }
}

static void readyTaskCount() {
    for(int i = 0; i < gState.dgsPrioritiesNum; i++) {
        dTaskControlBlock *temp = gState.dgsTaskQueneHead[i].body;
        gState.dgsTaskQueneHead[i].readyTask = 0;
        while(temp != nullptr) {
            if(temp->dtStatusCode == DOORT_TASK_STATUS_READY)
                gState.dgsTaskQueneHead[i].readyTask++;
            temp = temp->nextTCB;
        }
    }
}

void dkBitMapSync() {
    for(int i = 0; i < gState.dgsPrioritiesNum; i++) {
        kScheduler.dksSetBitMap(i, gState.dgsTaskQueneHead[i].readyTask);
    }
}

void dTaskRegist(dTaskControlBlock *taskHead) {
    if(taskHead == gState.dgsKernelIdleTask || taskHead == nullptr) dfInitializationFault_NoTasks();
    PriorityRegistry allTaskPriority[PRIORITIES_TOTAL_NUMBER] = {};

    uint8_t priorityNum = collectPriorityInfo(taskHead, allTaskPriority);

    allocQueueHeads(priorityNum);

    gState.dgsTaskQueneHead[0].queuePriority = -SPECIAL_PRIORITY_OFFSET;    //保留优先级(所有优先级最高)

    distributeTasksToQueues(taskHead, allTaskPriority);

    readyTaskCount();
}
