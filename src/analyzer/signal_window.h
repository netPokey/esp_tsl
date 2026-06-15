#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"

struct RawSamplePoint
{
    uint64_t ts_us = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
    uint32_t sequence = 0;
};

struct WindowSlot
{
    RawSamplePoint *samples = nullptr;
    uint8_t channel = 0;
    uint16_t id = 0;
    bool watched = false;
    size_t head = 0;
    size_t count = 0;
};

class WatchedSignalWindow
{
public:
    void init(WindowSlot *slots, size_t slot_count, size_t samples_per_slot);
    bool watch(uint8_t channel, uint16_t id);
    void unwatch(uint8_t channel, uint16_t id);
    bool isWatched(uint8_t channel, uint16_t id) const;
    void push(const CapturedFrame &frame);
    size_t copySamples(uint8_t channel, uint16_t id, RawSamplePoint *out, size_t cap) const;

private:
    WindowSlot *slots_ = nullptr;
    size_t slot_count_ = 0;
    size_t samples_per_slot_ = 0;
    uint32_t next_sequence_ = 0;

    WindowSlot *findSlot(uint8_t channel, uint16_t id);
    const WindowSlot *findSlot(uint8_t channel, uint16_t id) const;
    WindowSlot *findFreeSlot();
    void resetSlot(WindowSlot &slot);
};
