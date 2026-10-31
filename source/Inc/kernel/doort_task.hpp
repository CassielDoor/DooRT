#ifndef DOORT_TASK_HPP
#define DOORT_TASK_HPP

#include "doort.h"
#include "doort_global_state.hpp"

#define DOORT_TASK_STATUS_READY 0
#define DOORT_TASK_STATUS_BLOCKED 1
#define DOORT_TASK_STATUS_SUSPENDED 2

extern volatile dgState gState;

//延迟信号回调条目(异步信号槽用)
struct daPendingEntry {
    void (*daFn)(void*);
    void* daCtx;
};

class dTaskControlBlock {

private:
    void *dtStackPointer;           // [offset 0] -- PendSV 硬编码偏移, 必须为第一个成员
    void *dtStackBase = nullptr;    //私有栈底指针
    uint32_t dtStackSize;           //栈大小

public:
    void (*dtFunction)(void* dtParameters);     //任务入口函数
    dTaskControlBlock *nextTCB;
    dTaskControlBlock *prevTCB;

#if DOORT_STACK_REUSE_ENABLED
    dTaskControlBlock *childTCB;    //处理多任务复用栈
    dTaskControlBlock *parentTCB;
#endif

    uint16_t dtUniqueTaskID;
    uint8_t dtStatusCode;
    int8_t dtPriority;      //优先级(-128 ~ +127)

    void (*dtUserFunction)(void*);                                              //任务原始入口(供信号排空包装调用)
    struct daPendingEntry dtPendingSignal[DOORT_SIGNAL_PENDING_PER_TASK];       //每任务延迟信号回调队列
    uint8_t dtPendingSignalHead;                                                //队列头
    uint8_t dtPendingSignalCount;                                               //队列中回调数

    dTaskControlBlock(void (*dTaskFunc)(void* dtParameters), int8_t priority, uint32_t StackSize = 1024) :
                            dtStackPointer(nullptr),
                            dtStackBase(nullptr),
                            dtStackSize(StackSize),
                            dtFunction(dTaskFunc),
                            nextTCB(nullptr),
                            prevTCB(nullptr),
                            dtPriority(priority),
                            dtUserFunction(dTaskFunc),
                            dtPendingSignalHead(0),
                            dtPendingSignalCount(0)
                            {}
    void dTCBSetStackSize(uint32_t stackSize);
    void dTCBSetStackPtr(void *addr);
    void dTCBSetStackPointer(void *sp);
    void *dTCBGetStackPointer(void);
    void *dTCBGetStackBase(void);
    uint32_t dTCBGetStackSize(void);
};

/// 建任务。taskName 可选: 非空时在栈底下方(低地址侧)留一块名字前缀, 不占栈空间;
/// 传 nullptr/空串时行为与不传完全一致(零额外内存)。
dTaskControlBlock *dNewTask(dTaskParams *taskParams, const char *taskName = nullptr);
void dTaskRegist(dTaskControlBlock *taskHead);
void dkBitMapSync(void);

#endif      /* DOORT_TASK_HPP */
