#include "doort.h"
#include "doort_native_task.hpp"
#include "doort_global_state.hpp"

#include "doort_config.h"

extern volatile dgState gState;

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((weak)) void dntIdleTask(void* null) {
    /* empty */
    while(1);
}

__attribute__((weak)) dTaskParams dntIdleTaskHandle{
    .dtFunc = dntIdleTask,
    .dtPriority = INT8_MAX,
    .dtStackSize = 128,
    .dtHandle = nullptr,
    .dtParameters = nullptr
};

void dntIdleTaskStart(void) {
    dTaskDebugCreat(&dntIdleTaskHandle, "idle");     //带调试名字, 名字在栈底下方, 不占栈空间
    gState.dgsKernelIdleTask = (dTaskControlBlock*) dntIdleTaskHandle.dtHandle;     //拿到空任务句柄
}

#ifdef __cplusplus
}
#endif