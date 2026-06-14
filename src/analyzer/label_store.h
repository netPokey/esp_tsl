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
#if !defined(ARDUINO)
    bool loadFromBlobForTest(const LabelEntry *entries, size_t count);
#endif
    bool upsert(uint8_t channel, uint16_t id, const char *text);
    bool upsert(uint8_t channel, uint16_t id, const char *text, bool save);
    bool remove(uint8_t channel, uint16_t id);
    bool remove(uint8_t channel, uint16_t id, bool save);
    void save();
    const LabelEntry *entries() const;
    size_t count() const;

private:
    int find(uint8_t channel, uint16_t id) const;
    void persist();
    bool loadEntries(const LabelEntry *entries, size_t count);

    LabelEntry entries_[kMaxLabels] = {};
    size_t count_ = 0;
};
