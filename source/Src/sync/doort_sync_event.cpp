#include "doort_sync_event.hpp"
#include "doort_memory_handle.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_async_connect.hpp"

extern dkScheduler kScheduler;

static dsEvent* dsEventDereference(dsEventHandle handle) {      //句柄转指针, 每次现解析抗压缩
    if (handle == DSE_INVALID_HANDLE) return nullptr;
    return (dsEvent*)dmhHandleDereference(handle);
}

static inline bool dsEventCondMet(uint32_t flags, uint32_t need, bool waitAll) {    //判断等待条件是否满足
    if (waitAll) return (flags & need) == need;                                     //全满足
    return (flags & need) != 0;                                                     //任一满足
}

void dsEvent::dsEventInit(void) {
    dsEventFlags     = 0;
    dsEventWaitCount = 0;
}

bool dsEvent::dsEventSet(uint32_t flags) {
    dsEventFlags |= flags;                                      //置位事件标志

    //遍历等待表, 把条件已满足的等待者摘出并唤醒(可能唤醒多个)
    uint16_t n = 0;
    dTaskRef wakeList[DOORT_SYNC_EVENT_MAX_WAITERS];            //本地记录待唤醒者
    uint16_t i = 0;
    while (i < dsEventWaitCount) {
        dsEventWaiter* w = &dsEventWaitList[i];
        if (dsEventCondMet(dsEventFlags, w->dsEventWaitFlags, w->dsEventWaitAll)) {
            wakeList[n++] = w->dsEventTask;                     //条件满足, 记录唤醒对象
            for (uint16_t j = i; j + 1 < dsEventWaitCount; j++)
                dsEventWaitList[j] = dsEventWaitList[j + 1];    //前移填补空洞
            dsEventWaitCount--;
        } else {
            i++;                                                //条件未满足则跳过
        }
    }
    ENABLE_IRQ();                                               //开中断后再唤醒

    for (uint16_t k = 0; k < n; k++) dTaskTargetReady(wakeList[k]);
    return true;
}

bool dsEvent::dsEventClear(uint32_t flags) {
    dsEventFlags &= ~flags;     //清掉指定位
    return true;
}

bool dsEvent::dsEventWait(uint32_t flags, bool waitAll) {
    if (flags == 0) return false;

    if (dsEventCondMet(dsEventFlags, flags, waitAll)) {     //已满足则直接返回
        return true;
    }

    if (dsEventWaitCount >= DOORT_SYNC_EVENT_MAX_WAITERS) return false;     //等待表满则失败

    dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();
    dsEventWaiter* w = &dsEventWaitList[dsEventWaitCount];
    w->dsEventTask      = (dTaskRef)currentTask;    //登记等待任务
    w->dsEventWaitFlags = flags;                    //记录等待掩码
    w->dsEventWaitAll   = waitAll;                  //记录全满足/任一满足
    dsEventWaitCount++;

    currentTask->dtStatusCode = DOORT_TASK_STATUS_BLOCKED;      //置阻塞
    kScheduler.dksModifyCountAndMap(currentTask, 0);            //移出就绪
    kScheduler.dksSelectPerformTask();                          //选下一个任务
    kScheduler.dksSwapTask();                                   //切换
    ENABLE_IRQ();
    daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调
    return true;                                                            //被唤醒后回到这里
}

bool dsEvent::dsEventTryWait(uint32_t flags, bool waitAll) {    //非阻塞尝试等待
    if (flags == 0) return false;
    return dsEventCondMet(dsEventFlags, flags, waitAll);
}

uint32_t dsEvent::dsEventGetFlags(void) {
    return dsEventFlags;
}

void dsEvent::dsEventWakeAll(void) {    //删除事件组时唤醒全部等待者
    uint16_t n = dsEventWaitCount;
    dTaskRef wakeList[DOORT_SYNC_EVENT_MAX_WAITERS];
    for (uint16_t i = 0; i < n; i++) wakeList[i] = dsEventWaitList[i].dsEventTask;      //先拷到本地再清空
    dsEventWaitCount = 0;

    ENABLE_IRQ();
    for (uint16_t i = 0; i < n; i++) dTaskTargetReady(wakeList[i]);
}

dsEventHandle dseCreate(void) {
    uint16_t id = dmhAlloc(sizeof(dsEvent));    //从堆分配对象
    if (id == DSE_INVALID_HANDLE) return DSE_INVALID_HANDLE;

    dsEvent* eg = dsEventDereference(id);
    if (eg == nullptr) {
        dmhFree(id);
        return DSE_INVALID_HANDLE;
    }

    eg->dsEventInit();
    return id;
}

void dseDelete(dsEventHandle handle) {
    DISABLE_IRQ();
    dsEvent* eg = dsEventDereference(handle);
    if (eg == nullptr) {
        ENABLE_IRQ();
        return;
    }
    eg->dsEventWakeAll();       //先唤醒等待者
    dmhFree(handle);
}

bool dseSet(dsEventHandle handle, uint32_t flags) {
    DISABLE_IRQ();      //关中断保证解引用+使用原子
    dsEvent* eg = dsEventDereference(handle);
    if (eg == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = eg->dsEventSet(flags);
    ENABLE_IRQ();
    return r;
}

bool dseClear(dsEventHandle handle, uint32_t flags) {
    DISABLE_IRQ();
    dsEvent* eg = dsEventDereference(handle);
    if (eg == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = eg->dsEventClear(flags);
    ENABLE_IRQ();
    return r;
}

bool dseWait(dsEventHandle handle, uint32_t flags, bool waitAll) {
    if (flags == 0) return false;

    DISABLE_IRQ();
    dsEvent* eg = dsEventDereference(handle);
    if (eg == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = eg->dsEventWait(flags, waitAll);
    ENABLE_IRQ();
    return r;
}

bool dseTryWait(dsEventHandle handle, uint32_t flags, bool waitAll) {
    if (flags == 0) return false;

    DISABLE_IRQ();
    dsEvent* eg = dsEventDereference(handle);
    if (eg == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = eg->dsEventTryWait(flags, waitAll);
    ENABLE_IRQ();
    return r;
}

uint32_t dseGetFlags(dsEventHandle handle) {
    DISABLE_IRQ();
    dsEvent* eg = dsEventDereference(handle);
    if (eg == nullptr) {
        ENABLE_IRQ();
        return 0;
    }
    uint32_t f = eg->dsEventGetFlags();
    ENABLE_IRQ();
    return f;
}

