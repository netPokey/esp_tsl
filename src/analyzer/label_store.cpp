#include "analyzer/label_store.h"
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
#endif

namespace
{
struct PersistedLabels
{
    size_t count = 0;
    LabelEntry entries[kMaxLabels] = {};
};
}

void LabelStore::begin()
{
    count_ = 0;
    memset(entries_, 0, sizeof(entries_));

#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin("analyzer", true))
        return;

    PersistedLabels persisted{};
    const size_t len = prefs.getBytesLength("labels");
    if (len == sizeof(persisted) && prefs.getBytes("labels", &persisted, sizeof(persisted)) == sizeof(persisted))
    {
        if (persisted.count <= kMaxLabels)
        {
            count_ = persisted.count;
            memcpy(entries_, persisted.entries, sizeof(entries_));
        }
    }
    prefs.end();
#endif
}

bool LabelStore::upsert(uint8_t channel, uint16_t id, const char *text)
{
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
    PersistedLabels persisted{};
    persisted.count = count_;
    memcpy(persisted.entries, entries_, sizeof(entries_));

    Preferences prefs;
    if (!prefs.begin("analyzer", false))
        return;
    prefs.putBytes("labels", &persisted, sizeof(persisted));
    prefs.end();
#endif
}
