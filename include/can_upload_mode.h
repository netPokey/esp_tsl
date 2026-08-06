#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t CAN_UPLOAD_MAX_URL_LENGTH = 191;
static constexpr const char *CAN_UPLOAD_DEFAULT_URL = "http://1.116.182.175:48601/can/batch";
static constexpr const char *CAN_UPLOAD_DEFAULT_BINARY_URL = "http://1.116.182.175:48601/can/batch-bin";

constexpr uint8_t CAN_UPLOAD_BUS_A = 0x01;
constexpr uint8_t CAN_UPLOAD_BUS_B = 0x02;
constexpr uint8_t CAN_UPLOAD_BUS_BOTH = CAN_UPLOAD_BUS_A | CAN_UPLOAD_BUS_B;

enum class CanUploadTransport : uint8_t
{
    Json = 0,
    BinaryV1 = 1,
};

enum class CanUploadMode : uint8_t
{
    Off = 0,
    All = 1,
    Critical = 2,
};
