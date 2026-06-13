#pragma once
#include <cstdint>
#include "can_helpers.h"

inline Shared<bool> &analyzerChannelTxStorage(uint8_t channel)
{
    static Shared<bool> txA(false);
    static Shared<bool> txB(false);
    return channel == 0 ? txA : txB;
}

inline Shared<bool> &analyzerChannelOnlineStorage(uint8_t channel)
{
    static Shared<bool> onlineA(false);
    static Shared<bool> onlineB(false);
    return channel == 0 ? onlineA : onlineB;
}

inline void setAnalyzerChannelTxEnabled(uint8_t channel, bool enabled)
{
    storeSharedBool(analyzerChannelTxStorage(channel), enabled);
}

inline bool isAnalyzerChannelTxEnabled(uint8_t channel)
{
    return loadSharedBool(analyzerChannelTxStorage(channel));
}

inline void markAnalyzerChannelOnline(uint8_t channel, bool online)
{
    storeSharedBool(analyzerChannelOnlineStorage(channel), online);
}

inline bool isAnalyzerChannelOnline(uint8_t channel)
{
    return loadSharedBool(analyzerChannelOnlineStorage(channel));
}

inline bool shouldAllowAnalyzerChannelTx(uint8_t channel)
{
    return isCanTxEnabled() && isAnalyzerChannelTxEnabled(channel) && isAnalyzerChannelOnline(channel);
}
