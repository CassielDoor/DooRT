#ifndef DOORT_NATIVE_TASK_CPP
#define DOORT_NATIVE_TASK_CPP

#ifdef __cplusplus
extern "C" {    //防止Cpp编译器生成别名
#endif

void dntIdleTaskStart(void);
void dntMemoryMoveTaskStart(void);

#ifdef __cplusplus
}
#endif

#endif