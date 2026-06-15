#include "analyzer/signal_codec.h"

namespace
{
bool signalRangeIsValid(uint8_t start_bit, uint8_t bit_length)
{
    return bit_length >= 1 && bit_length <= 64 &&
           static_cast<uint16_t>(start_bit) + static_cast<uint16_t>(bit_length) <= 64;
}

int64_t signalSignExtend(uint64_t raw, uint8_t bit_length)
{
    if (bit_length == 64)
        return static_cast<int64_t>(raw);

    const uint64_t sign_bit = 1ULL << (bit_length - 1);
    if ((raw & sign_bit) == 0)
        return static_cast<int64_t>(raw);

    const uint64_t extended = raw | (~0ULL << bit_length);
    return static_cast<int64_t>(extended);
}
}

uint64_t signalExtractUnsigned(const uint8_t data[8], uint8_t start_bit, uint8_t bit_length, SignalEndian endian)
{
    if (!signalRangeIsValid(start_bit, bit_length))
        return 0;

    if (endian == SignalEndian::Intel)
    {
        uint64_t raw = 0;
        for (uint8_t i = 0; i < bit_length; ++i)
        {
            const uint8_t bit_index = static_cast<uint8_t>(start_bit + i);
            const uint8_t byte_index = bit_index / 8;
            const uint8_t bit_in_byte = bit_index % 8;
            const uint64_t bit = (static_cast<uint64_t>(data[byte_index]) >> bit_in_byte) & 0x1ULL;
            raw |= (bit << i);
        }
        return raw;
    }

    uint64_t raw = 0;
    for (uint8_t i = 0; i < bit_length; ++i)
    {
        const uint8_t bit_index = static_cast<uint8_t>(start_bit + i);
        const uint8_t byte_index = bit_index / 8;
        const uint8_t bit_in_byte = static_cast<uint8_t>(7 - (bit_index % 8));
        const uint64_t bit = (static_cast<uint64_t>(data[byte_index]) >> bit_in_byte) & 0x1ULL;
        raw = (raw << 1) | bit;
    }
    return raw;
}

bool signalMuxMatches(const uint8_t data[8], const SignalSpec &spec)
{
    if (!spec.has_mux)
        return true;

    if (!signalRangeIsValid(spec.mux_start_bit, spec.mux_bit_length))
        return false;

    const uint64_t mux = signalExtractUnsigned(data, spec.mux_start_bit, spec.mux_bit_length, spec.endian);
    return mux == spec.mux_value;
}

SignalDecodeResult signalDecode(const uint8_t data[8], const SignalSpec &spec)
{
    SignalDecodeResult result;
    if (!signalRangeIsValid(spec.start_bit, spec.bit_length))
        return result;

    if (!signalMuxMatches(data, spec))
        return result;

    const uint64_t raw = signalExtractUnsigned(data, spec.start_bit, spec.bit_length, spec.endian);
    result.raw = spec.is_signed ? signalSignExtend(raw, spec.bit_length) : static_cast<int64_t>(raw);
    result.physical = static_cast<float>(result.raw) * spec.scale + spec.offset;
    result.valid = true;
    return result;
}
