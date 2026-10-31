/** ===------------------------------------------------------------------=== **\
 * @file DooRT.h                                                               *
 * @brief DooRT采用Cpp编写，当前文件为所有暴露接口                                  *
 * @details 函数具体描述，整体运行机制请查看README.md                               *
 * @details 如果你不清楚自己正在做什么                                             *
 * @details 请不要修改除doort_config和doort_config_user_type以外的任何文件          *
 * @author CassielDoor                                                         *
 * @date 2026-07                                                               *
\** ===------------------------------------------------------------------=== **/

#ifndef DOORT_H
#define DOORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "doort_config_user_type.h"
#include "doort_config.h"

/* DooRT 版本: 主.次.修订 = 0.0.3 */
#define DOORT_VERSION_MAJOR  0
#define DOORT_VERSION_MINOR  0
#define DOORT_VERSION_PATCH  3
#define DOORT_VERSION_STRING "0.0.3"

typedef void* dTaskRef;
typedef void (*dtFunction)(void* dtParameters);

struct dTaskParams {
    dtFunction dtFunc;
    int8_t dtPriority;
    uint16_t dtStackSize;
    dTaskRef dtHandle;
    void* dtParameters;
};

#ifdef __cplusplus
extern "C" {
#endif

void dInitSystemClock(uint32_t dgsCoreFreq);
void dInitSysTick(uint32_t dgsSysTickUs);

dTaskRef dTaskCreate(struct dTaskParams *taskParams);
/// 调试用: 同 dTaskCreate, 额外给任务挂个名字(仅用于上位机识别)。
/// 名字放在栈底下方的前缀里, 不占栈空间; 一段都不传时行为与 dTaskCreate 相同。
dTaskRef dTaskDebugCreat(struct dTaskParams *taskParams, const char *taskName);
/// 同上, 但创建后初始为挂起态(给紧缩任务这类"按需唤醒"的任务用)
dTaskRef dTaskDebugCreatSuspend(struct dTaskParams *taskParams, const char *taskName);
dTaskRef dTaskCreateSuspend(struct dTaskParams *taskParams);
struct dTaskParams dTaskInherit(dTaskRef * parentTask, dtFunction dtFunc);      //未实现

void dStartScheduler(void);

void dTaskSuspend(void);
void dTaskDelay(uint32_t delayTick);
void dTaskYield(void);
void dTaskTargetSuspend(dTaskRef dtHandle);
void dTaskTargetReady(dTaskRef dtHandle);

void dTaskSetPriority(dTaskRef dtHandle, int8_t newPriority);       //任务优先级调整(同步原语优先级继承可复用)
int8_t dTaskGetPriority(dTaskRef dtHandle);

uint16_t GetTaskID(void);
uint8_t  GetTaskStatu(void);
uint8_t GetCurrentReadyTaskCount(void);
void* GetBlockSeqList(void);

void TestCompaction(void);

void* dHandleDereference(uint16_t id);
uint16_t dHeapAlloc(size_t size);
void dHeapFree(uint16_t id);

//---------------- 信号量 ----------------
typedef uint16_t dsSemHandle;
#define DSS_INVALID_HANDLE UINT16_MAX

dsSemHandle dsSemCreate(uint16_t maxCount, uint16_t initCount);
void dsSemDelete(dsSemHandle handle);
bool dsSemGive(dsSemHandle handle);
bool dsSemTake(dsSemHandle handle);
bool dsSemTryTake(dsSemHandle handle);

//---------------- 互斥量(优先级继承) ----------------
typedef uint16_t dsMutexHandle;
#define DSM_INVALID_HANDLE UINT16_MAX

dsMutexHandle dsMutexCreate(void);
void dsMutexDelete(dsMutexHandle handle);
bool dsMutexLock(dsMutexHandle handle);
bool dsMutexTryLock(dsMutexHandle handle);
bool dsMutexUnlock(dsMutexHandle handle);

//---------------- 事件组 ----------------
typedef uint16_t dsEventHandle;
#define DSE_INVALID_HANDLE UINT16_MAX

dsEventHandle dsEventCreate(void);
void dsEventDelete(dsEventHandle handle);
bool dsEventSet(dsEventHandle handle, uint32_t flags);
bool dsEventClear(dsEventHandle handle, uint32_t flags);
bool dsEventWait(dsEventHandle handle, uint32_t flags, bool waitAll);
bool dsEventTryWait(dsEventHandle handle, uint32_t flags, bool waitAll);
uint32_t dsEventGetFlags(dsEventHandle handle);

//---------------- 邮箱(post) ----------------
typedef uint16_t dsPostHandle;
typedef uint16_t dsPostSubject;
#define DSP_INVALID_HANDLE UINT16_MAX
#define DSP_SUBJECT_ANY    0xFFFF       //收件主题通配: 匹配任意主题
#define DSP_OVERFLOW_BLOCK       0      //满箱策略: 阻塞发送者
#define DSP_OVERFLOW_DROP_OLDEST 1      //满箱策略: 丢最早入箱的邮件
#define DSP_OVERFLOW_DROP_NEWEST 2      //满箱策略: 丢最新入箱的邮件
#define DSP_WAIT_FOREVER 0xFFFFFFFF     //无限等待
#define DSP_NO_WAIT      0              //不等待

dsPostHandle dsPostCreate(uint16_t capacity, uint8_t overflowPolicy);
void dsPostDelete(dsPostHandle handle);
bool dsPostSend(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent);
bool dsPostTrySend(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent);
bool dsPostSendWait(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent, uint32_t ticks);
uint16_t dsPostRecv(dsPostHandle handle);
uint16_t dsPostTryRecv(dsPostHandle handle);
uint16_t dsPostRecvWait(dsPostHandle handle, dsPostSubject subject, uint32_t ticks);
uint16_t dsPostCount(dsPostHandle handle);

//---------------- 异步信号槽(connect, Qt式) ----------------
typedef uint16_t daSignalHandle;
typedef uint16_t daSlotHandle;
#define DAS_INVALID_HANDLE UINT16_MAX

daSignalHandle daConnectCreate(uint16_t slotCapacity);                          // 注册信号(KernelBlock)
daSlotHandle   daConnectSlot(dTaskRef task, void (*fn)(void*), void* ctx);      // 注册槽(KernelBlock)
bool daConnect(daSignalHandle sig, daSlotHandle slot);                          // 连接(可任意处调用)
bool daDisconnect(daSignalHandle sig, daSlotHandle slot);                       // 断开(对称)
bool daConnectTrigger(daSignalHandle sig, void* arg);                           // 触发

#ifdef __cplusplus
}
#endif

#endif      //DOORT_H