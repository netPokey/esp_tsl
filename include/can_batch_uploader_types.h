#pragma once

#include <cstdint>

#include "runtime_state.h"

static constexpr uint16_t CAN_UPLOAD_BATCH_SIZE = 200;
static constexpr size_t CAN_UPLOAD_MAX_DEVICE_ID_LENGTH = 63;

struct CanUploadEntry
{
    uint32_t seq = 0;
    CanBusId bus = CanBusId::Unknown;
    uint32_t timestampMs = 0;
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};
