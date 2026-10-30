#include <stdint.h>
#include <stdbool.h>

#include "doort_memory_manage.hpp"
#include "doort_async_connect.hpp"
#include "doort_port.hpp"
#include "doort_global_state.hpp"
#include "doort_task.hpp"
#include "doort_kernel_scheduler.hpp"


extern dkScheduler kScheduler;
extern volatile dgState gState;

// 信号 / 槽 注册链表(均 KernelBlock placement new)
static daConnectSignal *gaSignalList = nullptr;
static daSlot *gaSlotList   = nullptr;
static uint16_t gaSignalCount = 0;
static uint16_t gaSlotCount   = 0;

static daConnectSignal* daFindSignal(daSignalHandle signal) {
    if (signal >= gaSignalCount) return nullptr;
    daConnectSignal* current = gaSignalList;
    for (uint16_t i = 0; i < signal && current != nullptr; i++) current = current->daNext;
    return current;
}
static daSlot* daFindSlot(daSlotHandle slot) {
    if (slot >= gaSlotCount) return nullptr;
    daSlot* current = gaSlotList;
    for (uint16_t i = 0; i < slot && current != nullptr; i++) current = current->daNext;
    return current;
}

// 把一次延迟调用入队到目标任务的 pending 队列(零动态分配, 满则丢弃该次触发)
static void daEnqueuePending(dTaskControlBlock* task, void (*slotFunction)(void*), void* context) {
    if (task->dtPendingSignalCount >= DOORT_SIGNAL_PENDING_PER_TASK) return;
    uint8_t index = (uint8_t)((task->dtPendingSignalHead + task->dtPendingSignalCount) % DOORT_SIGNAL_PENDING_PER_TASK);
    task->dtPendingSignal[index].daFn  = slotFunction;
    task->dtPendingSignal[index].daCtx = context;
    task->dtPendingSignalCount++;
}

//==================== class daConnectSignal 实现 ====================

void daConnectSignal::daInit(struct daSlot** slotArray, uint16_t capacity) {
    daConnectedSlots = slotArray;
    daCapacity       = capacity;
    daUsedCount      = 0;
}

bool daConnectSignal::daConnect(struct daSlot* slot) {
    if (slot == nullptr) return false;
    for (uint16_t i = 0; i < daUsedCount; i++) if (daConnectedSlots[i] == slot) return true;    //已连接
    if (daUsedCount >= daCapacity) return false;                                                //满
    daConnectedSlots[daUsedCount++] = slot;
    return true;
}

bool daConnectSignal::daDisconnect(struct daSlot* slot) {
    for (uint16_t i = 0; i < daUsedCount; i++) {
        if (daConnectedSlots[i] == slot) {
            daConnectedSlots[i] = daConnectedSlots[daUsedCount - 1];    //末位补位
            daUsedCount--;
            return true;
        }
    }
    return false;
}

bool daConnectSignal::daTrigger(void* argument) {
    dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();
    for (uint16_t i = 0; i < daUsedCount; i++) {
        daSlot* slot = daConnectedSlots[i];
        if (slot->daTask == (dTaskRef)currentTask) {
            slot->daSlotFunction(argument);     //绑定任务==当前任务: 内联执行
        } else {
            daEnqueuePending((dTaskControlBlock*)slot->daTask, slot->daSlotFunction, argument);     //延迟到目标任务
        }
    }
    return true;
}

//==================== 内部接口: 供 doort.cpp 中转调用 ====================

daSignalHandle daConnectCreate(uint16_t slotCapacity) {

    if (slotCapacity == 0 || gaSignalCount >= UINT16_MAX) return DAS_INVALID_HANDLE;

    daConnectSignal* signal = dmKernelPlacementNew<daConnectSignal>(4);
    if (signal == nullptr) return DAS_INVALID_HANDLE;
    daSlot** slotArray = dmKernelPlacementNewArray<daSlot*>(4, slotCapacity);

    if (slotArray == nullptr) return DAS_INVALID_HANDLE;

    signal->daInit(slotArray, slotCapacity);
    signal->daNext = nullptr;

    if (gaSignalList == nullptr) gaSignalList = signal;

    else {
        daConnectSignal* tail = gaSignalList;
        while (tail->daNext) tail = tail->daNext;
        tail->daNext = signal;
    }

    return (daSignalHandle)(gaSignalCount++);
}

daSlotHandle daConnectSlot(dTaskRef task, void (*slotFunction)(void*), void* context) {
    if (slotFunction == nullptr || gaSlotCount >= UINT16_MAX) return DAS_INVALID_HANDLE;
    daSlot* slot = dmKernelPlacementNew<daSlot>(4);
    if (slot == nullptr) return DAS_INVALID_HANDLE;
    slot->daSlotFunction = slotFunction;
    slot->daContext      = context;
    slot->daTask         = task;
    slot->daNext         = nullptr;
    if (gaSlotList == nullptr) gaSlotList = slot;

    else {
        daSlot* tail = gaSlotList;
        while (tail->daNext)
            tail = tail->daNext;
        tail->daNext = slot;
    }

    return (daSlotHandle)(gaSlotCount++);
}

bool daConnect(daSignalHandle signal, daSlotHandle slot) {
    daConnectSignal* sig = daFindSignal(signal);
    daSlot* sl = daFindSlot(slot);
    if (sig == nullptr || sl == nullptr) return false;
    return sig->daConnect(sl);
}

bool daDisconnect(daSignalHandle signal, daSlotHandle slot) {
    daConnectSignal* sig = daFindSignal(signal);
    daSlot* sl = daFindSlot(slot);
    if (sig == nullptr || sl == nullptr) return false;
    return sig->daDisconnect(sl);
}

bool daConnectTrigger(daSignalHandle signal, void* argument) {
    daConnectSignal* sig = daFindSignal(signal);
    if (sig == nullptr) return false;
    return sig->daTrigger(argument);
}

// 任务启动/切换进入时调用: 排空该任务的延迟信号回调
void daConnectSignalDrainTask(dTaskRef task) {
    dTaskControlBlock* tcb = (dTaskControlBlock*)task;
    while (tcb->dtPendingSignalCount > 0) {
        uint8_t index = tcb->dtPendingSignalHead;
        void (*slotFunction)(void*) = tcb->dtPendingSignal[index].daFn;
        void* context               = tcb->dtPendingSignal[index].daCtx;
        tcb->dtPendingSignalHead = (uint8_t)((tcb->dtPendingSignalHead + 1) % DOORT_SIGNAL_PENDING_PER_TASK);
        tcb->dtPendingSignalCount--;
        slotFunction(context);      //在目标任务上下文执行延迟回调
    }
}
