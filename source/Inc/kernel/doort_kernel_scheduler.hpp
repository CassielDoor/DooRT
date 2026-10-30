#ifndef DOORT_KERNEL_SCHEDULER_HPP
#define DOORT_KERNEL_SCHEDULER_HPP
#include <cstdint>

#include "doort_global_state.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"


extern volatile dgState gState;
extern dPort dp;
extern dTaskControlBlock *dTCBHead;     //链表

extern volatile dTaskControlBlock *tCurrentTaskPointer;
extern volatile dTaskControlBlock *tNextTaskPointer;

class dkScheduler {
private:
    dTaskControlBlock *dksCurrentTask;
    dTaskControlBlock *dksNextTask;
    void dksInitPriorityBitMap(void);
    void dksStartFirstTask(void);
    uint8_t dksGetHighestReadyIndex(void);

public:
    dkScheduler() :
        dksCurrentTask(nullptr),
        dksNextTask(nullptr),
        dksCurrentRunningPriority(0),
        dksPriorityBitMapBlocksNum(0),
        dksPriorityBitMap(nullptr)
        {}

    int8_t dksCurrentRunningPriority;
    int8_t dksPriorityBitMapBlocksNum;
    uint32_t *dksPriorityBitMap;

    void dksSetCurrentTask(dTaskControlBlock *currentTask);
    void dksSetNextTask(dTaskControlBlock *nextTask);
    dTaskControlBlock *dksGetCurrentTask(void);
    dTaskControlBlock *dksGetNextTask(void);

    void dksModifyCountAndMap(dTaskControlBlock *tcb, bool isSetTrue);

    void dksSelectPerformTask(void);
    void dksSwapTask(void);

    void dksStartScheduler(void);

    void dksSetBitMap(uint8_t bitPos, bool isBitSetTrue);

    void dksSetTaskPriority(dTaskControlBlock *tcb, int8_t newPriority);    //任务优先级调整工具(同步原语优先级继承可复用)
    void dksMemoryTaskUpdate(void);                                         //内存紧缩任务跟随当前运行优先级, 无紧缩需求时挂起
};

#endif      /* DOORT_SCHEDULER_HPP */