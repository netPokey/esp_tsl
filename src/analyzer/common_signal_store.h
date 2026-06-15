#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kMaxCommonSignals = 32;
constexpr size_t kCommonSignalLabelLen = 24;

struct CommonSignalSpec
{
    uint8_t channel = 0;
    uint16_t id = 0;
    uint8_t start_bit = 0;
    uint8_t bit_length = 0;
    uint8_t endian = 0;
    uint8_t is_signed = 0;
    float scale = 1.0f;
    float offset = 0.0f;
    char label[kCommonSignalLabelLen] = {};
};

class CommonSignalStore
{
public:
    void begin();
    size_t count() const;
    const CommonSignalSpec *entries() const;
    bool replaceAll(const CommonSignalSpec *entries, size_t count, bool save_now = true);
    bool loadFromBlobForTest(const CommonSignalSpec *entries, size_t count);

private:
    bool loadEntries(const CommonSignalSpec *entries, size_t count);
    void persist();

    CommonSignalSpec entries_[kMaxCommonSignals] = {};
    size_t count_ = 0;
};
