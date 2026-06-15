#pragma once
#include <cstdint>

// 原始 CAN payload 信号字节序。
enum class SignalEndian : uint8_t
{
    Intel = 0,
    Motorola = 1,
};

// 单个信号的位段与缩放规格。
struct SignalSpec
{
    uint8_t channel = 0;
    uint16_t id = 0;
    uint8_t start_bit = 0;
    uint8_t bit_length = 0;
    SignalEndian endian = SignalEndian::Intel;
    bool is_signed = false;
    float scale = 1.0f;
    float offset = 0.0f;
    bool has_mux = false;
    uint8_t mux_start_bit = 0;
    uint8_t mux_bit_length = 0;
    uint32_t mux_value = 0;
};

// 单次解码结果。
struct SignalDecodeResult
{
    bool valid = false;
    int64_t raw = 0;
    float physical = 0.0f;
};

uint64_t signalExtractUnsigned(const uint8_t data[8], uint8_t start_bit, uint8_t bit_length, SignalEndian endian);
bool signalMuxMatches(const uint8_t data[8], const SignalSpec &spec);
SignalDecodeResult signalDecode(const uint8_t data[8], const SignalSpec &spec);
