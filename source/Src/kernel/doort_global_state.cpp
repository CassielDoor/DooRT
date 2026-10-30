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
    addr = addr + DOORT_KERNEL_BLOCK_SIZE * 1024ULL;
    dgsKernelEnd = (void*)addr;

    #if !DOORT_HERP_ADDRESS_FOLLOW_KERNEL
    addr = DOORT_HERP_ADDRESS;
    addr = (addr + 7) & ~0x7ULL;        //八字节对齐向后取整避免和核心尾发生踩踏
    dgsHerpAddr = (void*)addr;
    addr = addr + DOORT_HEAP_BLOCK_SIZE * 1024ULL;
    dgsHerpEnd = (void*)addr;
    #endif

    #if DOORT_HERP_ADDRESS_FOLLOW_KERNEL
    addr = (uintptr_t)dgsKernelEnd;     //直接以核心尾作为堆地址并八字节向后取整
    addr = (addr + 7) & ~0x7ULL;
    dgsHerpAddr = (void*)addr;
    addr = addr + DOORT_HEAP_BLOCK_SIZE * 1024ULL;
    dgsHerpEnd = (void*)addr;
    #endif

}