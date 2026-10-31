#ifndef DOORT_MEMORY_MANAGE_HPP
#define DOORT_MEMORY_MANAGE_HPP

#include <utility>
#include <new>

#include "doort_fault.hpp"
#include "doort_global_state.hpp"

extern volatile dgState gState;

inline uint32_t dmByteAlignment_8(uint32_t blockSize, uint32_t count) {
    return (((blockSize) * (count)) + 7) & ~0x7;    // 8字节对齐大小
}
inline uint32_t dmByteAlignment_4(uint32_t blockSize, uint32_t count) {
    return (((blockSize) * (count)) + 3) & ~0x3;    // 4字节对齐大小
}
inline uintptr_t dmAddrAlignment_8(uintptr_t addr) {
    return (addr + 7) & ~(uintptr_t)7;      // 8字节地址对齐
}
inline uintptr_t dmAddrAlignment_4(uintptr_t addr) {
    return (addr + 3) & ~(uintptr_t)3;      // 4字节地址对齐
}
inline bool dmAddressSafety(void* addr, void* end, uint32_t size) {
    if ((uintptr_t)addr < 0x20000000) dfMemoryFault_AccessViolation();      // 地址越界
    return size <= ((uintptr_t)end - (uintptr_t)addr) ? 1 : 0;              // 检查空间是否足够
}

static inline uint32_t dmCalcAlignedTotalSize(uint32_t blockSize, uint32_t alignment, uint32_t count) {
    if (alignment == 8) return dmByteAlignment_8(blockSize, count);     // 8字节对齐
    if (alignment == 4) return dmByteAlignment_4(blockSize, count);     // 4字节对齐
    return (blockSize * count + alignment - 1) & ~(alignment - 1);      // 其他对齐
}

static inline uintptr_t dmAlignAddress(uintptr_t addr, uint32_t alignment) {
    if (alignment == 8) return dmAddrAlignment_8(addr);     // 8字节地址对齐
    if (alignment == 4) return dmAddrAlignment_4(addr);     // 4字节地址对齐
    return (addr + alignment - 1) & ~(alignment - 1);       // 其他地址对齐
}

inline void* dmKernelPlacementNew(uint32_t alignment, uint32_t size) {
    uintptr_t aligned = dmAlignAddress((uintptr_t)gState.dgsKernelAddr, alignment);     // 对齐内核地址
    gState.dgsKernelAddr = (void*)aligned;                                              // 更新内核当前地址

    if (!dmAddressSafety(gState.dgsKernelAddr, gState.dgsKernelEnd, size)) {
        dfInitializationFault_DispatchQueueInitializeFailed();      // 内核空间不足
    }

    void* mem = gState.dgsKernelAddr;                                           // 获取分配地址
    gState.dgsKernelAddr = (void*)((uintptr_t)gState.dgsKernelAddr + size);     // 移动内核指针
    return mem;
}

template <typename T>
T* dmKernelPlacementNewArray(uint32_t alignment, uint32_t count) {
    uint32_t totalSize = dmCalcAlignedTotalSize(sizeof(T), alignment, count);       // 总大小
    void* mem = dmKernelPlacementNew(alignment, totalSize);                         // 分配空间
    T* array = new (mem) T[count];                                                  // placement new 构造数组
    return array;
}

template <typename T, typename... Args>
T* dmKernelPlacementNew(uint32_t alignment, Args&&... args) {
    uint32_t totalSize = dmCalcAlignedTotalSize(sizeof(T), alignment, 1);       // 总大小
    void* mem = dmKernelPlacementNew(alignment, totalSize);                     // 分配空间
    return new (mem) T(std::forward<Args>(args)...);                            // placement new 构造对象
}

struct dObjectHandle {
    void* dohObjectAddr;        // 对象地址
    uint32_t dohObjectSize;     // 对象大小
};

struct dAddressIndex {
    void* addr;             // 对象起始地址
    uint16_t handleId;      // 对应句柄 ID，UINT16_MAX 表示无效
};

class dMemoryManager {
private:
    uint32_t* dmHandleBitMap;               // 句柄位图，快速分配句柄 ID
    dObjectHandle* dmObjectHandleHead;      // 句柄表
    dAddressIndex* dmAddressIndex;          // 地址索引数组（按地址排序）
    uint32_t dmAddressIndexCount;           // 当前有效条目数

    uint8_t* dmHeapBase;    // 堆起始地址
    uint8_t* dmHeapTop;     // 当前分配指针
    uint8_t* dmHeapEnd;     // 堆结束地址

    bool dmCompacting;          // 是否正在压缩中
    bool dmFragment;            // 是否有碎片（非堆顶释放导致）
    uint8_t* dmCompactWrite;    // 压缩写入指针
    uint32_t dmCompactIndex;    // 当前在索引数组中的游标

    void dmInitHandleBitMap(void);
    void dmInitObjectList(void);
    void dmInitAddressIndex(void);

    void* mtHeapAllocAddr(size_t size);
    void* mtMemoryConstriction(size_t needSize);
    void mtCompactionInit(void);
    void mtCompactionStep(void);
    void mtCompactIndexArray(void);
    bool dmHeapUsageHigh(void);     //堆占用是否超过50%

public:
    dMemoryManager() :
        dmHandleBitMap(nullptr),
        dmObjectHandleHead(nullptr),
        dmAddressIndex(nullptr),
        dmAddressIndexCount(0),
        dmHeapBase(nullptr),
        dmHeapTop(nullptr),
        dmHeapEnd(nullptr),
        dmCompacting(false),
        dmFragment(false),
        dmCompactWrite(nullptr),
        dmCompactIndex(0)
    {}

    void dmInitHeap(void);

    uint16_t dmAllocHandle(size_t size);
    void dmFreeHandle(uint16_t id);
    void* dmDereferenceHandle(uint16_t id);
    void dmCompactionStep(void);
    bool dmCompactionActive(void) { return dmCompacting; }

    //调试快照: 返回"堆顶指针变量本身"的地址, 上位机读这个地址得到当前堆顶。
    //不改变任何分配状态, 也不参与内核逻辑。
    void** dmGetHeapTopSlot(void) { return reinterpret_cast<void**>(&dmHeapTop); }

    void* dmAlloc(void);
    void dmFree(void);

    void dmRunCompaction(void);
};

extern dMemoryManager dmm;      //全局内存管理器, 定义在 doort.cpp

#endif      /* DOORT_MEMORY_MANAGE_HPP */