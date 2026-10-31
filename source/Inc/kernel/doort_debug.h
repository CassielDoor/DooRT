/**
 * @file    doort_debug.h
 * @brief   DooRT 调试锚点与目录布局(内核私有, 用户代码不需要 include)。
 *
 * 设计约束:
 *  - 只吃 <stdint.h> 和 doort.h, 不含任何芯片/厂商头文件, 不依赖链接脚本;
 *  - 锚点是一个普通全局变量, 地址不写死。上位机靠"在 SRAM 里按内容扫描
 *    magic + version"发现它, 所以既不需要 ELF, 也不需要预先约定地址;
 *  - 锚点在目录填完之后才写, 上位机看到有效锚点就说明目录已经完整;
 *  - 目录放在 dkScheduler::dksStartScheduler() 的局部区(主栈/MSP)上。该函数
 *    永不返回, 调度器启动后中断只会从更低的 SP 往下压栈, 所以这块内存不会被
 *    复用, 也不需要中断钩子/周期刷新;
 *  - 上位机读锚点 -> 读目录 -> 按目录里的地址读目标内存, 全程不发 halt 命令;
 *  - 布局固定且显式定宽, 上位机按同样布局解析。改布局必须升 version。
 */
#ifndef DOORT_DEBUG_H
#define DOORT_DEBUG_H

#include <stdint.h>

#include "doort.h"

#ifdef __cplusplus
extern "C" {
#endif

// 0xCAC5D88B: 由十六进制字符循环映射生成(逐位 C,A,S,L,D,O,O,R), 锚点和目录共用
// A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
// A B C D E F 0 1 2 3 4 5 6 7 8 9 A B C D E F 0 1 2 3

#define DOORT_DEBUG_MAGIC 0xCAC5D88Bu

// 调试契约版本跟随 DooRT 版本(见 doort.h): 0.0.2 -> 0x0002 (主<<8 | 次<<4 | 修订)
#define DOORT_DEBUG_VERSION                                                  \
    ((uint16_t)(((DOORT_VERSION_MAJOR & 0xFu) << 8) |                        \
                ((DOORT_VERSION_MINOR & 0xFu) << 4) |                        \
                (DOORT_VERSION_PATCH & 0xFu)))

/**
 * 锚点: 普通全局变量, 位置由链接器决定(不写死地址)。
 * 上位机在 SRAM 里扫描 magic 找到它; magic 为 0 表示调度器还没走到注册点。
 * 字段统一 dd(doortDbg) 前缀, 避免和普通名字撞车。
 */
typedef struct {
    uint32_t ddMagic;         // DOORT_DEBUG_MAGIC
    uint32_t ddLayoutVersion; // DOORT_DEBUG_VERSION
    uint32_t ddDirectoryAddr; // 指向下面的调试目录
    uint32_t ddDirectorySize; // sizeof(DoortDebugSnapshot)
} DoortDebugAnchor;

/**
 * 调试目录: 只有地址, 没有快照值。
 * 带 SlotAddr 后缀的是"保存该活数据的变量的地址", 上位机读该地址 4 字节得到当前值:
 *   ddHeapTopSlotAddr     -> uint8_t*  当前堆顶(已用字节 = ddHeapTop - ddHeapBlockBase)
 *   ddTickSlotAddr        -> uint32_t  内核节拍
 *   ddCurrentTaskSlotAddr -> TCB*      当前任务
 */
typedef struct {
    uint32_t ddMagic;               // DOORT_DEBUG_MAGIC
    uint16_t ddLayoutVersion;       // DOORT_DEBUG_VERSION
    uint16_t ddSnapshotSize;        // sizeof(DoortDebugSnapshot)

    uint32_t ddKernelBlockBase;     // 内核块起始地址
    uint32_t ddKernelBlockEnd;      // 内核块结束地址(不含)

    uint32_t ddHeapBlockBase;       // 堆块起始地址
    uint32_t ddHeapBlockEnd;        // 堆块结束地址(不含)

    uint32_t ddHeapTopSlotAddr;     // 堆顶指针变量的地址
    uint32_t ddTickSlotAddr;        // 内核节拍变量的地址
    uint32_t ddCurrentTaskSlotAddr; // 当前任务指针变量的地址

    /* ---- v0.0.3: 堆元数据表位置(只给地址, 值由上位机自己读) ----
     * 堆是没有块头的 bump 分配器, 单看内存内容分不出对象边界, 所以上位机
     * 必须顺着地址索引 + 句柄表才能画出"哪段被占、哪段是空洞"。 */
    uint32_t ddAddressIndexAddr;      // dAddressIndex* 这个变量的地址(槽)
    uint32_t ddAddressIndexCountAddr; // uint32_t*, 当前索引条目数(含空洞槽)
    uint32_t ddObjectHandleAddr;      // dObjectHandle* 这个变量的地址(槽)
    uint32_t ddObjectNum;             // 句柄容量 = DOORT_HEAP_OBJECT_NUM
    uint32_t ddEntrySize;             // 两个表的单条大小(都是 8 字节)
    uint32_t ddCompactWriteSlotAddr;  // uint8_t**, 紧缩写指针(紧缩进度)
    uint32_t ddCompactingSlotAddr;    // bool*, 是否正在紧缩
    uint32_t ddFragmentSlotAddr;      // bool*, 是否有碎片
} DoortDebugSnapshot;

// 锚点对象(位置由链接器决定, 上位机不按符号名查)
extern DoortDebugAnchor DoortDebug_Anchor;

/**
 * 由 dkScheduler::dksStartScheduler() 在启动首任务前调用一次。
 * 先填目录, 最后写锚点; 之后不再改。
 */
void dDebugPublishSnapshot(DoortDebugSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif // DOORT_DEBUG_H
