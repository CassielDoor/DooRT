#ifndef DOORT_TASK_BLOCKED_HPP
#define DOORT_TASK_BLOCKED_HPP
#include <cstdint>
#include "doort_task.hpp"

struct BlockedSeqList {
    int AwakeTick;
    dTaskControlBlock *BlockedTask;
};

class dTaskBlockedManager {
private:
    BlockedSeqList *dtbBlockedList;
    uint16_t dtbHeadPointer;
    uint16_t dtbTailPointer;
    uint16_t dtbBlockTaskCount;
    void dtbInsertBlockTask(int index, uint32_t Tick);
    void dtbInitBlockedList(void);

public:
    dTaskBlockedManager() :
        dtbBlockedList(nullptr),
        dtbHeadPointer(0),
        dtbTailPointer(0),
        dtbBlockTaskCount(0)
        {}
    //~dTaskBlockedManager(); 全局阻塞器不析构

    uint32_t dtbNextTaskUnblockTime;
    void dtbTaskBlocked(uint32_t delayTick);
    void dtbTaskUnblock(void);
    void dtbTaskCancel(dTaskControlBlock *tcb);     //摘除指定任务的延时登记(不存在则空操作), 需调用方关中断
    bool dtbIsTickExpired(uint32_t AwakeTick, uint32_t currentTick);
    bool dtbTaskDue();
    BlockedSeqList *dtbGetSeqList(void);

    friend class dkScheduler;
};

#endif      /* DOORT_TASK_BLOCKED_HPP */