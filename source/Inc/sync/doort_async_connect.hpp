#ifndef DOORT_ASYNC_CONNECT_HPP
#define DOORT_ASYNC_CONNECT_HPP

#include <stdint.h>
#include <stdbool.h>

#include "doort.h"

// 槽位: 注册的待调用函数记录(绑定任务, KernelBlock 分配)
struct daSlot {
    void (*daSlotFunction)(void*);      // 槽函数
    void* daContext;                    // 槽函数参数
    dTaskRef daTask;                    // 绑定任务: 该槽函数在该任务的上下文里执行
    struct daSlot* daNext;              // 槽注册链表
};

// 信号(connect): 每个实例 = 一个信号, 维护已连接的槽列表(Qt 式)
class daConnectSignal {
public:
    struct daSlot** daConnectedSlots;       // KernelBlock 分配的连接槽指针数组
    uint16_t daCapacity;                    // 可连接槽数
    uint16_t daUsedCount;                   // 已连接数
    daConnectSignal* daNext;                // 信号注册链表

    void  daInit(struct daSlot** slotArray, uint16_t capacity);     // create 时初始化
    bool  daConnect(struct daSlot* slot);                           // 把已注册槽加入连接(可任意处调用)
    bool  daDisconnect(struct daSlot* slot);                        // 从连接摘除
    bool  daTrigger(void* argument);
};

// 内部接口: 供 doort.cpp 中转调用, 与 doort.h 公开 C API 一一对应
daSignalHandle dacCreate(uint16_t slotCapacity);
daSlotHandle   dacSlotCreate(dTaskRef task, void (*slotFunction)(void*), void* context);
bool dacConnect(daSignalHandle signal, daSlotHandle slot);
bool dacDisconnect(daSignalHandle signal, daSlotHandle slot);
bool dacTrigger(daSignalHandle signal, void* argument);

// 内核内部: 任务切换进/启动时排空其延迟回调
void daConnectSignalDrainTask(dTaskRef task);

#endif      /* DOORT_ASYNC_CONNECT_HPP */
