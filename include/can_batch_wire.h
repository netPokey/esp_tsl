#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "can_batch_uploader_types.h"

namespace can_batch_wire
{
constexpr size_t kHeaderBytes = 24;
constexpr size_t kRecordBytes = 24;
constexpr size_t kMaxDeviceIdBytes = 63;
constexpr size_t kMaxBatchBytes = kHeaderBytes + kMaxDeviceIdBytes + CAN_UPLOAD_BATCH_SIZE * kRecordBytes;

inline void putU16(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

inline void putU32(uint8_t *out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

inline size_t encode(uint8_t *out, size_t capacity, const char *deviceId,
                     uint32_t uptimeMs, uint32_t batchSeq,
                     const CanUploadEntry *entries, uint16_t count)
{
    const size_t deviceLen = deviceId ? strnlen(deviceId, kMaxDeviceIdBytes + 1) : 0;
    if (!out || !entries || count == 0 || count > CAN_UPLOAD_BATCH_SIZE ||
        deviceLen == 0 || deviceLen > kMaxDeviceIdBytes)
        return 0;
    for (size_t i = 0; i < deviceLen; ++i)
        if (static_cast<unsigned char>(deviceId[i]) < 0x21 || static_cast<unsigned char>(deviceId[i]) > 0x7e)
            return 0;

    const size_t headerLen = kHeaderBytes + deviceLen;
    const size_t total = headerLen + static_cast<size_t>(count) * kRecordBytes;
    if (capacity < total)
        return 0;
    memset(out, 0, total);
    memcpy(out, "CBIN", 4);
    out[4] = 1;
    putU16(out + 6, static_cast<uint16_t>(headerLen));
    putU16(out + 8, static_cast<uint16_t>(kRecordBytes));
    putU16(out + 10, count);
    putU32(out + 12, batchSeq);
    putU32(out + 16, uptimeMs);
    out[20] = static_cast<uint8_t>(deviceLen);
    memcpy(out + kHeaderBytes, deviceId, deviceLen);

    uint8_t *record = out + headerLen;
    for (uint16_t i = 0; i < count; ++i, record += kRecordBytes)
    {
        const CanUploadEntry &entry = entries[i];
        if ((entry.bus != CanBusId::A && entry.bus != CanBusId::B) || entry.id > 0x7ff || entry.dlc > 8)
            return 0;
        putU32(record, entry.seq);
        putU32(record + 4, entry.timestampMs);
        putU32(record + 8, entry.id);
        record[12] = entry.bus == CanBusId::A ? 0 : 1;
        record[13] = entry.dlc;
        memcpy(record + 16, entry.data, entry.dlc);
    }
    return total;
}
}  // namespace can_batch_wire
