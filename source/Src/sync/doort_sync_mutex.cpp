#include "doort_sync_mutex.hpp"
#include "doort_memory_handle.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_async_connect.hpp"

extern dkScheduler kScheduler;

static dsMutex* dsMutexDereference(dsMutexHandle handle) {      //句柄转指针, 每次现解析抗压缩
    if (handle == DSM_INVALID_HANDLE) return nullptr;
    return (dsMutex*)dmm.dmDereferenceHandle(handle);
}

void dsMutex::dsMutexInit(void) {
    dsMutexLocked       = false;
    dsMutexOwner        = nullptr;
    dsMutexHoldCount    = 0;
    dsMutexBasePriority = 0;
    dsMutexWaitCount    = 0;
}

bool dsMutex::dsMutexLock(void) {
    dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();

    if (!dsMutexLocked) {       //锁空闲则直接占有
        dsMutexLocked       = true;
        dsMutexOwner        = (dTaskRef)currentTask;
        dsMutexHoldCount    = 1;
        dsMutexBasePriority = currentTask->dtPriority;      //记录占有者原始优先级
        return true;
    }

    if ((dTaskRef)currentTask == dsMutexOwner) {    //同一任务递归持有
        dsMutexHoldCount++;
        return true;
    }

    if (dsMutexWaitCount >= DOORT_SYNC_MUTEX_MAX_WAITERS) return false;     //等待表满则失败

    dsMutexWaitList[dsMutexWaitCount++] = (dTaskRef)currentTask;    //把自己登记进等待表

    //优先级继承: 若当前任务比持有者优先级更高, 把持有者提升到当前优先级, 防被中间优先级插队
    dTaskControlBlock* holder = (dTaskControlBlock*)dsMutexOwner;
    if (currentTask->dtPriority < holder->dtPriority)
        kScheduler.dksSetTaskPriority(holder, currentTask->dtPriority);

    currentTask->dtStatusCode = DOORT_TASK_STATUS_BLOCKED;      //置阻塞
    kScheduler.dksModifyCountAndMap(currentTask, 0);            //移出就绪
    kScheduler.dksSelectPerformTask();                          //选下一个任务
    kScheduler.dksSwapTask();                                   //切换
    ENABLE_IRQ();
    daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调
    return true;                                                            //被唤醒后回到这里, 等待转交锁
}

bool dsMutex::dsMutexTryLock(void) {    //非阻塞尝试加锁
    dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();

    if (!dsMutexLocked) {       //空闲则直接占有
        dsMutexLocked       = true;
        dsMutexOwner        = (dTaskRef)currentTask;
        dsMutexHoldCount    = 1;
        dsMutexBasePriority = currentTask->dtPriority;
        return true;
    }

    if ((dTaskRef)currentTask == dsMutexOwner) {    //递归持有
        dsMutexHoldCount++;
        return true;
    }

    return false;       //被占用且非本人, 不等待直接失败
}

bool dsMutex::dsMutexUnlock(void) {
    dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();
    if (!dsMutexLocked || (dTaskRef)currentTask != dsMutexOwner) return false;      //非持有者解锁非法

    if (dsMutexHoldCount > 1) {     //递归解锁
        dsMutexHoldCount--;
        return true;
    }

    //最后一次解锁: 先把持有者优先级恢复为原始值(继承结束)
    if (currentTask->dtPriority != dsMutexBasePriority)
        kScheduler.dksSetTaskPriority(currentTask, dsMutexBasePriority);
    dsMutexHoldCount = 0;

    if (dsMutexWaitCount > 0) {     //有等待者则把锁直接转交给队首
        dTaskRef waiter = dsMutexWaitList[0];
        for (uint16_t i = 0; i + 1 < dsMutexWaitCount; i++)
            dsMutexWaitList[i] = dsMutexWaitList[i + 1];    //前移填补队首空洞
        dsMutexWaitCount--;

        dsMutexOwner        = waiter;       //所有权转交
        dsMutexHoldCount    = 1;
        dsMutexBasePriority = ((dTaskControlBlock*)waiter)->dtPriority;

        //新持有者继承剩余等待者中最高优先级, 防止新持有者被中间优先级插队
        int8_t inherit = ((dTaskControlBlock*)waiter)->dtPriority;
        for (uint16_t i = 0; i < dsMutexWaitCount; i++) {
            int8_t p = ((dTaskControlBlock*)dsMutexWaitList[i])->dtPriority;
            if (p < inherit) inherit = p;
        }
        if (inherit < ((dTaskControlBlock*)waiter)->dtPriority)
            kScheduler.dksSetTaskPriority((dTaskControlBlock*)waiter, inherit);

        ENABLE_IRQ();
        dTaskTargetReady(waiter);       //唤醒新持有者
        return true;
    }

    dsMutexLocked = false;      //无等待者则释放锁
    dsMutexOwner  = nullptr;
    return true;
}

void dsMutex::dsMutexWakeAll(void) {    //删除互斥量时唤醒全部等待者
    //若持有者仍持锁且被提升过, 先恢复其原始优先级
    if (dsMutexLocked && dsMutexOwner != nullptr) {
        dTaskControlBlock* owner = (dTaskControlBlock*)dsMutexOwner;
        if (owner->dtPriority != dsMutexBasePriority)
            kScheduler.dksSetTaskPriority(owner, dsMutexBasePriority);
    }

    uint16_t n = dsMutexWaitCount;
    dTaskRef wakeList[DOORT_SYNC_MUTEX_MAX_WAITERS];
    for (uint16_t i = 0; i < n; i++) wakeList[i] = dsMutexWaitList[i];      //先拷到本地再清空
    dsMutexWaitCount = 0;

    ENABLE_IRQ();
    for (uint16_t i = 0; i < n; i++) dTaskTargetReady(wakeList[i]);
}

dsMutexHandle dsMutexCreate(void) {
    uint16_t id = dmm.dmAllocHandle(sizeof(dsMutex));    //从堆分配对象
    if (id == DSM_INVALID_HANDLE) return DSM_INVALID_HANDLE;

    dsMutex* mx = dsMutexDereference(id);
    if (mx == nullptr) {
        dmm.dmFreeHandle(id);
        return DSM_INVALID_HANDLE;
    }

    mx->dsMutexInit();
    return id;
}

void dsMutexDelete(dsMutexHandle handle) {
    DISABLE_IRQ();
    dsMutex* mx = dsMutexDereference(handle);
    if (mx == nullptr) {
        ENABLE_IRQ();
        return;
    }
    mx->dsMutexWakeAll();       //先唤醒等待者
    dmm.dmFreeHandle(handle);
}

bool dsMutexLock(dsMutexHandle handle) {
    DISABLE_IRQ();      //关中断保证解引用+使用原子
    dsMutex* mx = dsMutexDereference(handle);
    if (mx == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = mx->dsMutexLock();
    ENABLE_IRQ();
    return r;
}

bool dsMutexTryLock(dsMutexHandle handle) {
    DISABLE_IRQ();
    dsMutex* mx = dsMutexDereference(handle);
    if (mx == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = mx->dsMutexTryLock();
    ENABLE_IRQ();
    return r;
}

bool dsMutexUnlock(dsMutexHandle handle) {
    DISABLE_IRQ();
    dsMutex* mx = dsMutexDereference(handle);
    if (mx == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = mx->dsMutexUnlock();
    ENABLE_IRQ();
    return r;
}

