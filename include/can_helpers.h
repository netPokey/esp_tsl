#pragma once

#include "can_frame_types.h"
#include "shared_types.h"

inline Shared<bool> &forceFSDRuntimeStorage()
{
    static Shared<bool> value(false);
    return value;
}

inline bool loadSharedBool(const Shared<bool> &value)
{
#ifdef NATIVE_BUILD
    return value;
#else
    return value.load();
#endif
}

inline void storeSharedBool(Shared<bool> &target, bool value)
{
#ifdef NATIVE_BUILD
    target = value;
#else
    target.store(value);
#endif
}

inline bool isForceFSDEnabled()
{
    return loadSharedBool(forceFSDRuntimeStorage());
}

inline void setForceFSDEnabled(bool enabled)
{
    storeSharedBool(forceFSDRuntimeStorage(), enabled);
}

inline uint8_t readMuxID(const CanFrame &frame)
{
    return frame.data[0] & 0x07;
}

inline bool isFSDSelectedInUI(const CanFrame &frame)
{
#if defined(FORCE_FSD)
    (void)frame;
    return true;
#else
    if (isForceFSDEnabled())
        return true;
    return (frame.data[4] >> 6) & 0x01;
#endif
}

inline void setSpeedProfileV12V13(CanFrame &frame, int profile)
{
    frame.data[6] &= static_cast<uint8_t>(~0x06);
    frame.data[6] |= static_cast<uint8_t>((profile & 0x03) << 1);
}

inline void setBit(CanFrame &frame, int bit, bool value)
{
    if (bit < 0 || bit >= 64)
        return;

    const int byteIndex = bit / 8;
    const int bitIndex = bit % 8;
    const uint8_t mask = static_cast<uint8_t>(1U << bitIndex);

    if (value)
        frame.data[byteIndex] |= mask;
    else
        frame.data[byteIndex] &= static_cast<uint8_t>(~mask);
}