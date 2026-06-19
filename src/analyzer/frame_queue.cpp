#include "analyzer/frame_queue.h"

// ============================================================================
// SPSC（单生产者单消费者）无锁环形队列实现。
// 约定：仅 Core1 高优先级 rx_task 调 push（写 head_），仅 loop/Web 消费者调 pop（写 tail_）。
// 因生产/消费各自只写一个索引，配合 acquire/release 内存序即可无锁同步：
//   - 生产者写完 buffer_ 后以 release 发布新 head_，保证数据先于索引可见；
//   - 消费者以 acquire 读 head_，看到新索引时必能看到对应数据。
// NATIVE_BUILD（主机单元测试）无并发，退化为普通读写以简化。
// ============================================================================

#ifdef NATIVE_BUILD
#define LOAD(x) (x)
#define STORE(x, v) ((x) = (v))
#else
#define LOAD(x) (x).load(std::memory_order_acquire)
#define STORE(x, v) (x).store((v), std::memory_order_release)
#endif

void FrameQueue::init(CapturedFrame *buffer, uint16_t capacity)
{
    buffer_ = buffer;
    capacity_ = capacity;
    STORE(head_, 0);
    STORE(tail_, 0);
    STORE(dropped_, 0);
}

// 生产者侧（CAN rx_task）。返回 false 表示队满丢帧。
bool FrameQueue::push(const CapturedFrame &frame)
{
    const uint16_t head = LOAD(head_);
    // 预留一个空槽区分"空"与"满"：next==tail_ 即视为满，否则空满无法分辨。
    const uint16_t next = static_cast<uint16_t>((head + 1) % capacity_);
    if (next == LOAD(tail_))
    {
        STORE(dropped_, LOAD(dropped_) + 1);
        return false;
    }
    buffer_[head] = frame;   // 先写数据，
    STORE(head_, next);      // 再以 release 发布索引，顺序不可颠倒（见文件头说明）。
    return true;
}

// 消费者侧（analyzerWebLoop）。返回 false 表示队空。
bool FrameQueue::pop(CapturedFrame &out)
{
    const uint16_t tail = LOAD(tail_);
    if (tail == LOAD(head_))  // tail 追上 head 即为空。
        return false;
    out = buffer_[tail];      // 先取数据，
    STORE(tail_, static_cast<uint16_t>((tail + 1) % capacity_));  // 再释放槽位。
    return true;
}

// 累计丢帧数（队满时递增），供 Web/统计层观察是否消费跟不上。
uint32_t FrameQueue::dropped() const
{
    return LOAD(dropped_);
}
