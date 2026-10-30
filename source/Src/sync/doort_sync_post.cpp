#include "doort_sync_post.hpp"
#include "doort_memory_handle.hpp"
#include "doort_port.hpp"
#include "doort_task.hpp"
#include "doort_kernel_scheduler.hpp"
#include "doort_task_blocked.hpp"
#include "doort_async_connect.hpp"

extern dkScheduler kScheduler;
extern dTaskBlockedManager dtb;

static dsPostPost* dsPostDereference(dsPostHandle handle) {     //句柄转指针, 每次现解析抗压缩
    if (handle == DSP_INVALID_HANDLE) return nullptr;
    return (dsPostPost*)dmhHandleDereference(handle);
}

static dsPostSlot* dsPostGetSlots(dsPostPost* p) {      //槽位数组每次现解析(槽位本身也是堆对象, 会被搬移)
    return (dsPostSlot*)dmhHandleDereference(p->dsPostSlotsHandle);
}

//满箱丢弃: 返回被丢弃的邮件句柄(由调用方在开中断后释放)
static uint16_t dsPostDropOne(dsPostPost* p, dsPostSlot* slots, bool dropOldest) {
    uint16_t dropped;
    if (dropOldest) {       //丢队首
        dropped = slots[p->dsPostHead].dsPostMailId;
        p->dsPostHead = (uint16_t)((p->dsPostHead + 1) % p->dsPostCapacity);    //队首后移
        p->dsPostCount--;
        if (p->dsPostUrgent) p->dsPostUrgent--;     //丢的若是紧急段, 紧急数减一
    } else {                                        //丢队尾
        uint16_t tail = (uint16_t)((p->dsPostHead + p->dsPostCount - 1) % p->dsPostCapacity);
        dropped = slots[tail].dsPostMailId;
        p->dsPostCount--;
        if (p->dsPostUrgent > p->dsPostCount) p->dsPostUrgent = p->dsPostCount;     //钳制紧急段末尾
    }
    return dropped;
}

//入队: 满箱按策略丢弃; *droppedOut 输出被丢弃邮件句柄
static bool dsPostEnqueue(dsPostPost* p, uint16_t mailId, uint16_t subject, bool urgent, uint16_t* droppedOut) {
    dsPostSlot* slots = dsPostGetSlots(p);
    *droppedOut = DSP_INVALID_HANDLE;

    if (p->dsPostCount >= p->dsPostCapacity) {      //满箱
        switch (p->dsPostOverflow) {
        case DSP_OVERFLOW_DROP_OLDEST:      //丢最旧
            *droppedOut = dsPostDropOne(p, slots, true);
            break;
        case DSP_OVERFLOW_DROP_NEWEST:      //丢最新
            *droppedOut = dsPostDropOne(p, slots, false);
            break;
        default:
            return false;       //BLOCK 策略由调用方负责阻塞, 这里直接失败
        }
    }

    if (urgent) {       //紧急邮件插队到队首
        p->dsPostHead = (uint16_t)((p->dsPostHead + p->dsPostCapacity - 1) % p->dsPostCapacity);
        slots[p->dsPostHead].dsPostSubject = subject;
        slots[p->dsPostHead].dsPostMailId  = mailId;
        p->dsPostUrgent++;
    } else {    //普通邮件追加到队尾
        uint16_t tail = (uint16_t)((p->dsPostHead + p->dsPostCount) % p->dsPostCapacity);
        slots[tail].dsPostSubject = subject;
        slots[tail].dsPostMailId  = mailId;
    }
    p->dsPostCount++;
    return true;
}

//取首个主题匹配的邮件(从逻辑队首起扫, 前移填补空洞); 无匹配返回无效句柄
static uint16_t dsPostDequeue(dsPostPost* p, uint16_t subject) {
    if (p->dsPostCount == 0) return DSP_INVALID_HANDLE;
    dsPostSlot* slots = dsPostGetSlots(p);

    for (uint16_t i = 0; i < p->dsPostCount; i++) {
        uint16_t index = (uint16_t)((p->dsPostHead + i) % p->dsPostCapacity);
        if (subject == DSP_SUBJECT_ANY || slots[index].dsPostSubject == subject) {      //主题匹配
            uint16_t mail = slots[index].dsPostMailId;
            for (uint16_t j = i; j + 1 < p->dsPostCount; j++) {     //把后面邮件前移填补空洞
                uint16_t src = (uint16_t)((p->dsPostHead + j + 1) % p->dsPostCapacity);
                uint16_t dst = (uint16_t)((p->dsPostHead + j) % p->dsPostCapacity);
                slots[dst] = slots[src];
            }
            p->dsPostCount--;
            if (i < p->dsPostUrgent) p->dsPostUrgent--;     //取走的若是紧急段, 紧急数减一
            if (p->dsPostUrgent > p->dsPostCount) p->dsPostUrgent = p->dsPostCount;
            return mail;
        }
    }
    return DSP_INVALID_HANDLE;
}

//等待者是否已在列表中(判断是否超时/仍挂着)
static bool dsPostWaiterIsInList(struct dsPostWaiter* list, uint16_t count, dTaskRef task) {
    for (uint16_t i = 0; i < count; i++)
        if (list[i].dsPostTask == task) return true;
    return false;
}

//从等待者列表摘除指定任务
static void dsPostRemoveWaiter(struct dsPostWaiter* list, uint16_t* count, dTaskRef task) {
    for (uint16_t i = 0; i < *count; i++) {
        if (list[i].dsPostTask == task) {
            for (uint16_t j = i; j + 1 < *count; j++)
                list[j] = list[j + 1];      //前移填补空洞
            (*count)--;
            return;
        }
    }
}

//唤醒首个主题匹配的收件等待者; 摘表+撤销超时; 返回任务由调用方 dTaskTargetReady
static dTaskRef dsPostWakeRecvWaiter(dsPostPost* p, uint16_t subject) {
    for (uint16_t i = 0; i < p->dsPostRecvWaitCount; i++) {
        struct dsPostWaiter* w = &p->dsPostRecvWaitList[i];
        if (w->dsPostSubject == DSP_SUBJECT_ANY || w->dsPostSubject == subject) {       //主题匹配
            dTaskRef task = w->dsPostTask;
            if (w->dsPostTimed) dtb.dtbTaskCancel((dTaskControlBlock*)task);    //有超时则撤销, 防重复唤醒
            dsPostRemoveWaiter(p->dsPostRecvWaitList, &p->dsPostRecvWaitCount, task);
            return task;
        }
    }
    return nullptr;
}

//唤醒队首满箱发送等待者(腾出空间后让其补发); 同样摘表+撤销超时
static dTaskRef dsPostWakeSendWaiter(dsPostPost* p) {
    if (p->dsPostSendWaitCount == 0) return nullptr;
    struct dsPostWaiter* w = &p->dsPostSendWaitList[0];
    dTaskRef task = w->dsPostTask;
    if (w->dsPostTimed) dtb.dtbTaskCancel((dTaskControlBlock*)task);
    dsPostRemoveWaiter(p->dsPostSendWaitList, &p->dsPostSendWaitCount, task);
    return task;
}

void dsPostPost::dsPostInit(uint16_t capacity, uint8_t overflowPolicy, uint16_t slotsHandle) {
    dsPostCapacity      = capacity;
    dsPostCount         = 0;
    dsPostHead          = 0;
    dsPostUrgent        = 0;
    dsPostOverflow      = overflowPolicy;
    dsPostSlotsHandle   = slotsHandle;
    dsPostRecvWaitCount = 0;
    dsPostSendWaitCount = 0;
}

bool dsPostPost::dsPostSendWait(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent, uint32_t ticks) {
    dsPostPost* p = dsPostDereference(handle);
    if (p == nullptr) return false;

    for (;;) {      //阻塞被唤醒后回到这里重试入队
        dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();

        if (p->dsPostCount < p->dsPostCapacity || p->dsPostOverflow != DSP_OVERFLOW_BLOCK) {    //有空位或非阻塞策略
            uint16_t dropped;
            if (!dsPostEnqueue(p, mailId, subject, urgent, &dropped)) return false;
            dTaskRef w = dsPostWakeRecvWaiter(p, subject);          //入队后唤醒一个匹配收件者
            ENABLE_IRQ();                                           //开中断后再做唤醒/释放
            if (dropped != DSP_INVALID_HANDLE) dmhFree(dropped);    //释放被丢弃的邮件
            if (w != nullptr) dTaskTargetReady(w);
            return true;
        }

        if (ticks == DSP_NO_WAIT || p->dsPostSendWaitCount >= DOORT_SYNC_POST_MAX_WAITERS) return false;

        struct dsPostWaiter* w = &p->dsPostSendWaitList[p->dsPostSendWaitCount];
        w->dsPostTask    = (dTaskRef)currentTask;       //登记发送等待者
        w->dsPostSubject = subject;
        w->dsPostTimed   = (ticks != DSP_WAIT_FOREVER);     //是否带超时
        p->dsPostSendWaitCount++;

        currentTask->dtStatusCode = DOORT_TASK_STATUS_BLOCKED;      //置阻塞
        kScheduler.dksModifyCountAndMap(currentTask, 0);            //移出就绪
        if (w->dsPostTimed) dtb.dtbTaskBlocked(ticks);              //注册内核超时闹钟

        kScheduler.dksSelectPerformTask();      //选下一个任务
        kScheduler.dksSwapTask();               //切换
        ENABLE_IRQ();
        daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调

        //被唤醒后重新解析句柄(压缩搬移后地址已变)
        DISABLE_IRQ();
        p = dsPostDereference(handle);
        if (p == nullptr) return false;
        if (dsPostWaiterIsInList(p->dsPostSendWaitList, p->dsPostSendWaitCount, (dTaskRef)currentTask)) {
            //仍在等待表 = 超时被唤醒(非补位), 摘表返回失败
            dsPostRemoveWaiter(p->dsPostSendWaitList, &p->dsPostSendWaitCount, (dTaskRef)currentTask);
            return false;
        }
        //否则是补位早醒, 回到循环重试(IRQ 仍关闭)
    }
}


uint16_t dsPostPost::dsPostRecvWait(dsPostHandle handle, dsPostSubject subject, uint32_t ticks) {
    dsPostPost* p = dsPostDereference(handle);
    if (p == nullptr) return DSP_INVALID_HANDLE;

    for (;;) {      //阻塞被唤醒后回到这里重试取信
        dTaskControlBlock* currentTask = (dTaskControlBlock*)kScheduler.dksGetCurrentTask();

        uint16_t mail = dsPostDequeue(p, subject);
        if (mail != DSP_INVALID_HANDLE) {               //取到信
            dTaskRef s = dsPostWakeSendWaiter(p);       //腾出空间, 唤醒一个满箱发送者补位
            ENABLE_IRQ();
            if (s != nullptr) dTaskTargetReady(s);
            return mail;
        }

        if (ticks == DSP_NO_WAIT || p->dsPostRecvWaitCount >= DOORT_SYNC_POST_MAX_WAITERS) {
            return DSP_INVALID_HANDLE;      //不等待或满
        }

        struct dsPostWaiter* w = &p->dsPostRecvWaitList[p->dsPostRecvWaitCount];
        w->dsPostTask    = (dTaskRef)currentTask;       //登记收件等待者
        w->dsPostSubject = subject;
        w->dsPostTimed   = (ticks != DSP_WAIT_FOREVER);
        p->dsPostRecvWaitCount++;

        currentTask->dtStatusCode = DOORT_TASK_STATUS_BLOCKED;      //置阻塞
        kScheduler.dksModifyCountAndMap(currentTask, 0);            //移出就绪
        if (w->dsPostTimed) dtb.dtbTaskBlocked(ticks);              //注册超时

        kScheduler.dksSelectPerformTask();
        kScheduler.dksSwapTask();
        ENABLE_IRQ();
        daConnectSignalDrainTask((dTaskRef)kScheduler.dksGetCurrentTask());     //恢复时先排空延迟信号回调

        //被唤醒后重新解析句柄(抗压缩)
        DISABLE_IRQ();
        p = dsPostDereference(handle);
        if (p == nullptr) return DSP_INVALID_HANDLE;
        if (dsPostWaiterIsInList(p->dsPostRecvWaitList, p->dsPostRecvWaitCount, (dTaskRef)currentTask)) {
            //仍在等待表 = 超时唤醒, 摘表返回
            dsPostRemoveWaiter(p->dsPostRecvWaitList, &p->dsPostRecvWaitCount, (dTaskRef)currentTask);
            return DSP_INVALID_HANDLE;
        }
        //早醒则回到循环重试取信
    }
}

uint16_t dsPostPost::dsPostCountMail(void) {
    return dsPostCount;
}

void dsPostPost::dsPostCancelWaiters(void) {    //撤销全部等待者的内核超时闹钟(删除邮箱时用)
    for (uint16_t i = 0; i < dsPostRecvWaitCount; i++)
        if (dsPostRecvWaitList[i].dsPostTimed)
            dtb.dtbTaskCancel((dTaskControlBlock*)dsPostRecvWaitList[i].dsPostTask);
    for (uint16_t i = 0; i < dsPostSendWaitCount; i++)
        if (dsPostSendWaitList[i].dsPostTimed)
            dtb.dtbTaskCancel((dTaskControlBlock*)dsPostSendWaitList[i].dsPostTask);
}


dsPostHandle dsPostCreate(uint16_t capacity, uint8_t overflowPolicy) {
    if (capacity == 0) return DSP_INVALID_HANDLE;
    if (overflowPolicy > DSP_OVERFLOW_DROP_NEWEST) overflowPolicy = DSP_OVERFLOW_BLOCK;

    uint16_t id = dmhAlloc(sizeof(dsPostPost));     //分配邮箱对象
    if (id == DSP_INVALID_HANDLE) return DSP_INVALID_HANDLE;

    dsPostPost* p = dsPostDereference(id);
    if (p == nullptr) {
        dmhFree(id);
        return DSP_INVALID_HANDLE;
    }

    uint16_t slotsHandle = dmhAlloc(capacity * sizeof(dsPostSlot));     //分配槽位数组(独立堆对象, 抗压缩)
    if (slotsHandle == DSP_INVALID_HANDLE) {
        dmhFree(id);
        return DSP_INVALID_HANDLE;
    }

    p->dsPostInit(capacity, overflowPolicy, slotsHandle);
    return id;
}

void dsPostDelete(dsPostHandle handle) {
    DISABLE_IRQ();
    dsPostPost* p = dsPostDereference(handle);
    if (p == nullptr) {
        ENABLE_IRQ();
        return;
    }

    p->dsPostCancelWaiters();       //先撤销等待者超时

    //先保存字段到局部, 开中断后只用句柄操作(抗压缩)
    uint16_t capacity    = p->dsPostCapacity;
    uint16_t count       = p->dsPostCount;
    uint16_t head        = p->dsPostHead;
    uint16_t slotsHandle = p->dsPostSlotsHandle;

    uint16_t n = p->dsPostRecvWaitCount;
    dTaskRef rw[DOORT_SYNC_POST_MAX_WAITERS];
    for (uint16_t i = 0; i < n; i++) rw[i] = p->dsPostRecvWaitList[i].dsPostTask;
    uint16_t m = p->dsPostSendWaitCount;
    dTaskRef sw[DOORT_SYNC_POST_MAX_WAITERS];
    for (uint16_t i = 0; i < m; i++) sw[i] = p->dsPostSendWaitList[i].dsPostTask;

    ENABLE_IRQ();

    //释放未投递邮件与槽位数组(每次重新解引用, 抗压缩)
    for (uint16_t i = 0; i < count; i++) {
        uint16_t index = (uint16_t)((head + i) % capacity);
        dsPostSlot* slots = (dsPostSlot*)dmhHandleDereference(slotsHandle);
        dmhFree(slots[index].dsPostMailId);
    }
    dmhFree(slotsHandle);
    dmhFree(handle);

    for (uint16_t i = 0; i < n; i++) dTaskTargetReady(rw[i]);       //唤醒等待者
    for (uint16_t i = 0; i < m; i++) dTaskTargetReady(sw[i]);
}

bool dsPostSend(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent) {
    return dsPostSendWait(handle, mailId, subject, urgent, DSP_WAIT_FOREVER);
}

bool dsPostTrySend(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent) {
    return dsPostSendWait(handle, mailId, subject, urgent, DSP_NO_WAIT);
}

bool dsPostSendWait(dsPostHandle handle, uint16_t mailId, uint16_t subject, bool urgent, uint32_t ticks) {
    DISABLE_IRQ();      //关中断保证解引用+使用原子
    dsPostPost* p = dsPostDereference(handle);
    if (p == nullptr) {
        ENABLE_IRQ();
        return false;
    }
    bool r = p->dsPostSendWait(handle, mailId, subject, urgent, ticks);
    ENABLE_IRQ();
    return r;
}

uint16_t dsPostRecv(dsPostHandle handle) {
    return dsPostRecvWait(handle, DSP_SUBJECT_ANY, DSP_WAIT_FOREVER);
}

uint16_t dsPostTryRecv(dsPostHandle handle) {
    return dsPostRecvWait(handle, DSP_SUBJECT_ANY, DSP_NO_WAIT);
}

uint16_t dsPostRecvWait(dsPostHandle handle, dsPostSubject subject, uint32_t ticks) {
    DISABLE_IRQ();
    dsPostPost* p = dsPostDereference(handle);
    if (p == nullptr) {
        ENABLE_IRQ();
        return DSP_INVALID_HANDLE;
    }
    uint16_t r = p->dsPostRecvWait(handle, subject, ticks);
    ENABLE_IRQ();
    return r;
}

uint16_t dsPostCount(dsPostHandle handle) {
    DISABLE_IRQ();
    dsPostPost* p = dsPostDereference(handle);
    if (p == nullptr) {
        ENABLE_IRQ();
        return 0;
    }
    uint16_t c = p->dsPostCountMail();
    ENABLE_IRQ();
    return c;
}

