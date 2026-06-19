#include "analyzer/id_table.h"
#include <cstring>

// IdTable 是消费侧的主状态缓存：把原始帧折叠成每 ID 一条最新记录。
// 这里不加锁，依赖架构约定（只有 analyzerWebLoop/loopTask 调用）。

void IdTable::init(IdRecord *base)
{
    base_ = base;
    // PSRAM/堆分配的内存不保证清零；present=false、计数=0 是后续首次帧判断的前提。
    memset(base_, 0, kStorageBytes);
}

IdRecord &IdTable::at(uint8_t channel, uint32_t id)
{
    // 防御式钳制：外层一般已过滤非法 ID；这里兜底避免错误输入越界写 PSRAM。
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
    // 首次出现也视作"变化"，这样 UI 会立即标出新 ID，change_score 从 1 开始。
    bool changed = !r.present;

    // 只比较本帧 DLC 范围内的字节；每字节单独记录最近变化时间，用于 UI 的 hot/warm 高亮。
    for (uint8_t i = 0; i < dlc; ++i)
    {
        if (!r.present || r.data[i] != frame.data[i])
        {
            r.byte_change_ts[i] = frame.ts_us;
            changed = true;
        }
        r.data[i] = frame.data[i];
    }

    // 先保存上一帧时间，再写入本帧时间；首次帧让 prev=当前，避免产生虚假的大 delta。
    r.prev_rx_ts = r.present ? r.last_rx_ts : frame.ts_us;
    r.last_rx_ts = frame.ts_us;
    r.dlc = dlc;
    r.rx_count++;

    if (r.present)
    {
        const uint64_t delta64 = frame.ts_us - r.prev_rx_ts;
        // Web 协议只下发毫秒级 16-bit 截断值，但内部先保留微秒级 uint32，便于平滑估计。
        r.last_delta_us = delta64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(delta64);
        if (r.period_est_us == 0)
            r.period_est_us = r.last_delta_us;
        else
            // 简单 EWMA：7/8 保留历史、1/8 接收新 delta，抑制偶发调度抖动。
            r.period_est_us = (r.period_est_us * 7 + r.last_delta_us) / 8;
        // jitter 取当前 delta 与平滑周期估计的绝对差，供 UI 观察周期稳定性。
        r.jitter_us = r.last_delta_us > r.period_est_us ?
            r.last_delta_us - r.period_est_us : r.period_est_us - r.last_delta_us;
    }

    if (changed)
    {
        r.last_change_ts = frame.ts_us;
        // change_score 是单调饱和计数，不随时间衰减；前端用它做"活跃度"排序。
        if (r.change_score < UINT16_MAX)
            r.change_score++;
    }

    r.present = true;
}
