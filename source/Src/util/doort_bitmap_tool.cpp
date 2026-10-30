#include <stdint.h>

#include "doort_bitmap_tool.hpp"

// 所有内存至少4字节对齐, 所有操作都按单字来走
void dbtBitSet(void *startAddr, uint32_t index, bool bit) {
    uint32_t *word = (uint32_t *)startAddr + (index >> 5);      //找到这个bit在第几个图内
    if(bit) *word |= 1u << (index & 31);                        //与0b11111(&31)来保留后五位, 找到这个bit在第几个位内, 然后生成独热码进行或运算
    else *word &= ~(1u << (index & 31));                        //生成独冷码后进行与运算
}

bool dbtBitGet(void *startAddr, uint32_t index) {
    uint32_t *word = (uint32_t *)startAddr + (index >> 5);      //找到这个bit在第几个图内
    return (*word >> (index & 31)) & 1;                         //返回该位是0还是1
}

void dbtBitClean(void *startAddr, uint32_t byteSize) {
    uint32_t *word = (uint32_t *)startAddr;     //拿到起始位置
    while(byteSize >= 4) {                      //如果剩余字节长度大于4
        *word++ = 0;                            //按字清除
        byteSize -= 4;                          //剩余字节长度减少
    }
    uint8_t *byte = (uint8_t *)word;    //强转为单字节长度
    while(byteSize--) *byte++ = 0;      //单字清除
}

void dbtBitSetRange(void *startAddr, uint32_t startIndex, uint32_t length, bool bit) {
    uint32_t *word = (uint32_t *)startAddr + (startIndex >> 5);     //首地址
    uint32_t offset = startIndex & 31;                              //单图偏移
    uint32_t value = bit ? 0xFFFFFFFFu : 0;                         //全1或全0

    if(offset) {                                            //若偏移非0, 则首字不完整
        uint32_t headLen = 32 - offset;                     //偏移到末尾还剩多少位
        if(headLen > length) headLen = length;              //若需处理的长度小于剩余位数则只处理长度
        uint32_t mask = ((1u << headLen) - 1) << offset;    //生成位掩码
        if(bit) *word |= mask;                              //置1
        else *word &= ~mask;                                //置0
        word++;                                             //首字处理完成后指针后移
        length -= headLen;                                  //长度减去头长
        if(!length) return;                                 //如果已经处理完成则结束
    }
    while(length >= 32) {       //整字写
        *word++ = value;
        length -= 32;
    }
    if(length) {    //尾字是部分覆盖
        uint32_t mask = (1u << length) - 1;
        if(bit) *word |= mask;
        else *word &= ~mask;
    }
}

uint32_t dbtCountZeroBits(const void *startAddr, uint32_t totalBits) {
    const uint32_t *word = (const uint32_t *)startAddr;
    uint32_t totalOnes = 0;
    uint32_t fullWords = totalBits >> 5;        // 完整32位字的个数
    uint32_t remainBits = totalBits & 31;       // 剩余不足32位的位数

    // 主循环: 每次处理两个32位字, 减少循环开销
    uint32_t i = 0;
    while (i + 1 < fullWords) {
        totalOnes += __builtin_popcount(word[i]);
        totalOnes += __builtin_popcount(word[i + 1]);
        i += 2;
    }

    if (i < fullWords)  totalOnes += __builtin_popcount(word[i]);       //如果有奇数个完整字, 处理最后一个

    if (remainBits) {       //处理末尾不足32位的部分
        uint32_t mask = (1u << remainBits) - 1;
        totalOnes += __builtin_popcount(word[fullWords] & mask);
    }

    return totalBits - totalOnes;       //总位数减1的个数 = 0的个数
}


uint32_t dbtFindFirstZeroIndex(void *startAddr, uint32_t totalBits) {       //第一0位索引
    const uint32_t *word = (const uint32_t *)startAddr;                     //地址转指针
    uint32_t pos = 0;                                                       //记录已扫描位数
    while(pos + 32 <= totalBits) {                                          //剩余处理量大于一个图
        uint32_t w = *word++;
        if(w != 0xFFFFFFFFu) return pos + __builtin_ctz(~w);    //已找到非零位
        pos += 32;                                              //位数增加一图
    }
    if(pos < totalBits) {                                       //尾块处理
        uint32_t tail = totalBits - pos;                        //总数减去已处理位数
        uint32_t w = *word | ~((1u << tail) - 1);               //将totalBits之外的位强制为1, 防止填充位被当作空位
        if(w != 0xFFFFFFFFu) return pos + __builtin_ctz(~w);    //已找到非零位
    }
    return totalBits;       //如果全没找到那就是在最末尾
}

uint32_t dbtFreeBlockIndex(void *startAddr, uint32_t totalBits, uint32_t blockLength) {
    if (startAddr == nullptr) return totalBits;

    if (blockLength == 0 || blockLength > totalBits) return totalBits;
    if (blockLength == 1) return dbtFindFirstZeroIndex(startAddr, totalBits);

    const uint32_t *word = (const uint32_t *)startAddr;
    uint32_t pos = 0;
    uint32_t run = 0;

    while (pos < totalBits) {
        uint32_t bits = totalBits - pos;
        if (bits > 32) bits = 32;

        uint32_t w = *word++;
        if (bits < 32) w &= (1u << bits) - 1;

        if (w == 0) {       //全0
            if (run + bits >= blockLength) return pos - run;
            run += bits;
            pos += bits;
            continue;
        }
        if (w == 0xFFFFFFFFu) {     //全1
            run = 0;
            pos += bits;
            continue;
        }

        // 混合字：先判断 0 的总数是否可能满足
        uint32_t zero_total = bits - __builtin_popcount(w);
        if (run + zero_total < blockLength) {
            // 本字连同结转仍不足。可结转到下一字的只能是"末尾连续零"——
            // 置位之前的零已被打断, 不能计入 run (否则会返回指向已用块的假空闲位)
            uint32_t msb = 31 - __builtin_clz(w);       // 本字内最高置位位位置
            run = bits - msb - 1;
            pos += bits;
            continue;
        }

        //ctz加速扫描
        uint32_t offset = 0;
        uint32_t temp_w = w;
        while (temp_w) {
            uint32_t zero_cnt = __builtin_ctz(temp_w);
            if (run + zero_cnt >= blockLength) {
                return pos + offset - run;
            }
            run = 0;
            uint32_t step = zero_cnt + 1;
            offset += step;
            if (step == 32) temp_w = 0;
            else temp_w >>= step;
        }

        uint32_t remain_zero = bits - offset;
        if (run + remain_zero >= blockLength) {
            return pos + offset - run;
        }
        run += remain_zero;
        pos += bits;
    }

    return totalBits;
}