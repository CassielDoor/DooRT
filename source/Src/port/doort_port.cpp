#include "doort_port.hpp"
#include "doort_fault.hpp"
#include "doort_task.hpp"
#include "doort.h"
#include <stdint.h>

#define MINIMUM_INTERRUPT_CYCLES 50U

typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_Type;

#define SYSTICK_BASE      (0xE000E010UL)
#define SysTick           ((SysTick_Type *)SYSTICK_BASE)
#define SYSTICK_CTRL_COUNTFLAG_Msk   (1UL << 16)

void dPort::dpSetSysTick(uint32_t SysTickFreq , uint32_t us) {
    uint32_t usCycles = SysTickFreq/1000000;    //一微秒的周期数
    uint32_t Cycles = usCycles*us;              //每次中断周期数（寻找准确微秒值）
    if(SysTickFreq == 0 || us == 0 || (Cycles < MINIMUM_INTERRUPT_CYCLES)) dfInitializationFault_TickTimeTooShort();
    //零除或值余量不足最小周期数报错
    if(Cycles > 0xFFFFFFU) {
        dLog("[warning]: The interrupt period has exceeded the SysTick interrupt maximum, ");
        dLog("[warning]: the default maximum will be used. \n");
        dLog("[warning]: Current Value: %d, Systick Maximum Value: %d", Cycles, 0xFFFFFFU);
        Cycles = 0xFFFFFFU;
    }
    SysTick->CTRL = 0;
    SysTick->LOAD = Cycles -1;
    SysTick->VAL = 0;
    // 不在此处使能: 由 SVC_Handler 恢复首任务上下文后使能,
    // 否则 SVC 启动前 SysTick 触发会执行 PendSV(PSP 未切换), 写地址 0 → 崩溃
     SysTick->CTRL = (1 << 2) | (1 << 1) | (1 << 0);    //SysTick使能
     //使能位置为假性故障, 因SysTick使用if (!gState.dgsIsInScheduler)判断是否进入调度器, 不会执行PendSV
}

uint32_t* dPort::dpInitTaskStack(uint32_t* StackHighAddr, void (*dTaskFunc)(void* dtParameters), void* arg) {
    uint32_t *sp = StackHighAddr;

    *--sp = 0x01000000;
    *--sp = ((uint32_t)dTaskFunc) | 1;
    *--sp = ((uint32_t)dfTaskFault_UnexpectedReturn) | 1;
    *--sp = 0;      // R12
    *--sp = 0;      // R3
    *--sp = 0;      // R2
    *--sp = 0;      // R1
    *--sp = (uint32_t)arg;

    *--sp = 0;      // R11
    *--sp = 0;      // R10
    *--sp = 0;      // R9
    *--sp = 0;      // R8
    *--sp = 0;      // R7
    *--sp = 0;      // R6
    *--sp = 0;      // R5
    *--sp = 0;      // R4

    return sp;
}

/*栈帧伪造
第一字节 0x01000000 -> 第 24 位是 Thumb指令集状态位，必须为 1
第二字节 程序执行起始位 -> 任务函数
第三字节 程序返回执行位 -> 意外返回函数
第四到七字节 临时寄存器, 初值填0
第八字节 程序运行第一个参数, ARM规定第一个参数需使用这种办法传递
第九到十六字节 上下文切换时软件手动压栈, 需留出空位
*/