#pragma once
#include <cstdint>
#include "analyzer_types.h"
#include "id_table.h"

// 单次窗口内的双通道总线统计快照。
struct BusStatsSnapshot
{
    uint16_t fps[kChannelCount] = {};       // 每通道每秒帧数
    uint16_t load_x10[kChannelCount] = {};  // 每通道总线占用率%×10
    uint32_t dropped = 0;                   // 队列累计丢帧
};

// 双通道总线负载/帧率统计器。
// 仅消费者（Core1）调用：每帧 noteRx 累加，定期 update 结算一个窗口。
class BusStatsTracker
{
public:
    // 标准帧位数估算：SOF + 11位ID + 控制/CRC/ACK/EOF 等固定开销 ≈ 47 位 + 数据位。
    static constexpr uint32_t kFrameOverheadBits = 47;
    static constexpr uint32_t kBitsPerByte = 8;
    static constexpr uint32_t kBusBitrate = 500000; // 500kbps

    void begin(uint32_t nowMs)
    {
        windowStartMs_ = nowMs;
        for (uint8_t c = 0; c < kChannelCount; ++c)
        {
            frames_[c] = 0;
            bits_[c] = 0;
        }
    }

    // 累计一帧：帧数 +1，位数按 DLC 估算累加。
    void noteRx(const CapturedFrame &frame)
    {
        const uint8_t ch = frame.channel < kChannelCount ? frame.channel : 0;
        const uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
        frames_[ch]++;
        bits_[ch] += kFrameOverheadBits + static_cast<uint32_t>(dlc) * kBitsPerByte;
    }

    // 若窗口（默认 1 秒）已满则结算快照并重置；否则不动。
    void update(uint32_t nowMs, uint32_t droppedTotal)
    {
        dropped_ = droppedTotal;
        const uint32_t elapsed = nowMs - windowStartMs_;
        if (elapsed < kWindowMs)
            return;

        for (uint8_t c = 0; c < kChannelCount; ++c)
        {
            snapshot_.fps[c] = frames_[c] > 65535 ? 65535 : static_cast<uint16_t>(frames_[c]);
            // 占用率 = 本窗口位数 / (比特率 × 窗口秒数) × 1000(=%×10)
            const uint64_t capacityBits = static_cast<uint64_t>(kBusBitrate) * elapsed / 1000;
            uint32_t loadX10 = 0;
            if (capacityBits > 0)
                loadX10 = static_cast<uint32_t>(static_cast<uint64_t>(bits_[c]) * 1000 / capacityBits);
            snapshot_.load_x10[c] = loadX10 > 1000 ? 1000 : static_cast<uint16_t>(loadX10);
            frames_[c] = 0;
            bits_[c] = 0;
        }
        snapshot_.dropped = dropped_;
        windowStartMs_ = nowMs;
    }

    BusStatsSnapshot snapshot() const
    {
        BusStatsSnapshot s = snapshot_;
        s.dropped = dropped_;
        return s;
    }

private:
    static constexpr uint32_t kWindowMs = 1000;

    uint32_t windowStartMs_ = 0;
    uint32_t frames_[kChannelCount] = {};
    uint32_t bits_[kChannelCount] = {};
    uint32_t dropped_ = 0;
    BusStatsSnapshot snapshot_;
};
