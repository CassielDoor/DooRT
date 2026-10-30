#ifndef DOORT_SYNC_POST_HPP
#define DOORT_SYNC_POST_HPP

#include <stdint.h>
#include <stdbool.h>

#include "doort.h"

//邮箱(邮政)最大收发等待任务数, 可在工程配置处覆盖
#ifndef DOORT_SYNC_POST_MAX_WAITERS
#define DOORT_SYNC_POST_MAX_WAITERS 8
#endif

//槽位: 主题 + 邮件句柄(邮件实体留在堆上, 邮箱只搬运句柄, 零拷贝且抗压缩)
struct dsPostSlot {
    uint16_t dsPostSubject;
    uint16_t dsPostMailId;
};

//等待者条目(收/发通用); dsPostTimed 标记是否注册了内核超时(早醒时需同步摘表)
struct dsPostWaiter {
    dTaskRef dsPostTask;
    uint16_t dsPostSubject;
    bool dsPostTimed;
};

class dsPostPost {
public:
    uint16_t dsPostCapacity;            //槽位总数
    uint16_t dsPostCount;               //当前邮件数
    uint16_t dsPostHead;                //队首(接收位置)
    uint16_t dsPostUrgent;              //队首连续紧急邮件数
    uint8_t  dsPostOverflow;            //满箱策略
    uint16_t dsPostSlotsHandle;         //槽位数组自身的堆句柄
    uint16_t dsPostRecvWaitCount;       //空箱等待者
    struct dsPostWaiter dsPostRecvWaitList[DOORT_SYNC_POST_MAX_WAITERS];
    uint16_t dsPostSendWaitCount;       //满箱等待者
    struct dsPostWaiter dsPostSendWaitList[DOORT_SYNC_POST_MAX_WAITERS];

    void dsPostInit(uint16_t capacity, uint8_t overflowPolicy, uint16_t slotsHandle);
    bool dsPostSendWait(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent, uint32_t ticks);
    uint16_t dsPostRecvWait(dsPostHandle handle, dsPostSubject subject, uint32_t ticks);
    uint16_t dsPostCountMail(void);
    void dsPostCancelWaiters(void);     //撤销全部等待者的内核超时(删除原语时用)
};

#endif      /* DOORT_SYNC_POST_HPP */
