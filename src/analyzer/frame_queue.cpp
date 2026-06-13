#include "analyzer/frame_queue.h"

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

bool FrameQueue::push(const CapturedFrame &frame)
{
    const uint16_t head = LOAD(head_);
    const uint16_t next = static_cast<uint16_t>((head + 1) % capacity_);
    if (next == LOAD(tail_))
    {
        STORE(dropped_, LOAD(dropped_) + 1);
        return false;
    }
    buffer_[head] = frame;
    STORE(head_, next);
    return true;
}

bool FrameQueue::pop(CapturedFrame &out)
{
    const uint16_t tail = LOAD(tail_);
    if (tail == LOAD(head_))
        return false;
    out = buffer_[tail];
    STORE(tail_, static_cast<uint16_t>((tail + 1) % capacity_));
    return true;
}

uint32_t FrameQueue::dropped() const
{
    return LOAD(dropped_);
}
