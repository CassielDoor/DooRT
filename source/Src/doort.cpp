#include <stddef.h>
#include <new>
#include <stdint.h>

#include "doort_memory_manage.hpp"
#include "doort_global_state.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"
#include "doort_fault.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_task_blocked.hpp"
#include "doort_memory_handle.hpp"

#include "doort_sync_semaphore.hpp"
#include "doort_sync_mutex.hpp"
#include "doort_sync_event.hpp"
#include "doort_sync_post.hpp"

#include "doort_async_connect.hpp"

#include "doort.h"      //内有config, 必须放于最低下

volatile dgState gState;
dTaskControlBlock *dTCBHead = nullptr;      //链表
dTaskBlockedManager dtb;
dPort dp;
dMemoryManager dmm;
dkScheduler kScheduler;

//暴露区

#define NVIC_SYSPRI2_REGS   (*(volatile uint32_t *)0xE000ED20UL)
#define NVIC_SYSPRI2_PENDSV (0xFFUL << 16)      //PendSV  优先级 15
#define NVIC_SYSPRI2_SYSTICK (0xFFUL << 24)     //SysTick 优先级 15

void dInitSystemClock(uint32_t dgsCoreFreq) {
    gState.dgsCoreFreq = dgsCoreFreq;
}

void dInitSysTick(uint32_t dgsSysTickUs) {
    gState.dgsSysTick = dgsSysTickUs;
}

dTaskRef dTaskCreate(dTaskParams *taskParams) {
    dTaskControlBlock *newTask = dNewTask(taskParams);      //造一个标准新任务

    taskParams->dtHandle = (void*)newTask;

    if(dTCBHead == nullptr) {
        dTCBHead = newTask;         //头为空则挂载到头上, 前驱默认为空
    }
    else {
        dTaskControlBlock *temp = dTCBHead;
        while(temp->nextTCB != nullptr) temp = temp->nextTCB;       //找到尾
        temp->nextTCB = newTask;
        newTask->prevTCB = temp;    //前驱指向
    }

    return (void*)newTask;
}

dTaskRef dTaskCreateSuspend(struct dTaskParams *taskParams) {
    dTaskControlBlock *newTask = (dTaskControlBlock*)dTaskCreate(taskParams);
    newTask->dtStatusCode = DOORT_TASK_STATUS_SUSPENDED;
    return newTask;
}

dTaskParams dTaskStackInherit(dTaskRef *parentTask, dtFunction dtFunc) {
    //未实现, 需要解析并挂载, 然后链接父子级关系
    dTaskParams temp = {};
    return temp;
}

void dTaskSuspend(void) {
    DISABLE_IRQ();
    kScheduler.dksGetCurrentTask()->dtStatusCode = DOORT_TASK_STATUS_SUSPENDED;
    dTaskControlBlock *temp = kScheduler.dksGetCurrentTask();

    kScheduler.dksModifyCountAndMap(temp, 0);

    kScheduler.dksSelectPerformTask();
    kScheduler.dksSwapTask();                                               //挂起PendSV, 打开中断后立即切换
    ENABLE_IRQ();                                                           //因关中断, 所以dSwapTaskBeforeSuspend(i);不会立即切栈, 需等到这句开中断后再切栈
    daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调
    return;
}

void dTaskDelay(uint32_t delayTick) {
    DISABLE_IRQ();
    dTaskControlBlock *temp = nullptr;
    temp = kScheduler.dksGetCurrentTask();
    temp->dtStatusCode = DOORT_TASK_STATUS_BLOCKED;

     kScheduler.dksModifyCountAndMap(temp, 0);

    dtb.dtbTaskBlocked(delayTick);

    kScheduler.dksSelectPerformTask();
    kScheduler.dksSwapTask();       //挂起PendSV, 打开中断后立即切换
    ENABLE_IRQ();
    daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调
    return;
}

void dTaskYield(void) {
    DISABLE_IRQ();
    kScheduler.dksSelectPerformTask();      //先选中下一个任务
    kScheduler.dksSwapTask();               //切换
    ENABLE_IRQ();
    daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调
}

void dTaskTargetSuspend(dTaskRef dtHandle) {
    DISABLE_IRQ();
    dTaskControlBlock* temp = (dTaskControlBlock*)dtHandle;
    if(temp  == kScheduler.dksGetCurrentTask()) {       //如果挂起的是自己就执行挂起自己
        dTaskSuspend();
        return;
    }
    if(temp ->dtStatusCode == DOORT_TASK_STATUS_SUSPENDED) return;      //如果已经挂起就直接返回
    temp->dtStatusCode = DOORT_TASK_STATUS_SUSPENDED;

    kScheduler.dksModifyCountAndMap(temp, 0);

    ENABLE_IRQ();
    return;
}

void dTaskTargetReady(dTaskRef dtHandle) {
    DISABLE_IRQ();
    dTaskControlBlock* temp = (dTaskControlBlock*)dtHandle;
    if( temp == kScheduler.dksGetCurrentTask() ||
        temp->dtStatusCode == DOORT_TASK_STATUS_READY) {
        ENABLE_IRQ();
        return;     //如果就绪的是自己或者句柄本身就是就绪态直接返回
    }

    temp->dtStatusCode = DOORT_TASK_STATUS_READY;

    kScheduler.dksModifyCountAndMap(temp, 1);

    if(temp->dtPriority < kScheduler.dksCurrentRunningPriority) {       //若是更高优先级的任务
        kScheduler.dksCurrentRunningPriority = temp->dtPriority;
        kScheduler.dksSelectPerformTask();
        kScheduler.dksSwapTask();
    }
    ENABLE_IRQ();
    return;
}

void dTaskSetPriority(dTaskRef dtHandle, int8_t newPriority) {
    DISABLE_IRQ();
    kScheduler.dksSetTaskPriority((dTaskControlBlock*)dtHandle, newPriority);
    ENABLE_IRQ();
}

int8_t dTaskGetPriority(dTaskRef dtHandle) {
    dTaskControlBlock *temp = (dTaskControlBlock*)dtHandle;
    return temp ? temp->dtPriority : 0;
}

//主调度器
void dStartScheduler(void) {
    //PendSV/SysTick 设为最低优先级(15), 否则默认 0(最高)会打断 UART 等中断
    NVIC_SYSPRI2_REGS |= (NVIC_SYSPRI2_PENDSV | NVIC_SYSPRI2_SYSTICK);
    kScheduler.dksStartScheduler();
}

uint16_t GetTaskID(void) {
    return kScheduler.dksGetCurrentTask()->dtUniqueTaskID;
}

uint8_t  GetTaskStatu(void) {
    return kScheduler.dksGetCurrentTask()->dtStatusCode;
}

uint8_t GetCurrentReadyTaskCount(void) {
    int i = 0;
    for(; gState.dgsTaskQueneHead[i].queuePriority != (kScheduler.dksGetCurrentTask())->dtPriority; i++);       //使用二分查找
    return gState.dgsTaskQueneHead[i].readyTask;
}

void* GetBlockSeqList(void) {
    return (void*)dtb.dtbGetSeqList();
}

void TestCompaction(void) {
    dmm.dmRunCompaction();
}

uint8_t dHeapUsagePercent(void) {
    return dmm.dmGetUsagePercent();
}

uint8_t dHeapFragPercent(void) {
    return dmm.dmGetFragPercent();
}

void* dHandleDereference(uint16_t id) {
    return dmhHandleDereference(id);
}

uint16_t dHeapAlloc(size_t size) {
    return dmhAlloc(size);
}

void dHeapFree(uint16_t id) {
    dmhFree(id);
}

//需实现, 同步原语, 在同步原语之前可能需要先实现增量内存紧缩模型和静态池    已实现
//超级提权给软中断, weak函数用户自行实现
//增量内存紧缩模型, 同步原语, 复用栈                                    已实现增量紧缩和同步原语, 未实现复用栈

//==================== 同步原语: C API 中转站 ====================
//每个 API 一句话转发到 sync 原语自己的 cpp 内部接口

//信号量
dsSemHandle dsSemCreate(uint16_t maxCount, uint16_t initCount) { return dsSemaphoreCreate(maxCount, initCount); }
void dsSemDelete(dsSemHandle handle) { dsSemaphoreDelete(handle); }
bool dsSemGive(dsSemHandle handle) { return dsSemaphoreGive(handle); }
bool dsSemTake(dsSemHandle handle) { return dsSemaphoreTake(handle); }
bool dsSemTryTake(dsSemHandle handle) { return dsSemaphoreTryTake(handle); }

//互斥量(优先级继承)
dsMutexHandle dsMutexCreate(void) { return dsMutexCreate(); }
void dsMutexDelete(dsMutexHandle handle) { dsMutexDelete(handle); }
bool dsMutexLock(dsMutexHandle handle) { return dsMutexLock(handle); }
bool dsMutexTryLock(dsMutexHandle handle) { return dsMutexTryLock(handle); }
bool dsMutexUnlock(dsMutexHandle handle) { return dsMutexUnlock(handle); }

//事件组
dsEventHandle dsEventCreate(void) { return dsEventCreate(); }
void dsEventDelete(dsEventHandle handle) { dsEventDelete(handle); }
bool dsEventSet(dsEventHandle handle, uint32_t flags) { return dsEventSet(handle, flags); }
bool dsEventClear(dsEventHandle handle, uint32_t flags) { return dsEventClear(handle, flags); }
bool dsEventWait(dsEventHandle handle, uint32_t flags, bool waitAll) { return dsEventWait(handle, flags, waitAll); }
bool dsEventTryWait(dsEventHandle handle, uint32_t flags, bool waitAll) { return dsEventTryWait(handle, flags, waitAll); }
uint32_t dsEventGetFlags(dsEventHandle handle) { return dsEventGetFlags(handle); }

//邮箱(post)
dsPostHandle dsPostCreate(uint16_t capacity, uint8_t overflowPolicy) { return dsPostPostCreate(capacity, overflowPolicy); }
void dsPostDelete(dsPostHandle handle) { dsPostPostDelete(handle); }
bool dsPostSend(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent) { return dsPostPostSend(handle, mailId, subject, urgent); }
bool dsPostTrySend(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent) { return dsPostPostTrySend(handle, mailId, subject, urgent); }
bool dsPostSendWait(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent, uint32_t ticks) { return dsPostPostSendWait(handle, mailId, subject, urgent, ticks); }
uint16_t dsPostRecv(dsPostHandle handle) { return dsPostPostRecv(handle); }
uint16_t dsPostTryRecv(dsPostHandle handle) { return dsPostPostTryRecv(handle); }
uint16_t dsPostRecvWait(dsPostHandle handle, dsPostSubject subject, uint32_t ticks) { return dsPostPostRecvWait(handle, subject, ticks); }
uint16_t dsPostCount(dsPostHandle handle) { return dsPostPostCount(handle); }

//异步信号槽(connect)
daSignalHandle daConnectCreate(uint16_t slotCapacity) { return daConnectSignalCreate(slotCapacity); }
daSlotHandle   daConnectSlot(dTaskRef task, void (*fn)(void*), void* ctx) { return daConnectSlotCreate(task, fn, ctx); }
bool daConnect(daSignalHandle sig, daSlotHandle slot) { return daConnectSignalConnect(sig, slot); }
bool daDisconnect(daSignalHandle sig, daSlotHandle slot) { return daConnectSignalDisconnect(sig, slot); }
bool daConnectTrigger(daSignalHandle sig, void* arg) { return daConnectSignalTrigger(sig, arg); }