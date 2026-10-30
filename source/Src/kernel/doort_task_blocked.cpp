#include <new>
#include <string.h>

#include "doort_task_blocked.hpp"
#include "doort_global_state.hpp"
#include "doort_memory_manage.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_fault.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"

extern volatile dgState gState;
extern dPort dp;
extern dkScheduler kScheduler;

static uint32_t wakeTickCalcu(unsigned int delayTick) {
    return gState.dgsTickCount + delayTick;                                     //自动回绕
}

static inline bool isFastCompar(uint32_t aWakeTick, uint32_t bWakeTick) {       //任务更快
    return ((uint32_t)(aWakeTick - gState.dgsTickCount) < (uint32_t)(bWakeTick - gState.dgsTickCount )) ? 1 : 0;
}

static inline bool isTickExpired(uint32_t AwakeTick, uint32_t currentTick) {    //任务过期
    return (int32_t)(AwakeTick - currentTick) <= 0;
}

void dTaskBlockedManager::dtbInsertBlockTask(int index, uint32_t Tick) {
    dtbBlockedList[index].AwakeTick = wakeTickCalcu(Tick);                      //拿到就绪时间
    dtbBlockedList[index].BlockedTask = kScheduler.dksGetCurrentTask();         //拿到当前任务
}

void dTaskBlockedManager::dtbInitBlockedList(void) {
    dtbBlockedList = dmKernelPlacementNewArray<BlockedSeqList>(8,gState.dgsTaskNum);
}

/**
 * @brief   按唤醒Tick插入阻塞任务（有序数组）
 * @param   delayTick   延迟滴答数
 * @note    平均时间复杂度 T = log₂(n) + n/4 + O(1), 主项系数 1/4,
                  通过前后比较搬运长短使得总能搬运到最短边, 将n/2优化到平均n/4
                  同时该算法相较于最小堆和时间轮等有更优的cache友好性和空间复杂度
                  在任务数量少于64时因为无须跳转和cache友好, 实际时间复杂度表现优于最小堆
 */
void dTaskBlockedManager::dtbTaskBlocked(uint32_t delayTick) {
    if(dtbBlockTaskCount == 0) {    //如果没有任务在阻塞
        dtbInsertBlockTask(0, delayTick);
        dtbTailPointer++;       //插入一个, 尾后移
        dtbBlockTaskCount++;    //任务数量增加
        return;
    }

    if(dtbBlockTaskCount == 1) {    //如果当前有的那个任务更快到期
        if(isFastCompar(dtbBlockedList[dtbHeadPointer].AwakeTick, wakeTickCalcu(delayTick))) {
            if(dtbTailPointer < gState.dgsTaskNum) {        //如果尾指针未到任务数量(末尾有空位)
                int index = dtbHeadPointer;                 //拿到当前头指针位置
                index++;                                    //位置后移一位
                dtbInsertBlockTask(index, delayTick);       //插入
                dtbTailPointer++;                           //尾插入一个, 尾后移
            }
            else {                                                          //如果尾指针值等于任务数量(末尾已占满), 头指针指向最后一个格子
                int index = dtbHeadPointer;                                 //拿到当前头指针位置
                index--;                                                    //位置前移
                dtbBlockedList[index] = dtbBlockedList[dtbHeadPointer];     //把原有的一个前移(因为它更快)
                dtbInsertBlockTask(dtbHeadPointer, delayTick);              //在原本的位置插入当前任务
                dtbHeadPointer--;                                           //头指针前移
            }
        }
        else {                                                          //如果新任务更快到期
            if(!dtbHeadPointer) {                                       //如果头是首位
                dtbBlockedList[1] = dtbBlockedList[dtbHeadPointer];     //头节点后移一位
                dtbInsertBlockTask(dtbHeadPointer, delayTick);          //后移后插入当前任务
                dtbTailPointer++;                                       //尾指针后移到空位
            }
            else {                                                  //如果头不是首位
                dtbHeadPointer--;                                   //头指针前移一位
                dtbInsertBlockTask(dtbHeadPointer, delayTick);      //插入
            }
        }
        dtbBlockTaskCount++;    //任务数量增加
        return;
    }

    if(dtbTailPointer == gState.dgsTaskNum) {       //二分前先检查尾空间是否为0, 满了则整体搬到队头, 减少后续长待机任务新建时的memmove开销
        memmove(&dtbBlockedList[0], &dtbBlockedList[dtbHeadPointer], dtbBlockTaskCount * sizeof(dtbBlockedList[0]));
        dtbHeadPointer = 0;
        dtbTailPointer = dtbBlockTaskCount;
    }

    // 二分查找 左闭右开区间 [head, tail)
    uint16_t head = dtbHeadPointer;
    uint16_t tail = dtbTailPointer;     // 开区间，指向第一个空位
    uint32_t targetVal = wakeTickCalcu(delayTick);

    while (head < tail) {
        uint16_t mid = head + (tail - head) / 2;
        if (isFastCompar(targetVal, dtbBlockedList[mid].AwakeTick)) {
            tail = mid;     // target 比 mid 快，插入点在左半部分（包含mid）
        } else {
            head = mid + 1;     // target 不比 mid 快（更慢或相等），插入点在右半部分
        }
    }
    // 此时 head 即为插入位置（0 <= head <= dtbTailPointer）
    uint16_t midd = head;

    // 否则，有效位置，取该位置的 AwakeTick 用于后续比较
    // midd==tail 时(新任务最慢)不读 dtbBlockedList[tail](数组外), val 置0不参与分支
    uint32_t val = (midd < dtbTailPointer) ? dtbBlockedList[midd].AwakeTick : 0;

    if (midd == dtbTailPointer) {
        dtbInsertBlockTask(midd, delayTick);
        dtbTailPointer++;
        dtbBlockTaskCount++;
        return;
    }

    if(dtbHeadPointer == 0) {                   //如果队列头已无空位
        if(isFastCompar(targetVal, val)) {      //如果它比当前midd快
            memmove(&dtbBlockedList[midd + 1], &dtbBlockedList[midd], (dtbTailPointer - midd) * sizeof(dtbBlockedList[0]));
            dtbTailPointer++;                       //尾部指针后移
            dtbInsertBlockTask(midd, delayTick);    //在开出的那个格子插入当前任务
        }
        else {
            memmove(&dtbBlockedList[midd + 2], &dtbBlockedList[midd + 1], (dtbTailPointer - midd - 1) * sizeof(dtbBlockedList[0]));
            dtbTailPointer++;                           //尾部指针后移
            dtbInsertBlockTask(midd + 1, delayTick);    //在开出的那个格子插入当前任务
        }
        dtbBlockTaskCount++;
        return;
    }

    head = dtbHeadPointer;          //重新拿到队列有值头
    tail = dtbTailPointer - 1;      //重新拿到队列有值尾而非空尾
    if(isFastCompar(targetVal, val)) {
        if(tail - midd > midd - head) {             //如果往前搬运内容少
            uint16_t oldHead = dtbHeadPointer;      //保存原头
            memmove(&dtbBlockedList[oldHead - 1], &dtbBlockedList[oldHead], (midd - oldHead) * sizeof(dtbBlockedList[0]));
            //把不包括midd的前面位置挪前面一格
            dtbHeadPointer--;                           //逻辑头前移
            dtbInsertBlockTask(midd - 1, delayTick);    //在开出的那个格子插入当前任务
        }
        else {
            memmove(&dtbBlockedList[midd + 1], &dtbBlockedList[midd], (dtbTailPointer - midd) * sizeof(dtbBlockedList[0]));
            dtbTailPointer++;                       //尾部指针后移
            dtbInsertBlockTask(midd, delayTick);    //在开出的那个格子插入当前任务

        }
        dtbBlockTaskCount++;
        return;
    }
    else {
        if(tail - midd > midd - head) {
            uint16_t oldHead = dtbHeadPointer;      //保存原头
            memmove(&dtbBlockedList[oldHead - 1], &dtbBlockedList[oldHead], (midd - oldHead + 1) * sizeof(dtbBlockedList[0]));
            //把包括midd的前面位置挪前面一格
            dtbHeadPointer--;                       //逻辑头前移
            dtbInsertBlockTask(midd, delayTick);    //在开出的那个格子插入当前任务
        }
        else {
            memmove(&dtbBlockedList[midd + 2], &dtbBlockedList[midd + 1], (dtbTailPointer - midd - 1) * sizeof(dtbBlockedList[0]));
            dtbTailPointer++;                           //尾部指针后移
            dtbInsertBlockTask(midd + 1, delayTick);    //在开出的那个格子插入当前任务
        }
        dtbBlockTaskCount++;
        return;
    }
}

void dTaskBlockedManager::dtbTaskUnblock(void) {
    DISABLE_IRQ();      //先关中断再检查, 防止检查与操作之间的窗口被其他中断插入

    do {    //解除全部在同1ms时刻到期的任务, 在SysTick中已限制必定有一个任务到期, 这里使用doWhile可以优化一次判断
        dTaskControlBlock *temp = dtbBlockedList[dtbHeadPointer].BlockedTask;
        temp->dtStatusCode = DOORT_TASK_STATUS_READY;

        kScheduler.dksModifyCountAndMap(temp, 1);

        if(temp->dtPriority < kScheduler.dksCurrentRunningPriority) kScheduler.dksCurrentRunningPriority = temp->dtPriority;    //更新当前正在运行的优先级
        dtbHeadPointer++;                                                                                                       //头指针后移, 因为阻塞任务被摘除
        dtbBlockTaskCount--;                                                                                                    //计数器减少

    } while (dtbBlockTaskCount > 0 && isTickExpired(dtbBlockedList[dtbHeadPointer].AwakeTick, gState.dgsTickCount));    //如果头部任务已到期

    if (dtbBlockTaskCount == 0) {       //如果所有任务全挂起就重置指针
        dtbHeadPointer = 0;
        dtbTailPointer = 0;
    }
    ENABLE_IRQ();
    return;
}

void dTaskBlockedManager::dtbTaskCancel(dTaskControlBlock *tcb) {
    if (tcb == nullptr || dtbBlockTaskCount == 0) return;       //无登记直接返回

    //在 [head, head+count) 逻辑区间内查找该任务
    for (uint16_t i = 0; i < dtbBlockTaskCount; i++) {
        uint16_t idx = (uint16_t)((dtbHeadPointer + i) % gState.dgsTaskNum);
        if (dtbBlockedList[idx].BlockedTask == tcb) {
            //把后面的条目向前搬, 保持按唤醒Tick有序
            for (uint16_t j = i; j + 1 < dtbBlockTaskCount; j++) {
                uint16_t src = (uint16_t)((dtbHeadPointer + j + 1) % gState.dgsTaskNum);
                uint16_t dst = (uint16_t)((dtbHeadPointer + j) % gState.dgsTaskNum);
                dtbBlockedList[dst] = dtbBlockedList[src];
            }
            dtbBlockTaskCount--;
            if (dtbBlockTaskCount == 0) {       //清空后复位指针
                dtbHeadPointer = 0;
                dtbTailPointer = 0;
            } else {
                dtbTailPointer = (uint16_t)((dtbTailPointer + gState.dgsTaskNum - 1) % gState.dgsTaskNum);
            }
            return;
        }
    }
}

BlockedSeqList* dTaskBlockedManager::dtbGetSeqList(void) {
    return dtbBlockedList;
}


bool dTaskBlockedManager::dtbIsTickExpired(uint32_t AwakeTick, uint32_t currentTick) {
    return isTickExpired(AwakeTick, currentTick);
}

bool dTaskBlockedManager::dtbTaskDue() {
    if(dtbBlockTaskCount && dtbIsTickExpired(dtbBlockedList[dtbHeadPointer].AwakeTick, gState.dgsTickCount)) return 1;
    else return 0;
}
//将当前最快到期的任务到期时间记录到gState中, 使检查函数只有在有任务到期时才检查, 减少压栈开销
