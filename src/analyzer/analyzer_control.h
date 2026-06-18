#pragma once
#include <cstdint>
#include "can_helpers.h"

inline Shared<bool> &analyzerChannelOnlineStorage(uint8_t channel)
{
    static Shared<bool> onlineA(false);
    static Shared<bool> onlineB(false);
    return channel == 0 ? onlineA : onlineB;
}

inline void markAnalyzerChannelOnline(uint8_t channel, bool online)
{
    storeSharedBool(analyzerChannelOnlineStorage(channel), online);
}

inline bool isAnalyzerChannelOnline(uint8_t channel)
{
    return loadSharedBool(analyzerChannelOnlineStorage(channel));
}
