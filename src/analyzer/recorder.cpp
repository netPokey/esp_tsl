#include "recorder.h"

void Recorder::init(CapturedFrame *storage, size_t capacity)
{
    storage_ = storage;
    capacity_ = capacity;
    head_ = 0;
    count_ = 0;
    dropped_ = 0;
    active_ = false;
}

void Recorder::start()
{
    head_ = 0;
    count_ = 0;
    dropped_ = 0;
    active_ = true;
}

void Recorder::stop()
{
    active_ = false;
}

void Recorder::push(const CapturedFrame &frame)
{
    if (!storage_ || capacity_ == 0)
        return;
    storage_[head_] = frame;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_)
        ++count_;
    else
        ++dropped_;
}

size_t Recorder::collect(CapturedFrame *out, size_t cap, size_t skip) const
{
    if (!storage_ || count_ == 0 || skip >= count_ || cap == 0)
        return 0;
    size_t oldest = (count_ < capacity_) ? 0 : head_;
    size_t avail = count_ - skip;
    size_t n = avail < cap ? avail : cap;
    for (size_t i = 0; i < n; ++i)
        out[i] = storage_[(oldest + skip + i) % capacity_];
    return n;
}
