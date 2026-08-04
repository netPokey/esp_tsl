#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t CAN_UPLOAD_MAX_URL_LENGTH = 191;
static constexpr const char *CAN_UPLOAD_DEFAULT_URL = "http://1.116.182.175:48601/can/batch";

enum class CanUploadMode : uint8_t
{
    Off = 0,
    All = 1,
    Critical = 2,
};
