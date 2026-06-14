#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer/id_table.h"

constexpr uint8_t SNAPSHOT_DIFF_ADDED = 1;
constexpr uint8_t SNAPSHOT_DIFF_REMOVED = 2;
constexpr uint8_t SNAPSHOT_DIFF_CHANGED = 3;

struct SnapshotRecord
{
    bool present = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

struct SnapshotDiffRecord
{
    uint8_t channel = 0;
    uint16_t id = 0;
    uint8_t kind = 0;
    uint8_t dlc_a = 0;
    uint8_t data_a[8] = {};
    uint8_t dlc_b = 0;
    uint8_t data_b[8] = {};
};

enum class SnapshotSlot : uint8_t
{
    A = 0,
    B = 1,
};

class SnapshotStore
{
public:
    void init(SnapshotRecord *slotA, SnapshotRecord *slotB);
    void capture(SnapshotSlot slot, const IdTable &table);
    size_t diff(SnapshotDiffRecord *out, size_t cap) const;

private:
    SnapshotRecord *a_ = nullptr;
    SnapshotRecord *b_ = nullptr;

    static size_t key(uint8_t channel, uint32_t id)
    {
        return static_cast<size_t>(channel) * kStdIdCount + id;
    }
};
