#pragma once

#include <cstddef>
#include <cstdint>

#include "analyzer/signal_window.h"

enum class HintKind : uint8_t
{
    Mux = 1,
    Counter = 2,
    Checksum = 3,
};

struct HintBitRange
{
    uint8_t start_bit = 0;
    uint8_t bit_length = 0;
};

struct SignalHint
{
    HintKind kind = HintKind::Mux;
    HintBitRange bit_range;
    float confidence = 0.0f;
    char evidence[64] = {};
};

size_t signalFindHints(const RawSamplePoint *samples, size_t count, SignalHint *out, size_t cap);
