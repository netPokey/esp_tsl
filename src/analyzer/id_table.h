#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"

// 标准 11-bit CAN ID 空间：0x000..0x7FF，共 2048 个 ID；两路总线固定为 A/B。
// IdTable 因此可以用定长数组 [channel][id] 做 O(1) 查询，避免哈希表和动态分配。
constexpr uint16_t kStdIdCount = 2048;
constexpr uint8_t kChannelCount = 2;

// 单个 (channel,id) 的最新观测状态。
// WebSocket 只推送脏 ID 的这份快照，不保留历史帧；历史统计由 rx_count/周期估计等字段折叠保存。
struct IdRecord
{
    bool present = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
    uint64_t byte_change_ts[8] = {};
    uint64_t last_rx_ts = 0;
    uint64_t prev_rx_ts = 0;
    uint64_t last_change_ts = 0;
    uint32_t rx_count = 0;
    uint32_t last_delta_us = 0;
    uint32_t period_est_us = 0;
    uint32_t jitter_us = 0;
    uint16_t change_score = 0;
    uint8_t flags = 0;
};

// 每通道/每标准 ID 的固定状态表。
// 存储由调用方提供（设备上放 PSRAM），类本身只保存 base_ 指针，避免在构造期依赖堆。
// 约定：只有 Web/分析循环调用 update()/record()；rx_task 只写 FrameQueue，不直接碰表。
class IdTable
{
public:
    // 调用方需分配这么多字节的 IdRecord 连续内存，布局为 channel * kStdIdCount + id。
    static constexpr size_t kStorageBytes = sizeof(IdRecord) * kChannelCount * kStdIdCount;

    void init(IdRecord *base);
    void update(const CapturedFrame &frame);

    // 返回指定槽位的可变/只读引用；无效 channel/id 会钳制到 0，调用方通常已先校验。
    IdRecord &record(uint8_t channel, uint32_t id);
    const IdRecord &record(uint8_t channel, uint32_t id) const;

private:
    IdRecord *base_ = nullptr;
    IdRecord &at(uint8_t channel, uint32_t id);
};
