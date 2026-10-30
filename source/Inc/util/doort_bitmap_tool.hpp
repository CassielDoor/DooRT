#ifndef DOORT_BITMAP_TOOL_HPP
#define  DOORT_BITMAP_TOOL_HPP

#include <stdint.h>

void dbtBitSet(void *startAddr, uint32_t index, bool bit);
bool dbtBitGet(void *startAddr, uint32_t index);
void dbtBitClean(void *startAddr, uint32_t byteSize);
void dbtBitSetRange(void *startAddr, uint32_t startIndex, uint32_t length, bool bit);
uint32_t dbtCountZeroBits(const void *startAddr, uint32_t totalBits);
uint32_t dbtFindFirstZeroIndex(void *startAddr, uint32_t totalBits);
uint32_t dbtFreeBlockIndex(void *startAddr, uint32_t totalBits, uint32_t blockLength);

#endif      /*  DOORT_BITMAP_TOOL_HPP */