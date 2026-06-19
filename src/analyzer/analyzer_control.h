#pragma once
#include <cstdint>
#include "can_helpers.h"

// 跨模块共享的通道在线状态。
// setup() 在驱动 init 后写入，Web 状态端点读取；Shared<bool> 封装了目标平台上的原子/同步细节。
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
