#include <stdint.h>
#include <string.h>

#include "doort_port.hpp"
#include "doort_memory_manage.hpp"
#include "doort_memory_handle.hpp"
#include "doort_global_state.hpp"
#include "doort_fault.hpp"
#include "doort_bitmap_tool.hpp"

extern volatile dgState gState;
extern dMemoryManager dmm;

void dMemoryManager::dmInitHandleBitMap(void) {
    uint32_t HandleBitMapCount = ((DOORT_HEAP_OBJECT_NUM + 31) >> 5);               //计算句柄位图字数
    dmHandleBitMap = dmKernelPlacementNewArray<uint32_t>(4, HandleBitMapCount);     //分配句柄位图内存
    dbtBitClean(dmHandleBitMap, HandleBitMapCount * 4);                             //清空句柄位图
}

void dMemoryManager::dmInitObjectList(void) {
    dmObjectHandleHead = dmKernelPlacementNewArray<dObjectHandle>(4, DOORT_HEAP_OBJECT_NUM);    //分配句柄表
}

void dMemoryManager::dmInitAddressIndex(void) {
    dmAddressIndex = dmKernelPlacementNewArray<dAddressIndex>(4, DOORT_HEAP_OBJECT_NUM);    //分配地址索引数组
    dmAddressIndexCount = 0;                                                                //初始无有效条目
    for (uint32_t i = 0; i < DOORT_HEAP_OBJECT_NUM; i++) {                                  //初始化所有条目为无效
        dmAddressIndex[i].handleId = UINT16_MAX;                                            //无效标记
        dmAddressIndex[i].addr = nullptr;                                                   //空地址
    }
}

void dMemoryManager::dmInitHeap(void) {
    dmInitObjectList();
    dmInitHandleBitMap();
    dmInitAddressIndex();

    dmHeapBase = (uint8_t*)gState.dgsHerpAddr;      //堆基址
    dmHeapEnd  = (uint8_t*)gState.dgsHerpEnd;       //堆尾
    dmHeapTop  = dmHeapBase;                        //初始堆顶等于基址

    dmCompacting = false;
    dmFragment = false;
    dmCompactWrite = nullptr;
    dmCompactIndex = 0;

    uint32_t heapSize = (uint32_t)(dmHeapEnd - dmHeapBase);     //堆总字节数
    uint32_t heapMB = heapSize / (1024 * 1024);
    uint32_t heapKB = (heapSize % (1024 * 1024)) / 1024;
    dLog("[info]: Heap initialized. Base=%p, End=%p, Size=%luMB %luKB\n", dmHeapBase, dmHeapEnd, (unsigned long)heapMB, (unsigned long)heapKB);

}

void dMemoryManager::mtCompactionInit(void) {
    if (dmAddressIndexCount == 0) {     //无对象
        dmCompacting = false;           //停止压缩
        dmFragment = false;             //清碎片标志
        return;
    }

    uint8_t *expected = dmHeapBase;                                 //期望的连续地址
    for (uint32_t i = 0; i < dmAddressIndexCount; i++) {            //遍历索引数组
        if (dmAddressIndex[i].handleId == UINT16_MAX) continue;     //无效条目
             // 跳过

        uint8_t *objAddr = (uint8_t*)dmAddressIndex[i].addr;    //对象地址
        if (objAddr > expected) {                               //有碎片
            dmCompacting = true;
            dmCompactWrite = (uint8_t*)(((uintptr_t)expected + 7) & ~(uintptr_t)7);     //搬移目标必须8字节对齐
            dmCompactIndex = i;                                                         //记录游标
            return;
        }

        uint32_t size = dmObjectHandleHead[dmAddressIndex[i].handleId].dohObjectSize;       // 对象大小
        uint32_t alignedSize = (size + 7) & ~0x7;                                           //对齐后大小
        expected += alignedSize;                                                            //更新期望地址
    }

    dmCompacting = false;       //无空洞
    dmFragment = false;         //清碎片标志
    dmHeapTop = expected;       //堆顶更新
}

void dMemoryManager::mtCompactIndexArray(void) {
    uint32_t write = 0;
    uint32_t cursorValid = 0;       // 原游标 dmCompactIndex 之前的有效条目数 = 已被压缩器处理过的条目数
    for (uint32_t read = 0; read < dmAddressIndexCount; read++) {
        if (dmAddressIndex[read].handleId != UINT16_MAX) {
            if (read < dmCompactIndex) cursorValid++;
            dmAddressIndex[write++] = dmAddressIndex[read];
        }
    }
    dmAddressIndexCount = write;
    // 索引整体前移会打乱压缩器游标: 未处理区域起点 = "原游标前的有效条目数"(前移后仍保序)。
    // 若不同步, 压缩器会跳过/重复搬移块, 或提前把 dmHeapTop 设低, 造成活对象重叠。
    if (dmCompacting) dmCompactIndex = cursorValid;
}

bool dMemoryManager::dmHeapUsageHigh(void) {
    return (uint32_t)(dmHeapTop - dmHeapBase) > (uint32_t)(dmHeapEnd - dmHeapBase) / 2;
}

void dMemoryManager::mtCompactionStep(void) {
    if (!dmCompacting) return;      //未压缩直接返回

    while (dmCompactIndex < dmAddressIndexCount && dmAddressIndex[dmCompactIndex].handleId == UINT16_MAX)
        dmCompactIndex++;       //跳过无效条目

    if (dmCompactIndex >= dmAddressIndexCount) {    //所有对象处理完毕
        dmHeapTop = dmCompactWrite;                 //更新堆顶
        dmCompacting = false;                       //停止压缩
        dmFragment = false;                         //清碎片标志
        mtCompactIndexArray();                      //压缩索引数组
        return;
    }

    dAddressIndex &entry = dmAddressIndex[dmCompactIndex];      //当前条目
    uint16_t id = entry.handleId;                               //拿到句柄ID
    void *oldAddr = entry.addr;                                 //拿到旧地址
    uint32_t size = dmObjectHandleHead[id].dohObjectSize;       //对象大小
    uint32_t alignedSize = (size + 7) & ~0x7;                   //对齐后大小

    if ((uint8_t*)oldAddr != dmCompactWrite) {                      //需要搬移
        memmove(dmCompactWrite, oldAddr, size);                     //搬移对象
        dmObjectHandleHead[id].dohObjectAddr = dmCompactWrite;      //更新句柄表
        entry.addr = dmCompactWrite;                                //更新索引表
    }

    dmCompactWrite += alignedSize;      //写指针后移
    dmCompactIndex++;                   //游标后移
}

void* dMemoryManager::mtHeapAllocAddr(size_t size) {
    uint32_t alignedSize = (size + 7) & ~0x7;       //对齐后大小

    if ((uintptr_t)dmHeapTop + alignedSize > (uintptr_t)dmHeapEnd) {                                                // 空间不足
        if (!dmFragment) return nullptr;                                                                            //无碎片以供开辟返回空指针
        if (!dmCompacting) mtCompactionInit();                                                                      //未在压缩则初始化压缩
        while (dmCompacting && ((uintptr_t)dmHeapTop + alignedSize > (uintptr_t)dmHeapEnd)) mtCompactionStep();     //步进压缩
        if ((uintptr_t)dmHeapTop + alignedSize > (uintptr_t)dmHeapEnd) return nullptr;                              // 压缩后仍不够
    }

    void* addr = dmHeapTop;         //分配地址
    dmHeapTop += alignedSize;       //堆顶后移
    return addr;
}

uint16_t dMemoryManager::dmAllocHandle(size_t size) {
    DISABLE_IRQ();                                                                          //分配整体原子: 防止中途被打断导致索引计数与槽位不一致
    uint32_t handleIndex = dbtFindFirstZeroIndex(dmHandleBitMap, DOORT_HEAP_OBJECT_NUM);    // 找空闲句柄
    if (handleIndex >= DOORT_HEAP_OBJECT_NUM) {                                             //无空闲句柄
        ENABLE_IRQ();
        return UINT16_MAX;      //失败返回最大值
    }

    void* addr = mtHeapAllocAddr(size);     //从堆分配内存
    if (!addr) {                            //分配失败
        ENABLE_IRQ();
        return UINT16_MAX;      //失败
    }

    dmObjectHandleHead[handleIndex].dohObjectAddr = addr;       //写入句柄表
    dmObjectHandleHead[handleIndex].dohObjectSize = size;       //记录大小

    dbtBitSet(dmHandleBitMap, handleIndex, true);       //标记句柄占用

    //新对象一律追加到索引末尾, 保持索引按地址有序(新对象地址在堆顶, 一定是最高地址)
    //若复用第一个空闲槽, 新对象会插到中间位置破坏地址有序, 紧缩时会把对象搬进存活块
    uint32_t slot = 0;
    if (dmAddressIndexCount >= DOORT_HEAP_OBJECT_NUM) {
        mtCompactIndexArray();      //索引满时先压缩空洞恢复稠密
        if (dmAddressIndexCount >= DOORT_HEAP_OBJECT_NUM) {
            ENABLE_IRQ();
            return UINT16_MAX;      //确实满了
        }
    }
    slot = dmAddressIndexCount;
    dmAddressIndex[slot].addr = addr;               //写入地址
    dmAddressIndex[slot].handleId = handleIndex;    //写入句柄 ID
    if (slot >= dmAddressIndexCount) {              //更新计数
        dmAddressIndexCount = slot + 1;
    }

    //堆占用过50%线且已有碎片时才触发紧缩(判断点只在alloc)
    if (dmHeapUsageHigh() && dmFragment && !dmCompacting)
        mtCompactionInit();

    ENABLE_IRQ();
    return (uint16_t)handleIndex;       //返回句柄 ID
}

void dMemoryManager::dmFreeHandle(uint16_t id) {
    if (id >= DOORT_HEAP_OBJECT_NUM) return;        //无效句柄
    if (!dbtBitGet(dmHandleBitMap, id)) return;     //句柄未占用

    DISABLE_IRQ();                                              //释放整体原子: 防止中途被打断读到/写入不一致的地址
    void* addr = dmObjectHandleHead[id].dohObjectAddr;          //取地址
    uint32_t size = dmObjectHandleHead[id].dohObjectSize;       //取大小

    for (uint32_t i = 0; i < DOORT_HEAP_OBJECT_NUM; i++) {      //查找索引条目
        if (dmAddressIndex[i].handleId == id) {                 //找到
            dmAddressIndex[i].handleId = UINT16_MAX;            //标记无效
            dmAddressIndex[i].addr = nullptr;                   //清空地址
            break;
        }
    }

    dbtBitSet(dmHandleBitMap, id, false);               //清句柄位图
    dmObjectHandleHead[id].dohObjectAddr = nullptr;     //清句柄表
    dmObjectHandleHead[id].dohObjectSize = 0;

    uint32_t alignedSize = (size + 7) & ~0x7;                       //对齐大小
    if ((uintptr_t)addr + alignedSize == (uintptr_t)dmHeapTop) {    //释放的是堆顶对象
        dmHeapTop = (uint8_t*)addr;                                 //堆顶回退
        if (dmAddressIndexCount > 0 && dmAddressIndex[dmAddressIndexCount - 1].handleId == UINT16_MAX) {
            dmAddressIndexCount--;      //减少索引计数
        }
    }
    else {                      //非堆顶释放
        dmFragment = true;      //只记录碎片, 触发判断在alloc过50%时进行
    }
    ENABLE_IRQ();
}

void* dMemoryManager::dmDereferenceHandle(uint16_t id) {
    if (id >= DOORT_HEAP_OBJECT_NUM) return nullptr;        // 无效句柄
    if (!dbtBitGet(dmHandleBitMap, id)) return nullptr;     // 句柄未占用
    return dmObjectHandleHead[id].dohObjectAddr;            // 返回地址
}

uint8_t dMemoryManager::dmGetUsagePercent(void) {
    DISABLE_IRQ();
    uint32_t total = (uint32_t)(dmHeapEnd - dmHeapBase);
    uint32_t used = (uint32_t)(dmHeapTop - dmHeapBase);
    ENABLE_IRQ();
    if (total == 0) return 0;
    return (uint8_t)((uint64_t)used * 100 / total);
}

uint8_t dMemoryManager::dmGetFragPercent(void) {
    DISABLE_IRQ();
    uint32_t used = (uint32_t)(dmHeapTop - dmHeapBase);
    uint32_t live = 0;
    for (uint32_t i = 0; i < dmAddressIndexCount; i++) {
        if (dmAddressIndex[i].handleId == UINT16_MAX) continue;
        uint32_t size = dmObjectHandleHead[dmAddressIndex[i].handleId].dohObjectSize;
        live += (size + 7) & ~0x7;
    }
    ENABLE_IRQ();
    if (used == 0) return 0;
    uint32_t frag = used > live ? used - live : 0;
    return (uint8_t)((uint64_t)frag * 100 / used);
}

void dMemoryManager::dmRunCompaction(void) {
    if (!dmFragment) return;    //无碎片不需要压缩
    DISABLE_IRQ();              //整趟原子: 搬移+句柄更新+索引压缩不被打断
    if (!dmCompacting) {        //未在压缩
        mtCompactionInit();     //初始化压缩
    }
    while (dmCompacting) {      //循环压缩直到完成
        mtCompactionStep();
    }
    ENABLE_IRQ();
}

void dMemoryManager::dmCompactionStep(void) {
    if (dmCompacting) {
        DISABLE_IRQ();          //一步搬移原子: memmove写目标区时不能让其他任务经句柄读到一半的数据
        mtCompactionStep();     // 只执行一步
        ENABLE_IRQ();
    }
}

//待修改, 这三个函数可以脱壳, 在上层直接调用dmm
void* dmhHandleDereference(uint16_t id) {
    return dmm.dmDereferenceHandle(id);
}

uint16_t dmhAlloc(size_t size) {
    return dmm.dmAllocHandle(size);
}

void dmhFree(uint16_t id) {
    dmm.dmFreeHandle(id);
}