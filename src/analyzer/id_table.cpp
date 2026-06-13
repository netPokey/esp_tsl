#include "analyzer/id_table.h"
#include <cstring>

void IdTable::init(IdRecord *base)
{
    base_ = base;
    memset(base_, 0, kStorageBytes);
}

IdRecord &IdTable::at(uint8_t channel, uint32_t id)
{
    const uint8_t ch = channel < kChannelCount ? channel : 0;
    const uint32_t idx = id < kStdIdCount ? id : 0;
    return base_[ch * kStdIdCount + idx];
}

IdRecord &IdTable::record(uint8_t channel, uint32_t id)
{
    return at(channel, id);
}

const IdRecord &IdTable::record(uint8_t channel, uint32_t id) const
{
    return const_cast<IdTable *>(this)->at(channel, id);
}

void IdTable::update(const CapturedFrame &frame)
{
    if (frame.id >= kStdIdCount)
        return;

    IdRecord &r = at(frame.channel, frame.id);
    const uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;

    for (uint8_t i = 0; i < dlc; ++i)
    {
        if (!r.present || r.data[i] != frame.data[i])
            r.byte_change_ts[i] = frame.ts_us;
        r.data[i] = frame.data[i];
    }

    r.prev_rx_ts = r.present ? r.last_rx_ts : frame.ts_us;
    r.last_rx_ts = frame.ts_us;
    r.dlc = dlc;
    r.rx_count++;
    r.present = true;
}
