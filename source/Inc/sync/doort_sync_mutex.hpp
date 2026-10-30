#ifndef DOORT_SYNC_MUTEX_HPP
#define DOORT_SYNC_MUTEX_HPP

#include <stdint.h>
#include <stdbool.h>

#include "doort.h"

//互斥量最大等待任务数, 可在工程配置处覆盖
#ifndef DOORT_SYNC_MUTEX_MAX_WAITERS
#define DOORT_SYNC_MUTEX_MAX_WAITERS 8
#endif

class dsMutex {
public:
    bool dsMutexLocked;                                         //是否被持有
    dTaskRef dsMutexOwner;                                      //当前持有者
    uint16_t dsMutexHoldCount;                                  //递归持有计数
    int8_t dsMutexBasePriority;                                 //持有者获取锁时的原始优先级(优先级继承用)
    uint16_t dsMutexWaitCount;                                  //等待任务数
    dTaskRef dsMutexWaitList[DOORT_SYNC_MUTEX_MAX_WAITERS];     //等待任务表(按序唤醒)

    void dsMutexInit(void);
    bool dsMutexLock(void);
    bool dsMutexTryLock(void);
    bool dsMutexUnlock(void);
    void dsMutexWakeAll(void);      //恢复原始优先级+唤醒全部等待者(删除原语时用)
};

//内部接口 中转调用
dsMutexHandle dsMutexCreate(void);
void dsMutexDelete(dsMutexHandle handle);
bool dsMutexLock(dsMutexHandle handle);
bool dsMutexTryLock(dsMutexHandle handle);
bool dsMutexUnlock(dsMutexHandle handle);

#endif      //DOORT_SYNC_MUTEX_HPP
