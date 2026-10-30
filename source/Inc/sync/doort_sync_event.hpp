#ifndef DOORT_SYNC_EVENT_HPP
#define DOORT_SYNC_EVENT_HPP

#include <stdint.h>
#include <stdbool.h>

#include "doort.h"

//事件组最大等待任务数, 可在工程配置处覆盖
#ifndef DOORT_SYNC_EVENT_MAX_WAITERS
#define DOORT_SYNC_EVENT_MAX_WAITERS 8
#endif

struct dsEventWaiter {
    dTaskRef dsEventTask;           //等待任务
    uint32_t dsEventWaitFlags;      //等待的标志位掩码
    bool dsEventWaitAll;            //true=全满足 false=任一满足
};

class dsEvent {
public:
    uint32_t dsEventFlags;                                                  //当前事件标志位
    uint16_t dsEventWaitCount;                                              //等待任务数
    struct dsEventWaiter dsEventWaitList[DOORT_SYNC_EVENT_MAX_WAITERS];     //等待任务表

    void dsEventInit(void);
    bool dsEventSet(uint32_t flags);
    bool dsEventClear(uint32_t flags);
    bool dsEventWait(uint32_t flags, bool waitAll);
    bool dsEventTryWait(uint32_t flags, bool waitAll);
    uint32_t dsEventGetFlags(void);
    void dsEventWakeAll(void);      //唤醒全部等待者(删除原语时用)
};

//内部接口 中转调用
dsEventHandle dseCreate(void);
void dseDelete(dsEventHandle handle);
bool dseSet(dsEventHandle handle, uint32_t flags);
bool dseClear(dsEventHandle handle, uint32_t flags);
bool dseWait(dsEventHandle handle, uint32_t flags, bool waitAll);
bool dseTryWait(dsEventHandle handle, uint32_t flags, bool waitAll);
uint32_t dseGetFlags(dsEventHandle handle);

#endif      /* DOORT_SYNC_EVENT_HPP */
