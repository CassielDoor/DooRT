//d -> door
#ifndef DOORT_STATE_HPP
#define DOORT_STATE_HPP

#include <stdint.h>

class dTaskControlBlock;

struct PriorityRegistry{
    bool isEnable;
    uint8_t taskNum;
};

struct dTaskQueueHead{
    dTaskControlBlock *body = nullptr;
    uint8_t readyTask = 0;
    int8_t queuePriority = 0;
    uint8_t taskNum = 0;
};

class dgState {     //doort global State
public:
    dgState();
    //~dgState();//全局状态不析构
    uint32_t dgsCoreFreq;
    uint32_t dgsSysTick;
    uint32_t dgsHeapSize;
    uint32_t dgsTickCount;
    uint16_t dgsTaskNum;
    uint8_t dgsPrioritiesNum;

    void* dgsKernelAddr;
    void* dgsKernelEnd;
    void* dgsHerpAddr;
    void* dgsHerpEnd;

    dTaskQueueHead *dgsTaskQueneHead = nullptr;

    dTaskControlBlock* dgsKernelIdleTask = nullptr;
    dTaskControlBlock* dgsKernelMemoryTask = nullptr;       //内存紧缩任务句柄

    bool dgsInScheduler = false;
};

#endif      /* DOORT_STATE_HPP */