#ifndef DOORT_MEMORY_HANDLE_HPP
#define DOORT_MEMORY_HANDLE_HPP

#include "doort_config.h"

//不要尝试理解这个文件内的宏魔法, 魔法岂是尔等凡人所能参透之物?

#if DOORT_ENABLE_INCREMENTAL_MEMORY_COMPACTOR

#include <stdint.h>
#include <stddef.h>
#include "doort_config_user_type.h"

/* ---------- 生成句柄结构体 ---------- */
#define DOORT_HANDLE_STRUCT_DEFINE(type) \
    typedef struct { uint16_t id; } type##_dObjectHandle;
    //##拼接符号, 将宏调用中的type和_Hanlle拼接在一起, 自动生成句柄定义
DOORT_TYPES_LIST(DOORT_HANDLE_STRUCT_DEFINE)    //嵌套宏定义, 批量生成对应句柄类型

#undef DOORT_HANDLE_STRUCT_DEFINE       //释放无用宏

/* ----------- 底层接口声明 ----------- */
#ifdef __cplusplus
extern "C" {
#endif

void* dmhHandleDereference(uint16_t id);    //拿句柄id, 内部会正常返回当前裸值在内存中的位置
uint16_t dmhAlloc(size_t size);             //开辟裸空间, 并返回一个id
void dmhFree(uint16_t id);                  //释放id对应的内存空间

#ifdef __cplusplus
}
#endif

/* ----------- 生成解析函数 ----------- */
#define DOORT_RESOLVE_FUNC_DEFINE(type)                 \
    static inline type* type##_Resolve(type##_dObjectHandle h) {   \
        return (type*)dmhHandleDereference(h.id);                \
    }
    //type##_Resolve(type##_dObjectHandle h) -> type_Resolve(type_Handle h), 调用时传递handle
    //这个函数自动返回dmhHandleDereference的返回值, 也就是裸地址

DOORT_TYPES_LIST(DOORT_RESOLVE_FUNC_DEFINE)     //有多少个已注册类型就会有多少个函数

#undef DOORT_RESOLVE_FUNC_DEFINE    //释放无用宏

/* ---------- 生成创建/销毁函数 ---------- */
#define DOORT_CREATE_DESTROY_FUNC_DEFINE(type) \
    static inline type##_dObjectHandle type##_Create(void) { \
        return (type##_dObjectHandle){ .id = dmhAlloc(sizeof(type)) }; \
    } \
    static inline type##_dObjectHandle type##_CreateArray(size_t count) { \
        return (type##_dObjectHandle){ .id = dmhAlloc(sizeof(type) * count) }; \
    } \
    static inline void type##_Destroy(type##_dObjectHandle h) { \
        dmhFree(h.id); \
    }
    //Alloc函数返回id, 需要通过GET来访问对应裸地址
    //这里生成创建数组类型和创建单个类型的函数
    //最后生成对应放销毁函数

DOORT_TYPES_LIST(DOORT_CREATE_DESTROY_FUNC_DEFINE)      //有多少个已注册类型就会有多少个函数

#undef DOORT_CREATE_DESTROY_FUNC_DEFINE     //释放无用宏

/* ---------- 生成 GET 宏 ---------- */
#define DOORT_GET_GENERIC_BRANCH(type) \
    type##_dObjectHandle: type##_Resolve,

#define GET(h) _Generic((h),                                             \
    DOORT_TYPES_LIST(DOORT_GET_GENERIC_BRANCH) \
    default: (void)0                                                              \
)(h)
//_Generic的语法是:
/*
#define GET_TYPENAME(var) _Generic((var), \
    int: "int",                                                    \
    char: "char",                                               \
    default: "other type")
*/
//这个意思是在使用GET_TYPENAME(var)的时候会自动拿到var类型冒号后面的东西
//#define DOORT_GET_GENERIC_BRANCH(type)
//type##_dObjectHandle: type##_Resolve,   ->   每个类型对应每个表达式, 这样当宏触发的时候会根据类型选择一个已注册的表达式使用

#define GET_AT(handle, index) (GET(handle)[index])

/* 注意：DOORT_GET_GENERIC_BRANCH 必须保留 */

#endif      /* DOORT_ENABLE_INCREMENTAL_MEMORY_COMPACTOR */

#endif      /* DOORT_MEMORY_HANDLE_HPP */