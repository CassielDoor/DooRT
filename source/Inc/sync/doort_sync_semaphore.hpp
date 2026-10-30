#ifndef DOORT_SYNC_SEMAPHORE_HPP
#define DOORT_SYNC_SEMAPHORE_HPP

#include <stdint.h>
#include <stdbool.h>

#include "doort.h"

//信号量最大等待任务数, 可在工程配置处覆盖
#ifndef DOORT_SYNC_SEMAPHORE_MAX_WAITERS
#define DOORT_SYNC_SEMAPHORE_MAX_WAITERS 8
#endif

class dsSemaphore {
public:
    uint16_t dsSemMaxCount;                                         //信号量最大计数值
    uint16_t dsSemCount;                                            //当前计数值
    uint16_t dsSemWaitCount;                                        //等待任务数
    dTaskRef dsSemWaitList[DOORT_SYNC_SEMAPHORE_MAX_WAITERS];       //等待任务表(按序唤醒)

    void dsSemInit(uint16_t maxCount, uint16_t initCount);
    bool dsSemGive(void);
    bool dsSemTake(void);
    bool dsSemTryTake(void);
    void dsSemWakeAll(void);    //唤醒全部等待者(删除原语时用)
};

#endif      /* DOORT_SYNC_SEMAPHORE_HPP */
