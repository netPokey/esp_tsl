#pragma once

#include <Arduino.h>
#include <cstring>

#include "can_frame_types.h"

enum class CanBusId : uint8_t
{
    A,
    B,
    Unknown,
};

inline const char *canBusName(CanBusId bus)
{
    switch (bus)
    {
    case CanBusId::A:
        return "CAN_A";
    case CanBusId::B:
        return "CAN_B";
    default:
        return "UNKNOWN";
    }
}

struct CanBusRuntime
{
    const char *name = "UNKNOWN";
    bool online = false;
    uint32_t rxFrames = 0;
    uint32_t txFrames = 0;
    uint32_t lastId = 0;
    uint8_t lastDlc = 0;
    uint8_t lastData[8] = {};
    unsigned long lastSeenMs = 0;
    unsigned long lastInjectedMs = 0;
};

struct DualCanRuntime
{
    unsigned long bootMs = 0;
    uint32_t totalRxFrames = 0;
    uint32_t totalTxFrames = 0;
    CanBusRuntime busA;
    CanBusRuntime busB;

    void begin()
    {
        bootMs = millis();
        busA.name = "CAN_A";
        busB.name = "CAN_B";
    }

    CanBusRuntime &bus(CanBusId id)
    {
        return id == CanBusId::A ? busA : busB;
    }

    const CanBusRuntime &bus(CanBusId id) const
    {
        return id == CanBusId::A ? busA : busB;
    }

    void markOnline(CanBusId id, bool value)
    {
        bus(id).online = value;
    }

    void noteRx(CanBusId id, const CanFrame &frame)
    {
        totalRxFrames++;
        CanBusRuntime &target = bus(id);
        target.online = true;
        target.rxFrames++;
        target.lastId = frame.id;
        target.lastDlc = frame.dlc <= 8 ? frame.dlc : 8;
        memset(target.lastData, 0, sizeof(target.lastData));
        memcpy(target.lastData, frame.data, target.lastDlc);
        target.lastSeenMs = millis();
    }

    void noteTx(CanBusId id, const CanFrame &frame)
    {
        totalTxFrames++;
        CanBusRuntime &target = bus(id);
        target.online = true;
        target.txFrames++;
        target.lastInjectedMs = millis();
        target.lastId = frame.id;
        target.lastDlc = frame.dlc <= 8 ? frame.dlc : 8;
        memset(target.lastData, 0, sizeof(target.lastData));
        memcpy(target.lastData, frame.data, target.lastDlc);
    }
};