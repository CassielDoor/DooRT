#ifndef DOORT_FAULT_HPP
#define DOORT_FAULT_HPP

#include <stdint.h>
#include "doort.h"

class dTaskControlBlock;    //在别的文件实现

void dfInitializationFault_TickTimeTooShort(void);
void dfInitializationFault_TaskStackTooSmall(void);
void dfInitializationFault_UnsupportedPriority(void);
void dfInitializationFault_NoTasks(void);
void dfInitializationFault_DispatchQueueInitializeFailed(void);
void dfInitializationFault_FailedToCreateTask(uint32_t RemainingMemory, uint32_t dTCBsize);


void dfTaskFault_UnexpectedReturn(void);

void dfMemoryFault_AccessViolation(void);
#endif      /* DOORT_FAULT_HPP */