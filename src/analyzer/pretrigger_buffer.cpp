#include "analyzer/pretrigger_buffer.h"
#include "analyzer/id_table.h"
#include <cstring>

namespace
{
uint16_t elapsedMsAgo(uint64_t nowUs, uint64_t thenUs)
{
    if (thenUs >= nowUs)
        return 0;
    const uint64_t ms = (nowUs - thenUs) / 1000ULL;
    return ms > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(ms);
}

bool dataDiffers(const WsPretriggerRecord &record, const CapturedFrame &frame)
{
    const uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
    if (record.dlc != dlc)
        return true;
    return memcmp(record.data, frame.data, dlc) != 0;
}

bool isSeen(const uint8_t *seen, size_t key)
{
    return (seen[key >> 3] & static_cast<uint8_t>(1u << (key & 7))) != 0;
}

void setSeen(uint8_t *seen, size_t key)
{
    seen[key >> 3] |= static_cast<uint8_t>(1u << (key & 7));
}
}

void PretriggerBuffer::init(CapturedFrame *storage, size_t capacity)
{
    storage_ = storage;
    capacity_ = capacity;
    head_ = 0;
    count_ = 0;
}

void PretriggerBuffer::push(const CapturedFrame &frame)
{
    if (storage_ == nullptr || capacity_ < 2)
        return;

    storage_[head_] = frame;
    head_ = (head_ + 1) % capacity_;

    const size_t usable = capacity_ - 1;
    if (count_ < usable)
        count_++;
}

size_t PretriggerBuffer::collect(uint64_t now_us, uint32_t window_us, CapturedFrame *out, size_t cap) const
{
    if (storage_ == nullptr || out == nullptr || cap == 0 || capacity_ < 2)
        return 0;

    const uint64_t start = now_us > window_us ? now_us - window_us : 0;
    const size_t oldest = (head_ + capacity_ - count_) % capacity_;
    size_t written = 0;

    for (size_t i = 0; i < count_ && written < cap; ++i)
    {
        const CapturedFrame &frame = storage_[(oldest + i) % capacity_];
        if (frame.ts_us >= start && frame.ts_us <= now_us)
            out[written++] = frame;
    }

    return written;
}

size_t PretriggerBuffer::summarize(uint64_t now_us, uint32_t window_us, WsPretriggerRecord *out, size_t cap) const
{
    return summarize(now_us, window_us, out, cap, 0);
}

size_t PretriggerBuffer::summarize(uint64_t now_us, uint32_t window_us, WsPretriggerRecord *out, size_t cap, size_t skip) const
{
    if (storage_ == nullptr || out == nullptr || cap == 0 || capacity_ < 2)
        return 0;

    const uint64_t start = now_us > window_us ? now_us - window_us : 0;
    const size_t oldest = (head_ + capacity_ - count_) % capacity_;
    uint8_t seen[kChannelCount * kStdIdCount / 8] = {};
    size_t skipped = 0;
    size_t summaries = 0;

    for (size_t i = 0; i < count_; ++i)
    {
        const CapturedFrame &frame = storage_[(oldest + i) % capacity_];
        if (frame.ts_us < start || frame.ts_us > now_us)
            continue;
        if (frame.id >= kStdIdCount || frame.channel >= kChannelCount)
            continue;

        const size_t key = static_cast<size_t>(frame.channel) * kStdIdCount + frame.id;
        const uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
        WsPretriggerRecord *record = nullptr;
        for (size_t j = 0; j < summaries; ++j)
        {
            if (out[j].channel == frame.channel && out[j].id == frame.id)
            {
                record = &out[j];
                break;
            }
        }

        if (record == nullptr)
        {
            if (isSeen(seen, key))
                continue;

            setSeen(seen, key);
            if (skipped < skip)
            {
                ++skipped;
                continue;
            }
            if (summaries >= cap)
                continue;

            record = &out[summaries++];
            memset(record, 0, sizeof(*record));
            record->channel = frame.channel;
            record->id = frame.id;
            record->first_seen_ms_ago = elapsedMsAgo(now_us, frame.ts_us);
            record->frames = 1;
            record->dlc = dlc;
            memcpy(record->data, frame.data, dlc);
        }
        else
        {
            if (dataDiffers(*record, frame) && record->changes < UINT16_MAX)
                record->changes++;
            if (record->frames < UINT16_MAX)
                record->frames++;
            record->dlc = dlc;
            memcpy(record->data, frame.data, dlc);
        }

        record->last_seen_ms_ago = elapsedMsAgo(now_us, frame.ts_us);
    }

    return summaries;
}
