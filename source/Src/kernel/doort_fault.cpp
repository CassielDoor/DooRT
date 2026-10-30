#include <cstdint>
#include <exception>

#include "doort_fault.hpp"
#include "doort_port.hpp"
#include "doort_global_state.hpp"


extern volatile dgState gState;

void dfInitializationFault_TickTimeTooShort(void) {
    /*How about giving some feedback, use (func)dLog or something?*/
    
    while(1) {
        /*The current tick time is too short, the scheduler will get stuck in interrupts forever*/
    }
}

void dfInitializationFault_TaskStackTooSmall(void) {
    /*How about giving some feedback, use (func)dLog or something?*/

    while(1) {
        /*One of your task stacks is too small, 
        there's not even enough space to save the context, 
        so the scheduler can't run the task properly.*/
    }
}

void dfInitializationFault_UnsupportedPriority(void) {
    /*How about giving some feedback, use (func)dLog or something?*/

    while(1) {
        /*One of your tasks used an unsupported priority, 
        so this task couldn't be created properly.
        Please use priority levels from -2 to 127, This should be enough.*/
    }
}

void dfInitializationFault_NoTasks(void) {
    /*How about giving some feedback, use (func)dLog or something?*/

    while(1){
        /*The task registered func didn't find any valid task registrations, 
        please check if the tasks are registered.
        Or there are no ready tasks when the scheduler starts.*/
    }
}

void dfInitializationFault_DispatchQueueInitializeFailed(void) {
    /*How about giving some feedback, use (func)dLog or something?*/

    while(1){
        /*Not enough kernel memory, please consider reducing the task stack size.*/
    }
}

void dfInitializationFault_FailedToCreateTask(uint32_t RemainingMemory, uint32_t dTCBsize) {
    /*How about giving some feedback, use (func)dLog or something?*/

    dLog("[error]: A task creation failed. Reason: Insufficient kernel block memory.\n");
    dLog("[error]: Remaining memory: %dbyte. Required memory: %dbyte\n",RemainingMemory, dTCBsize);

    while(1) {
        /*A Task failed to be created due to insufficient core memory.*/
    }
}

void dfTaskFault_UnexpectedReturn() {
    /*How about giving some feedback, use (func)dLog or something?*/
    //dLog("[error]: Unexpected return, returning task handle: %p", kScheduler.dksGetCurrentTask()); //Uncomment to print errors.

    while(1){
        /*The current task using return to returned a value or null.
        could this be unexpected behavior?
        if this isn't unexpected behavior
        please use dtReturn() to reset the task status.*/
    }
}

void dfMemoryFault_AccessViolation(void) {
    /*How about giving some feedback, use (func)dLog or something?*/

    while(1){
        /*When alloc memory some placement new accessed memory that’s not allowed, 
        please check DOORT_KERNEL_ADDRESS or DOORT_HERP_ADDRESS in config.h mayde is not correct.*/
    }
}
