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
    bool changed = !r.present;

    for (uint8_t i = 0; i < dlc; ++i)
    {
        if (!r.present || r.data[i] != frame.data[i])
        {
            r.byte_change_ts[i] = frame.ts_us;
            changed = true;
        }
        r.data[i] = frame.data[i];
    }

    r.prev_rx_ts = r.present ? r.last_rx_ts : frame.ts_us;
    r.last_rx_ts = frame.ts_us;
    r.dlc = dlc;
    r.rx_count++;

    if (r.present)
    {
        const uint64_t delta64 = frame.ts_us - r.prev_rx_ts;
        r.last_delta_us = delta64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(delta64);
        if (r.period_est_us == 0)
            r.period_est_us = r.last_delta_us;
        else
            r.period_est_us = (r.period_est_us * 7 + r.last_delta_us) / 8;
        r.jitter_us = r.last_delta_us > r.period_est_us ?
            r.last_delta_us - r.period_est_us : r.period_est_us - r.last_delta_us;
    }

    if (changed)
    {
        r.last_change_ts = frame.ts_us;
        if (r.change_score < UINT16_MAX)
            r.change_score++;
    }

    r.present = true;
}
