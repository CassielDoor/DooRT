#include<string.h>
#include <new>
#include <stdint.h>
#include "doort_kernel_scheduler.hpp"
#include "doort_fault.hpp"
#include "doort_global_state.hpp"
#include "doort_task.hpp"
#include "doort_native_task.hpp"
#include "doort_task_blocked.hpp"
#include "doort_memory_manage.hpp"
#include "doort_bitmap_tool.hpp"

#include "doort_config.h"
#include "doort_debug.h"

extern dTaskBlockedManager dtb;
extern dMemoryManager dmm;

volatile dTaskControlBlock *tCurrentTaskPointer = nullptr;      //临时值, 用于pendSV内联汇编
volatile dTaskControlBlock *tNextTaskPointer = nullptr;         //临时值, 用于pendSV内联汇编


//工具声明
dTaskControlBlock* dksGetFirstTask(void);
//class构造


void dkScheduler::dksInitPriorityBitMap(void) {
    dksPriorityBitMapBlocksNum = (gState.dgsPrioritiesNum + 31) >> 5;
    dksPriorityBitMap = dmKernelPlacementNewArray<uint32_t>(4, dksPriorityBitMapBlocksNum);
    dbtBitClean(dksPriorityBitMap, dksPriorityBitMapBlocksNum * 4);     //位图使用前必须先归零
}

void dkScheduler::dksSetCurrentTask(dTaskControlBlock *currentTask) {
    dksCurrentTask = currentTask;
}

void dkScheduler::dksSetNextTask(dTaskControlBlock *nextTask) {
    dksNextTask = nextTask;
}

dTaskControlBlock *dkScheduler::dksGetCurrentTask(void) {
    return dksCurrentTask;
}

dTaskControlBlock *dkScheduler::dksGetNextTask(void) {
    return dksNextTask;
}

void dkScheduler::dksSetBitMap(uint8_t bitPos, bool isBitSetTrue) {
    dbtBitSet(dksPriorityBitMap, bitPos, isBitSetTrue);
}

void dkScheduler::dksStartFirstTask(void) {
    dksCurrentTask = dksGetFirstTask();
    dksCurrentRunningPriority = dksCurrentTask->dtPriority;
    void *sp = dksCurrentTask->dTCBGetStackPointer();       // 获取伪造好的栈指针
    //DISABLE_IRQ();//为了防止进入第一个任务之前触发切栈, 这里必须关闭全局中断 260823修改, 关中断会导致SVC触发失败
    gState.dgsInScheduler = true;       //将调度开始打开, 此时SysTick将开始触发PendSV
    __asm volatile (
        "msr psp, %0          \n"       // 将 sp 写入 PSP 寄存器（不切换 CONTROL，Thread 仍使用 MSP）
        "svc #0                  \n"    // SVC 在 MSP 上压栈，任务栈保持完整
        : : "r" (sp)
        : "memory"
    );
}

uint8_t dkScheduler::dksGetHighestReadyIndex(void) {
    int i = 0;
    for(; i < dksPriorityBitMapBlocksNum; i++) {    //找到非0位
        if(dksPriorityBitMap[i] != 0) break;
    }
    if(i == dksPriorityBitMapBlocksNum) return 0xFF;            //哨兵: 位图全空, 无就绪任务, 禁止越界读位图
    return (__builtin_ctz(dksPriorityBitMap[i]) + (i * 32));    //返回前导0和前面的非零位
}

void dkScheduler::dksStartScheduler(void) {
    dntIdleTaskStart();     //启动最低优先级任务

    #if DOORT_HEAP_IS_ENABLE
    dntMemoryMoveTaskStart();
    #endif

    dTaskRegist(dTCBHead);
    dtb.dtbInitBlockedList();
    dksInitPriorityBitMap();
    dkBitMapSync();     //位图与就绪队列统计同步, 必须在 dksInitPriorityBitMap() 之后执行

    dUserInit();

    /* 内核到这里已经完整分配(任务/TCB/栈/各张表), 堆才接着它的顶指针放。
     * 注意: dUserInit 里不要调用 dHeapAlloc, 那时堆还没放。 */
    #if DOORT_HEAP_IS_ENABLE
    dmm.dmInitHeap();
    #endif

    dLog("\n[info]: Now SysTick: %d(us)\nNow CoreFreq: %d(Hz)\n", gState.dgsSysTick, gState.dgsCoreFreq);
    dp.dpSetSysTick(gState.dgsCoreFreq, gState.dgsSysTick);

    /* ---- 调试快照: 告诉上位机 DooRT 运行在哪里 ----
       必须声明在本函数(永不返回)自己的局部区, 不能放进一个会返回的辅助函数,
       否则函数返回后那块栈会被后续中断压栈覆盖。
       调度器启动后 MSP 停在 dksStartFirstTask 的 svc 处, 所有中断只会往更低的
       地址压栈, 所以这块内存一直有效, 不需要中断钩子, 也不需要链接脚本配合。
       上位机先按符号名 DoortDebug_Anchor 找到锚点, 再用 SWD 只读内存读快照。 */
    DoortDebugSnapshot debugSnapshot;
    dDebugPublishSnapshot(&debugSnapshot);

    dksStartFirstTask();
}

void dkScheduler::dksSelectPerformTask(void) {
    uint8_t readyIndex = dksGetHighestReadyIndex();
    if(readyIndex == 0xFF) dfInitializationFault_NoTasks();     //位图全空: 无任何就绪任务(含idle), 系统停摆
    dTaskQueueHead *head = &gState.dgsTaskQueneHead[readyIndex];
    dTaskControlBlock *body = nullptr;
    dTaskControlBlock *circle = nullptr;

    if(head->queuePriority == dksGetCurrentTask()->dtPriority)      //若此时运行任务优先级等于当前队列优先级, 则表示同优先级切换
        body = dksGetCurrentTask();                                 //为了切栈后不会又重新碰到它, 需要将body设为它
    else body = head->body;                                         //若当前运行的任务不属于前导零找到的优先级则表示非同优先级切换

    //时间片轮转: 当前任务就绪时跳到下一个任务, 让同优先级任务轮流获得CPU
    if(body == dksGetCurrentTask() && body->dtStatusCode == DOORT_TASK_STATUS_READY)
        body = body->nextTCB;
    if(body == nullptr) body = head->body;      //队尾回绕到队头: 否则队尾任务(如memmove)永远自己选自己, 独占CPU跑完整趟紧缩
    circle = (body == nullptr) ? dksGetCurrentTask() : body;

    while(body != nullptr && body->dtStatusCode  != DOORT_TASK_STATUS_READY) {
        body = body->nextTCB;
        if(body == nullptr) body = head->body;      //如果到尽头, 则回到头
        if(body == circle) {                        //如果已套圈, 则整个队列没有就绪任务
            if(dksGetCurrentTask()->dtStatusCode == DOORT_TASK_STATUS_READY) {
                dksNextTask = dksGetCurrentTask();      //当前任务仍是唯一就绪任务, 继续运行
                return;
            }
            dksSetBitMap(readyIndex, 0);    //重置位图对应0
            dLog("[error]: Priority scheduling failed, leading zero error.");
            head->readyTask = 0;    //位图前导零出现了错误, 这里将当前任务归零
            readyIndex = dksGetHighestReadyIndex();
            head = &gState.dgsTaskQueneHead[readyIndex];
            body = head->body;      //重新拿到优先级任务链表, 重新进行调度
            circle = body;
        }
    }
    dksNextTask = (body == nullptr) ? dksGetCurrentTask() : body;       //找到下一个任务
}

void dkScheduler::dksSwapTask(void) {
    tCurrentTaskPointer = dksGetCurrentTask();                      //正在运行的任务
    tNextTaskPointer = dksGetNextTask();                            //即将运行的任务
    dksSetCurrentTask((dTaskControlBlock *)tNextTaskPointer);       //将即将运行的任务变成当前运行的任务, 然后触发PendSV切换任务栈

    dp.TriggerPendSV();     //任务切换位置不会触发精确数据访问违规
}

void dkScheduler::dksModifyCountAndMap(dTaskControlBlock *tcb, bool increment) {
    dTaskControlBlock *temp = tcb;
    // 队列按 queuePriority 升序排列, 统一走 dksFindQueueIndex() 的二分查找
    int i = dksFindQueueIndex(temp->dtPriority);
    if (i < 0) return;      // 未找到对应优先级: 防越界, 不修改计数/位图
    if (increment) {
        if(!gState.dgsTaskQueneHead[i].readyTask) dksSetBitMap(i, 1);
        gState.dgsTaskQueneHead[i].readyTask++;
    }
    else {
        gState.dgsTaskQueneHead[i].readyTask--;
        if(!gState.dgsTaskQueneHead[i].readyTask) dksSetBitMap(i, 0);
    }
}

//按优先级值查找队列下标, 找不到返回-1
//队列按 queuePriority 升序排列, 用二分查找: O(log n) 而不是线性扫描
int dkScheduler::dksFindQueueIndex(int8_t priority) {
    int low = 0;
    int high = (int)gState.dgsPrioritiesNum - 1;
    while (low <= high) {
        int mid = low + ((high - low) >> 1);
        int8_t p = gState.dgsTaskQueneHead[mid].queuePriority;
        if (p == priority) return mid;
        if (p < priority) low = mid + 1;
        else high = mid - 1;
    }
    return -1;
}

//从队列中摘除任务(不动就绪计数)
static void dksUnlinkTask(dTaskControlBlock *tcb, uint8_t queueIndex) {
    dTaskQueueHead *head = &gState.dgsTaskQueneHead[queueIndex];
    if (tcb == head->body) {
        head->body = tcb->nextTCB;
        if (head->body) head->body->prevTCB = nullptr;
    }
    else {
        if (tcb->prevTCB) tcb->prevTCB->nextTCB = tcb->nextTCB;
        if (tcb->nextTCB) tcb->nextTCB->prevTCB = tcb->prevTCB;
    }
    tcb->nextTCB = nullptr;
    tcb->prevTCB = nullptr;
    if (head->taskNum) head->taskNum--;
}

//将任务挂到队列尾部(不动就绪计数)
static void dksLinkTask(dTaskControlBlock *tcb, uint8_t queueIndex) {
    dTaskQueueHead *head = &gState.dgsTaskQueneHead[queueIndex];
    dTaskControlBlock *tail = head->body;
    if (tail == nullptr) head->body = tcb;
    else {
        while (tail->nextTCB) tail = tail->nextTCB;
        tail->nextTCB = tcb;
        tcb->prevTCB = tail;
    }
    head->taskNum++;
}

//只摘链表(用于滑动任务); 同时维护 taskNum(与 dksUnlinkTask 对齐)
static void dksDetachTask(dTaskControlBlock *tcb, uint8_t queueIndex) {
    dTaskQueueHead *head = &gState.dgsTaskQueneHead[queueIndex];
    if (tcb == head->body) {
        head->body = tcb->nextTCB;
        if (head->body) head->body->prevTCB = nullptr;
    }
    else {
        if (tcb->prevTCB) tcb->prevTCB->nextTCB = tcb->nextTCB;
        if (tcb->nextTCB) tcb->nextTCB->prevTCB = tcb->prevTCB;
    }
    tcb->nextTCB = nullptr;
    tcb->prevTCB = nullptr;
    if (head->taskNum) head->taskNum--;     // 防止 taskNum 与链表实际长度脱节
}

//只挂链表, (用于滑动任务); 同时维护 taskNum(与 dksLinkTask 对齐)
static void dksAttachTask(dTaskControlBlock *tcb, uint8_t queueIndex) {
    dTaskQueueHead *head = &gState.dgsTaskQueneHead[queueIndex];
    dTaskControlBlock *tail = head->body;
    if (tail == nullptr) head->body = tcb;
    else {
        while (tail->nextTCB) tail = tail->nextTCB;
        tail->nextTCB = tcb;
        tcb->prevTCB = tail;
    }
    head->taskNum++;    // 防止 taskNum 与链表实际长度脱节
}

void dkScheduler::dksSetTaskPriority(dTaskControlBlock *tcb, int8_t newPriority) {
    if (tcb == nullptr) return;
    if (tcb->dtPriority == newPriority) return;
    int oldIdx = dksFindQueueIndex(tcb->dtPriority);
    int newIdx = dksFindQueueIndex(newPriority);
    if (oldIdx < 0 || newIdx < 0) return;       //目标优先级未注册队列则不生效

    if (tcb->dtStatusCode == DOORT_TASK_STATUS_READY)
        dksModifyCountAndMap(tcb, 0);
    dksUnlinkTask(tcb, oldIdx);
    tcb->dtPriority = newPriority;
    dksLinkTask(tcb, newIdx);
    if (tcb->dtStatusCode == DOORT_TASK_STATUS_READY)
        dksModifyCountAndMap(tcb, 1);
}

//内存紧缩任务调度: 无固定优先级, 始终跟随当前运行优先级滑动; 不参与队列计数/位图
void dkScheduler::dksMemoryTaskUpdate(void) {
    dTaskControlBlock *mt = gState.dgsKernelMemoryTask;
    if (mt == nullptr) return;

    if (!dmm.dmCompactionActive()) {
        if (mt->dtStatusCode == DOORT_TASK_STATUS_READY)
            mt->dtStatusCode = DOORT_TASK_STATUS_SUSPENDED;     //挂起只改状态, 不碰计数/位图
        return;
    }

    int8_t curPri = dksCurrentTask->dtPriority;
    if (mt->dtPriority != curPri) {
        int oldIdx = dksFindQueueIndex(mt->dtPriority);
        int newIdx = dksFindQueueIndex(curPri);
        if (oldIdx >= 0 && newIdx >= 0) {
            dksDetachTask(mt, oldIdx);      //滑动只改链表挂载, 不碰计数/位图
            mt->dtPriority = curPri;
            dksAttachTask(mt, newIdx);
        }
    }
    if (mt->dtStatusCode != DOORT_TASK_STATUS_READY)
        mt->dtStatusCode = DOORT_TASK_STATUS_READY;     //就绪只改状态, 不影响位图
}




//内部工具实现
dTaskControlBlock* dksGetFirstTask(void) {
    for (int i = 0; i < gState.dgsPrioritiesNum; i++) {
        if (gState.dgsTaskQueneHead[i].body != nullptr) {       //找到一个非空优先级
            dTaskControlBlock *temp = gState.dgsTaskQueneHead[i].body;
            while(temp != nullptr && temp->dtStatusCode != DOORT_TASK_STATUS_READY) {
                temp = temp->nextTCB;
            }
            if(temp != nullptr) return temp;
        }
    }
    dfInitializationFault_NoTasks();    //不应到达
    return nullptr;
}


