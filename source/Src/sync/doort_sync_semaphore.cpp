#include "doort_sync_semaphore.hpp"
#include "doort_memory_handle.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_async_connect.hpp"

extern dkScheduler kScheduler;

static dsSemaphore* dsSemDereference(dsSemHandle handle) {      //句柄转指针, 每次现解析抗压缩
    if (handle == DSS_INVALID_HANDLE) return nullptr;
    return (dsSemaphore*)dmhHandleDereference(handle);
}

void dsSemaphore::dsSemInit(uint16_t maxCount, uint16_t initCount) {
    dsSemMaxCount  = maxCount;
    dsSemCount     = initCount;
    dsSemWaitCount = 0;
}

bool dsSemaphore::dsSemGive(void) {
    if (dsSemWaitCount > 0) {                   //有等待者则唤醒队首; 一个give对应一次take, 不再累加计数
        dTaskRef waiter = dsSemWaitList[0];     //取队首等待者
        for (uint16_t i = 0; i + 1 < dsSemWaitCount; i++)
            dsSemWaitList[i] = dsSemWaitList[i + 1];    //把后续等待者前移填补队首空洞
        dsSemWaitCount--;

        ENABLE_IRQ();                   //开中断后再唤醒, 允许上下文切换
        dTaskTargetReady(waiter);       //唤醒等待者
        return true;
    }

    if (dsSemCount < dsSemMaxCount) dsSemCount++;       //无等待者则累加计数
    return true;
}

bool dsSemaphore::dsSemTake(void) {
    if (dsSemCount > 0) {       //有可用信号直接消费
        dsSemCount--;
        return true;
    }

    if (dsSemWaitCount >= DOORT_SYNC_SEMAPHORE_MAX_WAITERS) return false;       //等待表满则失败

    dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();
    dsSemWaitList[dsSemWaitCount++] = (dTaskRef)currentTask;                //把自己登记进等待表
    currentTask->dtStatusCode = DOORT_TASK_STATUS_BLOCKED;                  //置阻塞态
    kScheduler.dksModifyCountAndMap(currentTask, 0);                        //移出就绪计数/位图
    kScheduler.dksSelectPerformTask();                                      //选下一个要运行的任务
    kScheduler.dksSwapTask();                                               //触发切换
    ENABLE_IRQ();                                                           //切换在开中断后生效
    daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调
    return true;                                                            //被唤醒后回到这里继续
}

bool dsSemaphore::dsSemTryTake(void) {      //非阻塞获取
    if (dsSemCount > 0) {
        dsSemCount--;
        return true;
    }
    return false;
}

void dsSemaphore::dsSemWakeAll(void) {      //删除信号量时唤醒全部等待者
    uint16_t n = dsSemWaitCount;
    dTaskRef wakeList[DOORT_SYNC_SEMAPHORE_MAX_WAITERS];
    for (uint16_t i = 0; i < n; i++) wakeList[i] = dsSemWaitList[i];    //先拷到本地再清空
    dsSemWaitCount = 0;

    ENABLE_IRQ();
    for (uint16_t i = 0; i < n; i++) dTaskTargetReady(wakeList[i]);
}

dsSemHandle dssCreate(uint16_t maxCount, uint16_t initCount) {
    if (maxCount == 0) return DSS_INVALID_HANDLE;
    if (initCount > maxCount) initCount = maxCount;
    uint16_t id = dmhAlloc(sizeof(dsSemaphore));    //从堆分配对象
    if (id == DSS_INVALID_HANDLE) return DSS_INVALID_HANDLE;
    dsSemaphore* sm = dsSemDereference(id);
    if (sm == nullptr) {
        dmhFree(id);    //解析失败则回收内存
        return DSS_INVALID_HANDLE;
    }
    sm->dsSemInit(maxCount, initCount);
    return id;
}

void dssDelete(dsSemHandle handle) {
    DISABLE_IRQ();
    dsSemaphore* sm = dsSemDereference(handle);
    if (sm == nullptr) {
        ENABLE_IRQ();
        return;
    }
    sm->dsSemWakeAll();     //先唤醒等待者, 避免他们永久阻塞
    dmhFree(handle);
}

bool dssGive(dsSemHandle handle) {
    DISABLE_IRQ();      //关中断保证解引用+使用原子(防压缩搬移)
    dsSemaphore* sm = dsSemDereference(handle);
    if (sm == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = sm->dsSemGive();
    ENABLE_IRQ();
    return r;
}

bool dssTake(dsSemHandle handle) {
    DISABLE_IRQ();
    dsSemaphore* sm = dsSemDereference(handle);
    if (sm == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = sm->dsSemTake();
    ENABLE_IRQ();
    return r;
}

bool dssTryTake(dsSemHandle handle) {
    DISABLE_IRQ();
    dsSemaphore* sm = dsSemDereference(handle);
    if (sm == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = sm->dsSemTryTake();
    ENABLE_IRQ();
    return r;
}

