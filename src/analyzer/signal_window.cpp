#include "analyzer/signal_window.h"

void WatchedSignalWindow::init(WindowSlot *slots, size_t slot_count, size_t samples_per_slot)
{
    slots_ = slots;
    slot_count_ = slot_count;
    samples_per_slot_ = samples_per_slot;
    next_sequence_ = 0;

    if (!slots_)
        return;

    for (size_t i = 0; i < slot_count_; ++i)
        resetSlot(slots_[i]);
}

bool WatchedSignalWindow::watch(uint8_t channel, uint16_t id)
{
    if (findSlot(channel, id))
        return true;

    WindowSlot *slot = findFreeSlot();
    if (!slot)
        return false;

    slot->channel = channel;
    slot->id = id;
    slot->watched = true;
    slot->head = 0;
    slot->count = 0;
    return true;
}

void WatchedSignalWindow::unwatch(uint8_t channel, uint16_t id)
{
    WindowSlot *slot = findSlot(channel, id);
    if (!slot)
        return;
    resetSlot(*slot);
}

bool WatchedSignalWindow::isWatched(uint8_t channel, uint16_t id) const
{
    return findSlot(channel, id) != nullptr;
}

void WatchedSignalWindow::push(const CapturedFrame &frame)
{
    WindowSlot *slot = findSlot(frame.channel, static_cast<uint16_t>(frame.id));
    if (!slot || !slot->samples || samples_per_slot_ == 0)
        return;

    RawSamplePoint &sample = slot->samples[slot->head];
    sample.ts_us = frame.ts_us;
    sample.dlc = frame.dlc;
    for (int i = 0; i < 8; ++i)
        sample.data[i] = frame.data[i];
    sample.sequence = next_sequence_++;

    slot->head = (slot->head + 1) % samples_per_slot_;
    if (slot->count < samples_per_slot_)
        ++slot->count;
}

size_t WatchedSignalWindow::copySamples(uint8_t channel, uint16_t id, RawSamplePoint *out, size_t cap) const
{
    const WindowSlot *slot = findSlot(channel, id);
    if (!slot || !slot->samples || !out || cap == 0 || slot->count == 0 || samples_per_slot_ == 0)
        return 0;

    const size_t n = slot->count < cap ? slot->count : cap;
    const size_t start = (slot->head + samples_per_slot_ - n) % samples_per_slot_;
    for (size_t i = 0; i < n; ++i)
        out[i] = slot->samples[(start + i) % samples_per_slot_];
    return n;
}

WindowSlot *WatchedSignalWindow::findSlot(uint8_t channel, uint16_t id)
{
    if (!slots_)
        return nullptr;

    for (size_t i = 0; i < slot_count_; ++i)
    {
        WindowSlot &slot = slots_[i];
        if (slot.watched && slot.channel == channel && slot.id == id)
            return &slot;
    }
    return nullptr;
}

const WindowSlot *WatchedSignalWindow::findSlot(uint8_t channel, uint16_t id) const
{
    if (!slots_)
        return nullptr;

    for (size_t i = 0; i < slot_count_; ++i)
    {
        const WindowSlot &slot = slots_[i];
        if (slot.watched && slot.channel == channel && slot.id == id)
            return &slot;
    }
    return nullptr;
}

WindowSlot *WatchedSignalWindow::findFreeSlot()
{
    if (!slots_)
        return nullptr;

    for (size_t i = 0; i < slot_count_; ++i)
    {
        if (!slots_[i].watched)
            return &slots_[i];
    }
    return nullptr;
}

void WatchedSignalWindow::resetSlot(WindowSlot &slot)
{
    slot.channel = 0;
    slot.id = 0;
    slot.watched = false;
    slot.head = 0;
    slot.count = 0;
}
