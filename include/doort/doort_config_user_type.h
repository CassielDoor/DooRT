#ifndef DOORT_CONFIG_USER_TYPE_H
#define DOORT_CONFIG_USER_TYPE_H

#include<stdint.h>

#include "doort_config.h"
#include "stdbool.h"
#if DOORT_ENABLE_INCREMENTAL_MEMORY_COMPACTOR

// 在这里定义自己的类型, 或在这里包含头文件后注册对应类型, 不允许嵌套包含
/* USER INCLUDE BEGIN */

/* USER INCLUDE END */

typedef struct MyStruct {       //测试用类型, 可以安全删除
    int testVal;
} MyStruct;

#define DOORT_TYPES_LIST(X) \
    X(int)                                     \
    X(float)                                  \
    X(double)                              \
    X(char)                                  \
    X(uint8_t)                               \
    X(uint16_t)                             \
    X(uint32_t)                             \
    X(size_t)                                 \
    X(uintptr_t)                            \
    X(intptr_t)                              \
    X(int8_t)                                 \
    X(int16_t)                               \
    X(int64_t)                               \
    X(uint64_t)                             \
    X(bool)                                  \
    X(MyStruct)


//将自定义类型注册在这里
#endif      /* DOORT_ENABLE_INCREMENTAL_MEMORY_COMPACTOR */

#endif      /* DOORT_CONFIG_USER_TYPE_H */