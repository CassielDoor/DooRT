/**
 * @file    doort_debug.cpp
 * @brief   DooRT 调试锚点与目录的填充。
 *
 * 锚点是普通全局变量(地址不写死): 上位机在 SRAM 里按 magic 扫描发现它。
 * 只做一次性的登记: 不挂中断、不刷新、不改变内核状态。
 */
#include <stdint.h>

#include "doort_debug.h"

#include "doort_config.h"
#include "doort_global_state.hpp"
#include "doort_task.hpp"
#include "doort_memory_manage.hpp"

extern volatile dgState gState;
extern volatile dTaskControlBlock *tCurrentTaskPointer;
extern dMemoryManager dmm;

/* 上位机按 16/68 字节、相同偏移解析, 布局改了这里先炸, 不要等运行期读到乱数据 */
static_assert(sizeof(DoortDebugAnchor) == 16, "DoortDebugAnchor layout changed");
static_assert(sizeof(DoortDebugSnapshot) == 68, "DoortDebugSnapshot layout changed");

/* 上位机靠这块内存里的 magic 发现锚点, 不能被 --gc-sections 收掉 */
extern "C" {
__attribute__((used)) DoortDebugAnchor DoortDebug_Anchor = {0u, 0u, 0u, 0u};
}

extern "C" void dDebugPublishSnapshot(DoortDebugSnapshot *snapshot)
{
    if (snapshot == nullptr) {
        return;
    }

    snapshot->ddMagic         = DOORT_DEBUG_MAGIC;
    snapshot->ddLayoutVersion = (uint16_t)DOORT_DEBUG_VERSION;
    snapshot->ddSnapshotSize  = (uint16_t)sizeof(DoortDebugSnapshot);

    /* 区域: 全部来自 doort_config, 与芯片/BSP 无关 */
    snapshot->ddKernelBlockEnd  = (uint32_t)(uintptr_t)gState.dgsKernelEnd;
    snapshot->ddKernelBlockBase = (uint32_t)((uintptr_t)DOORT_KERNEL_ADDRESS & ~(uintptr_t)0x3);

    snapshot->ddHeapBlockBase = (uint32_t)(uintptr_t)gState.dgsHerpAddr;
    snapshot->ddHeapBlockEnd  = (uint32_t)(uintptr_t)gState.dgsHerpEnd;

    /* 活数据: 只登记变量的地址, 值由上位机自己读 */
    snapshot->ddHeapTopSlotAddr     = (uint32_t)(uintptr_t)dmm.dmGetHeapTopSlot();
    snapshot->ddTickSlotAddr        = (uint32_t)(uintptr_t)&gState.dgsTickCount;
    snapshot->ddCurrentTaskSlotAddr = (uint32_t)(uintptr_t)&tCurrentTaskPointer;

    /* 堆元数据表: 同样是"只给地址", 上位机据此复原占用/空洞/紧缩进度 */
    snapshot->ddAddressIndexAddr      = (uint32_t)(uintptr_t)dmm.dmGetAddressIndexSlot();
    snapshot->ddAddressIndexCountAddr = (uint32_t)(uintptr_t)dmm.dmGetAddressIndexCountSlot();
    snapshot->ddObjectHandleAddr      = (uint32_t)(uintptr_t)dmm.dmGetObjectHandleSlot();
    snapshot->ddObjectNum             = (uint32_t)DOORT_HEAP_OBJECT_NUM;
    snapshot->ddEntrySize             = (uint32_t)dmm.dmGetEntrySize();
    snapshot->ddCompactWriteSlotAddr  = (uint32_t)(uintptr_t)dmm.dmGetCompactWriteSlot();
    snapshot->ddCompactingSlotAddr    = (uint32_t)(uintptr_t)dmm.dmGetCompactingSlot();
    snapshot->ddFragmentSlotAddr      = (uint32_t)(uintptr_t)dmm.dmGetFragmentSlot();

    /* 最后才写锚点: 上位机看到有效锚点就说明目录已经完整 */
    DoortDebug_Anchor.ddLayoutVersion = DOORT_DEBUG_VERSION;
    DoortDebug_Anchor.ddDirectoryAddr = (uint32_t)(uintptr_t)snapshot;
    DoortDebug_Anchor.ddDirectorySize = (uint32_t)sizeof(DoortDebugSnapshot);
    DoortDebug_Anchor.ddMagic         = DOORT_DEBUG_MAGIC;
}
