#define UART_HARD_FAULT_OUTPUT 0    //待修改, 这里需要使用dLog或者别的方案, 此输出方案依赖特定版本hal库

#include "doort_global_state.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_task.hpp"
#include "doort_task_blocked.hpp"

#include "doort_config.h"

extern volatile dgState gState;
extern dkScheduler kScheduler;
volatile uint32_t g_fault_frame[8];     //栈帧
extern dTaskBlockedManager dtb;

// 调试数组：记录每次插入时列表前5个元素的 AwakeTick
uint32_t g_debugTicks[50] = {0};

extern "C" {
void NMI_Handler(void)
{
    while (1)
    {
    }
}

/**
    * @brief This function handles Hard fault interrupt.
    *        现场无法用调试器监视时, 直接把 CFSR/BFAR/MMFAR/PC/LR/SP/寄存器
    *        用轮询方式从 USART1 打出来, 让串口直接给出违规地址。
    */
void HardFault_Handler(void)
{
    uint32_t stackPtr;

    // 取栈指针(MSP 还是 PSP 由 EXC_RETURN.bit2 决定), 并抓硬件栈帧 R0-R3,R12,LR,PC,xPSR
    __asm volatile (
        "tst  lr, #4               \n"
        "ite  eq                   \n"
        "mrseq r0, msp             \n"
        "mrsne r0, psp             \n"
        "mov  %0, r0               \n"
        "ldmia r0!, {r1-r4}        \n"
        "stmia %[arr]!, {r1-r4}    \n"
        "ldmia r0!, {r1-r4}        \n"
        "stmia %[arr]!, {r1-r4}    \n"
        : "=r"(stackPtr)
        : [arr] "r" (g_fault_frame)
        : "r0", "r1", "r2", "r3", "r4", "memory"
    );
    // g_fault_frame = [R0,R1,R2,R3, R12,LR,PC,xPSR]

    // 系统控制块寄存器
    //uint32_t cfsr  = *(volatile uint32_t *)0xE000ED28UL;
    //uint32_t mmfar = *(volatile uint32_t *)0xE000ED34UL;
    //uint32_t bfar  = *(volatile uint32_t *)0xE000ED38UL;

    while (1)
    {
    }
}

/**
    * @brief This function handles Memory management fault.
    */
void MemManage_Handler(void)
{
    while (1)
    {

    }
}

/**
    * @brief This function handles Pre-fetch fault, memory access fault.
    */
void BusFault_Handler(void)
{
    while (1)
    {

    }
}

/**
    * @brief This function handles Undefined instruction or illegal state.
    */
void UsageFault_Handler(void)
{
    while (1)
    {
        /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
        /* USER CODE END W1_UsageFault_IRQn 0 */
    }
}

/**
    * @brief This function handles System service call via SWI instruction.
    */
__attribute__((naked))      //去掉naked修饰SVC启动第一个任务不会造成INVPC. 260822修改, 重新加回naked, 在更高的优化下可能触发INVPC
void SVC_Handler(void)
{
    //gState.dgsInScheduler = true;//无竞态, SVC执行时不会被优先级低于它的SysTick抢断 260822修改, 将SVC改为裸函数, 防止序言污染
    //修改调度器就绪状态移动到了开始第一个任务的函数内, 且函数内同步关闭了中断, 需在当前内联汇编内再次打开中断
    //260823修改, 中断不影响裸函数执行, 但关中断会使进入SVC发生错误
    __asm volatile (
        "mrs r0, psp                  \n"       //拿到伪造栈, 此时有Task1的初始上下文
        "ldm r0, {r4-r11}           \n"         //从任务栈手动恢复R4-R11
        "add r0, r0, #32            \n"         //跳过R4-R11, 指向硬件弹出的位置
        "mov r1, #0                  \n"        //将R1归零
        "msr psp, r0                  \n"       //更新PSP指向异常帧底部 -> 若不更新PSP会导致尝试切换到无效状态
        "ldr lr, =0xFFFFFFFD     \n"            //把 EXC_RETURN 值写入 LR, 意思为返回到 Thread 模式, 并且使用 PSP
        //"cpsie i                      \n"  //打开全局中断, 关中断对在函数 void dkScheduler::dksStartFirstTask(void)  260823修改, 中断不影响裸函数执行
                                                   //目前只有此函数触发SVC, 其它函数使用软件中断触发需使用传递SVC参数
        "bx lr                            \n"       //异常返回
    );
    //改动, 删掉在SVC中启动SysTick的代码
}

/**
    * @brief This function handles Debug monitor.
    */
void DebugMon_Handler(void)
{

}

/* 裸函数: 通用上下文切换(不关心任务来源, 只按 TCB 直寻址切换)
 * tCurrentTaskPointer / tNextTaskPointer 由 SysTick 调度逻辑维护
 * 必须 naked: 非 naked 时 GCC 用 r7 做局部帧指针, 序言会污染 r7*/
__attribute__((naked))      //去掉naked修饰PendSV会在运行到第二轮后发生精确数据访问违规和非精确数据访问违规
void PendSV_Handler(void)
{
    __asm volatile (
        // ===== 1. 保存当前任务 =====
        // 硬件已把 xPSR/PC/LR/R12/R3-R0 压入当前任务栈(PSP)
        "mrs   r0, psp                              \n"     //r0 = 任务栈指针(硬件帧底部)
        "stmdb r0!, {r4-r11}                    \n"         // 保存 R4-R11, r0 = 新栈顶

        "ldr   r1, =tCurrentTaskPointer    \n"
        "ldr   r1, [r1]                                \n"      // r1 = 当前 TCB 指针
        "str   r0, [r1]                                \n"      // TCB->dtStackPointer(offset 0) = 新栈顶

        // ===== 2. 切换下一任务 =====
        "ldr   r1, =tNextTaskPointer       \n"
        "ldr   r1, [r1]                               \n"       // r1 = 下一 TCB 指针
        "ldr   r0, [r1]                               \n"       // r0 = 下一 TCB->dtStackPointer
        "ldmia r0!, {r4-r11}                    \n"             // 恢复 R4-R11
        "msr   psp, r0                             \n"          // PSP = 硬件帧底部
        "ldr   lr, =0xFFFFFFFD                 \n"
        "bx    lr                                       \n"     // 硬件弹出 xPSR/PC/LR/R12/R3-R0
    );
}

/**
    * @brief This function handles System tick timer.
    */
void SysTick_Handler(void)
{
    dIncTick();                 //系统滴答
    gState.dgsTickCount++;      //滴答后计数器增加

    if (!gState.dgsInScheduler) {       // 调度器启动前 PendSV 任务栈未就绪, 禁止触发
        return;
    }

    if(dtb.dtbTaskDue()) {
        dtb.dtbTaskUnblock();       //第一任务已到期才进行检查减少压栈开销
    }

    kScheduler.dksMemoryTaskUpdate();       //紧缩任务跟随当前运行优先级/无需求挂起
    kScheduler.dksSelectPerformTask();      // 先选中下一个要运行的任务, 给出有效的 dksNextTask
    kScheduler.dksSwapTask();               // 再触发 PendSV 上下文切换
}

}       /* end extern "C" */
//Cpp编译器会将所有的函数名称取全局唯一别名, 使用extern C使中断向量表正常指向内核重写的中断