#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"
#include "ws_protocol.h"

class PretriggerBuffer
{
public:
    void init(CapturedFrame *storage, size_t capacity);
    void push(const CapturedFrame &frame);
    size_t collect(uint64_t now_us, uint32_t window_us, CapturedFrame *out, size_t cap) const;
    size_t summarize(uint64_t now_us, uint32_t window_us, WsPretriggerRecord *out, size_t cap) const;

private:
    CapturedFrame *storage_ = nullptr;
    size_t capacity_ = 0;
    size_t head_ = 0;
    size_t count_ = 0;
};
