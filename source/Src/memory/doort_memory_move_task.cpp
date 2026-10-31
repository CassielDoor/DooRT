#include "doort_native_task.hpp"
#include "doort_global_state.hpp"
#include "doort_memory_manage.hpp"
#include "doort_task.hpp"

#include "doort_config.h"
#include "doort.h"

extern volatile dgState gState;
extern dMemoryManager dmm;

#ifdef __cplusplus
extern "C" {
#endif

void dntMemoryMoveTask(void* null) {
    (void)null;
    dLog("[mem] start\n");
    while(1) {
        if (dmm.dmCompactionActive()) {
            dmm.dmCompactionStep();     //执行一步压缩
            if (!dmm.dmCompactionActive())
                //自行挂起: 滑动任务不参与计数/位图, 只改状态后让出
                gState.dgsKernelMemoryTask->dtStatusCode = DOORT_TASK_STATUS_SUSPENDED;
        }
        dTaskYield();       //让出, 回到同级轮转
    }
}

__attribute__((weak)) dTaskParams dntMemoryMoveTaskHandle{
    .dtFunc = dntMemoryMoveTask,
    .dtPriority = INT8_MAX,     //最低优先级起步, 由调度器按需搬移到当前运行优先级
    .dtStackSize = 2048,        //任务内调dLog(vsnprintf+HAL_UART), 栈必须给足
    .dtHandle = nullptr,
    .dtParameters = nullptr
};

void dntMemoryMoveTaskStart(void) {
    dTaskDebugCreatSuspend(&dntMemoryMoveTaskHandle, "memMove");                            //挂起注册, 有紧缩需求时由SysTick唤醒
    gState.dgsKernelMemoryTask = (dTaskControlBlock*) dntMemoryMoveTaskHandle.dtHandle;     //保存紧缩任务句柄
}

#ifdef __cplusplus
}
#endif