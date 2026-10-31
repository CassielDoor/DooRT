//d - dooRT
#include <cstdint>
#include "doort_global_state.hpp"

#include "doort_config.h"

dgState::dgState() {
    dgsCoreFreq = 100000;
    dgsSysTick = 1000;
    dgsTaskNum = 0;
    dgsHeapSize = 0;
    dgsTickCount = 0;
    dgsPrioritiesNum = 0;

    /* ADDRESS */
    uint64_t addr = DOORT_KERNEL_ADDRESS;
    addr = addr & ~0x3ULL;              //四字节对齐向前取整
    dgsKernelAddr = (void*)addr;

    /* 内核不再预留固定块大小: 它从起始地址一路往上分配, 顶指针就是 dgsKernelAddr.
     * 堆的位置在 dmInitHeap() 里按当时的顶指针定. */
    #if !DOORT_HERP_ADDRESS_FOLLOW_KERNEL
    /* 固定堆地址: 内核最多长到堆基址, 长过去由 dmAddressSafety 抛越界 */
    addr = DOORT_HERP_ADDRESS;
    addr = (addr + 7) & ~0x7ULL;        //八字节对齐向后取整避免和核心尾发生踩踏
    dgsHerpAddr = (void*)addr;
    dgsHerpEnd = (void*)(addr + DOORT_HEAP_BLOCK_SIZE * 1024ULL);
    dgsKernelEnd = dgsHerpAddr;
    #endif

    #if DOORT_HERP_ADDRESS_FOLLOW_KERNEL
    /* 跟随内核: 这里先给个 SDRAM 末尾当临时上限, 真正的上限在 dmInitHeap()
     * 里收紧成堆基址(内核完整分配之后才确定). */
    dgsHerpAddr = nullptr;
    dgsHerpEnd = nullptr;
    dgsKernelEnd = (void*)0xE0000000ULL;
    #endif

}