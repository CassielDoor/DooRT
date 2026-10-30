#ifndef DOORT_CONFIG_H
#define DOORT_CONFIG_H

#define DOORT_HEAP_ALLOCATION_ENABLED 0     //标准库堆分配启用
#define DOORT_STACK_REUSE_ENABLED 0         //栈复用启用

#define DOORT_KERNEL_ADDRESS 0xD0000000         //调度器内核起始地址
#define DOORT_KERNEL_BLOCK_SIZE 128             //内核块内存大小 unit KB
#define DOORT_HEAP_IS_ENABLE 1                  //是否启用堆, 元数据需要部分空间, 不建议内存过小的芯片启用堆, 当前堆启用编译裁剪不完整, 需补完
#define DOORT_HERP_ADDRESS_FOLLOW_KERNEL 0      //静态堆跟随内核分配
#define DOORT_HEAP_BLOCK_SIZE 32                //静态堆内存大小 unit KB
#define DOORT_HEAP_OBJECT_NUM 2300              //堆对象数量

#if !DOORT_HERP_ADDRESS_FOLLOW_KERNEL       //如果不跟随分配
#define DOORT_HERP_ADDRESS 0xD1000000       //提供堆地址
#endif                                      //需要做堆内存重叠检查

#define DOORT_SUGGESTION_MIN_STACK 256                          //建议最小栈大小 unit byte
#define PHYSICAL_MIN_STACK  64                                  //最小物理栈大小, 低于将无法保存完整上下文
#define SPECIAL_PRIORITY_OFFSET 3                               //-1软件中断 -2硬件中断 -3超级提权
#define PRIORITIES_TOTAL_NUMBER 128+SPECIAL_PRIORITY_OFFSET     //总优先级数量

#define DOORT_ENABLE_INCREMENTAL_MEMORY_COMPACTOR 1     //启用增量式内存紧缩器, 可能影响一点任务运行速度

#define DOORT_SIGNAL_PENDING_PER_TASK 4     //异步信号槽: 每任务延迟回调队列容量(零动态分配, 满则丢弃该次触发)

/*                                                            weakFunc                                                            */
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((weak)) void dLog(const char *format, ...);
    /*You need to implement a logging function yourself*/
    /* empty */
__attribute__((weak)) void dIncTick(void);
    /*You need to define the timer for the interrupt yourself*/
    /* empty */
__attribute__((weak)) void dUserInit(void);
#ifdef __cplusplus
}
#endif














#if !DOORT_HEAP_ALLOCATION_ENABLED

#define malloc(size) ({ _Pragma("GCC error \"[DooRT] malloc is forbidden.\""); (void)(size); })
#define calloc(num, size) ({ _Pragma("GCC error \"[DooRT] calloc is forbidden.\""); (void)(num); (void)(size); })
#define realloc(ptr, size) ({ _Pragma("GCC error \"[DooRT] realloc is forbidden.\""); (void)(ptr); (void)(size); })
#define free(ptr) ({ _Pragma("GCC error \"[DooRT] free is forbidden.\""); (void)(ptr); })
#define strdup(str) ({ _Pragma("GCC error \"[DooRT] strdup is forbidden.\""); (void)(str); })
#define asprintf(strp, fmt, ...) ({ _Pragma("GCC error \"[DooRT] asprintf is forbidden. \""); (void)(strp); (void)(fmt); (void)(__VA_ARGS__); })
#define vasprintf(strp, fmt, ap) ({ _Pragma("GCC error \"[DooRT] vasprintf is forbidden.\""); (void)(strp); (void)(fmt); (void)(ap); })

#endif

#endif      /* DOORT_CONFIG_H */
