#include "analyzer/label_store.h"
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
#endif

void LabelStore::begin()
{
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin("analyzer", true))
        return;

    const size_t len = prefs.getBytesLength("labels");
    if (len > 0 && len % sizeof(LabelEntry) == 0)
    {
        const size_t count = len / sizeof(LabelEntry);
        if (count <= kMaxLabels)
        {
            LabelEntry persisted[kMaxLabels] = {};
            if (prefs.getBytes("labels", persisted, len) == len)
                loadEntries(persisted, count);
        }
    }
    prefs.end();
#else
    count_ = 0;
    memset(entries_, 0, sizeof(entries_));
#endif
}

#if !defined(ARDUINO)
bool LabelStore::loadFromBlobForTest(const LabelEntry *entries, size_t count)
{
    return loadEntries(entries, count);
}
#endif

bool LabelStore::loadEntries(const LabelEntry *entries, size_t count)
{
    if (count > kMaxLabels || (count > 0 && !entries))
        return false;

    LabelEntry sanitized[kMaxLabels] = {};

    for (size_t i = 0; i < count; ++i)
    {
        if (entries[i].channel > 1)
            return false;

        LabelEntry entry = entries[i];
        entry.text[kLabelTextLen - 1] = '\0';
        if (entry.text[0] == '\0')
            return false;

        sanitized[i] = entry;
    }

    memset(entries_, 0, sizeof(entries_));
    memcpy(entries_, sanitized, sizeof(sanitized));
    count_ = count;
    return true;
}

bool LabelStore::upsert(uint8_t channel, uint16_t id, const char *text)
{
    if (channel > 1)
        return false;

    if (!text || text[0] == '\0')
        return remove(channel, id);

    int index = find(channel, id);
    if (index < 0)
    {
        if (count_ >= kMaxLabels)
            return false;
        index = static_cast<int>(count_++);
        entries_[index].channel = channel;
        entries_[index].id = id;
    }

    memset(entries_[index].text, 0, kLabelTextLen);
    strncpy(entries_[index].text, text, kLabelTextLen - 1);
    persist();
    return true;
}

bool LabelStore::remove(uint8_t channel, uint16_t id)
{
    const int index = find(channel, id);
    if (index < 0)
        return false;

    for (size_t i = static_cast<size_t>(index); i + 1 < count_; ++i)
        entries_[i] = entries_[i + 1];

    --count_;
    entries_[count_] = LabelEntry{};
    persist();
    return true;
}

const LabelEntry *LabelStore::entries() const
{
    return entries_;
}

size_t LabelStore::count() const
{
    return count_;
}

int LabelStore::find(uint8_t channel, uint16_t id) const
{
    for (size_t i = 0; i < count_; ++i)
    {
        if (entries_[i].channel == channel && entries_[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

void LabelStore::persist()
{
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin("analyzer", false))
        return;
    prefs.putBytes("labels", entries_, count_ * sizeof(LabelEntry));
    prefs.end();
#endif
}
