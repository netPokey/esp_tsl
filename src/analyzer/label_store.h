#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kMaxLabels = 64;
constexpr size_t kLabelTextLen = 24;

struct LabelEntry
{
    uint8_t channel = 0;
    uint16_t id = 0;
    char text[kLabelTextLen] = {};
};

class LabelStore
{
public:
    void begin();
    bool upsert(uint8_t channel, uint16_t id, const char *text);
    bool remove(uint8_t channel, uint16_t id);
    const LabelEntry *entries() const;
    size_t count() const;

private:
    int find(uint8_t channel, uint16_t id) const;
    void persist();

    LabelEntry entries_[kMaxLabels] = {};
    size_t count_ = 0;
};
