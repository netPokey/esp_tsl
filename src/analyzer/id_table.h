#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"

constexpr uint16_t kStdIdCount = 2048;
constexpr uint8_t kChannelCount = 2;

struct IdRecord
{
    bool present = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
    uint64_t byte_change_ts[8] = {};
    uint64_t last_rx_ts = 0;
    uint64_t prev_rx_ts = 0;
    uint32_t rx_count = 0;
    uint32_t period_est = 0;
    uint16_t change_score = 0;
    uint8_t flags = 0;
};

class IdTable
{
public:
    static constexpr size_t kStorageBytes = sizeof(IdRecord) * kChannelCount * kStdIdCount;

    void init(IdRecord *base);
    void update(const CapturedFrame &frame);

    IdRecord &record(uint8_t channel, uint32_t id);
    const IdRecord &record(uint8_t channel, uint32_t id) const;

private:
    IdRecord *base_ = nullptr;
    IdRecord &at(uint8_t channel, uint32_t id);
};
