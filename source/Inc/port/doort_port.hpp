//p -> port
//d -> doort
#ifndef DOORT_PORT_HPP
#define DOORT_PORT_HPP
#include <stdint.h>
#include "doort_global_state.hpp"

// 关闭全局中断 (PRIMASK=1)
#define DISABLE_IRQ()    __asm__ volatile("cpsid i")
// 开启全局中断 (PRIMASK=0)
#define ENABLE_IRQ()     __asm__ volatile("cpsie i")

class dTaskControlBlock;
extern volatile dgState gState;

class dPort {
public:
    void dpSetSysTick(uint32_t SysTickFreq , uint32_t us);
    uint32_t* dpInitTaskStack(uint32_t* StackHighAddr, void (*dTaskFunc)(void* dtParameters), void* arg);
    void TriggerPendSV(void);
};

inline void dPort::TriggerPendSV(void) {
    volatile uint32_t *icsr = (volatile uint32_t *)0xE000ED04;
    *icsr |= (1 << 28);     // 设置 bit28: PENDSVSET
}

#endif      /* DOORT_PORT_HPP */
