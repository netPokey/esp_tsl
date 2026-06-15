#include "analyzer/common_signal_store.h"
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
#endif

namespace
{
bool sanitizeSpec(const CommonSignalSpec &input, CommonSignalSpec &output)
{
    if (input.channel > 1)
        return false;
    if (input.id > 0x7FF)
        return false;
    if (input.bit_length < 1 || input.bit_length > 64)
        return false;
    if (static_cast<unsigned int>(input.start_bit) + static_cast<unsigned int>(input.bit_length) > 64)
        return false;
    if (input.endian > 1)
        return false;
    if (input.is_signed > 1)
        return false;

    output = input;
    output.label[kCommonSignalLabelLen - 1] = '\0';
    if (output.label[0] == '\0')
        return false;
    return true;
}
}

void CommonSignalStore::begin()
{
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin("ana", true))
        return;

    const size_t len = prefs.getBytesLength("cs");
    if (len > 0 && len % sizeof(CommonSignalSpec) == 0)
    {
        const size_t count = len / sizeof(CommonSignalSpec);
        if (count <= kMaxCommonSignals)
        {
            CommonSignalSpec persisted[kMaxCommonSignals] = {};
            if (prefs.getBytes("cs", persisted, len) == len)
                loadEntries(persisted, count);
        }
    }
    prefs.end();
#else
    count_ = 0;
    memset(entries_, 0, sizeof(entries_));
#endif
}

size_t CommonSignalStore::count() const
{
    return count_;
}

const CommonSignalSpec *CommonSignalStore::entries() const
{
    return entries_;
}

bool CommonSignalStore::replaceAll(const CommonSignalSpec *entries, size_t count, bool save_now)
{
    if (!loadEntries(entries, count))
        return false;
    if (save_now)
        persist();
    return true;
}

bool CommonSignalStore::loadFromBlobForTest(const CommonSignalSpec *entries, size_t count)
{
    return loadEntries(entries, count);
}

bool CommonSignalStore::loadEntries(const CommonSignalSpec *entries, size_t count)
{
    if (count > kMaxCommonSignals || (count > 0 && !entries))
        return false;

    CommonSignalSpec sanitized[kMaxCommonSignals] = {};
    for (size_t i = 0; i < count; ++i)
    {
        if (!sanitizeSpec(entries[i], sanitized[i]))
            return false;
    }

    memset(entries_, 0, sizeof(entries_));
    memcpy(entries_, sanitized, sizeof(sanitized));
    count_ = count;
    return true;
}

void CommonSignalStore::persist()
{
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin("ana", false))
        return;
    prefs.putBytes("cs", entries_, count_ * sizeof(CommonSignalSpec));
    prefs.end();
#endif
}
