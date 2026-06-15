#include "analyzer/signal_hints.h"

#include <cstdio>
#include <cstring>

#include "analyzer/signal_codec.h"

namespace
{
constexpr size_t kMaxMuxValues = 8;
constexpr float kCounterThreshold = 0.75f;
constexpr float kMuxThreshold = 0.60f;

struct CounterCandidate
{
    bool found = false;
    uint8_t start_bit = 0;
    uint8_t bit_length = 0;
    float confidence = 0.0f;
};

struct CounterCandidates
{
    CounterCandidate items[3] = {};
    size_t count = 0;

    const CounterCandidate *best() const
    {
        return count > 0 ? &items[0] : nullptr;
    }
};

struct GroupStats
{
    bool seen = false;
    uint8_t value = 0;
    uint8_t first_payload[8] = {};
    uint8_t diff_mask[8] = {};
    size_t sample_count = 0;
};

float clampConfidence(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

void setEvidence(char *dst, size_t size, const char *fmt, unsigned a = 0, unsigned b = 0, unsigned c = 0)
{
    if (!dst || size == 0)
        return;

    const int written = std::snprintf(dst, size, fmt, a, b, c);
    if (written < 0 || static_cast<size_t>(written) >= size)
        dst[size - 1] = '\0';
}

uint64_t extractIntel(const RawSamplePoint &sample, uint8_t start_bit, uint8_t bit_length)
{
    return signalExtractUnsigned(sample.data, start_bit, bit_length, SignalEndian::Intel);
}

CounterCandidates findCounterCandidates(const RawSamplePoint *samples, size_t count)
{
    CounterCandidates candidates;
    const uint8_t widths[] = {16, 8, 4};

    for (uint8_t width : widths)
    {
        CounterCandidate best_for_width;
        const uint8_t limit = static_cast<uint8_t>(64 - width);
        const uint32_t modulo = width == 16 ? 65536u : (1u << width);

        for (uint8_t start = 0; start <= limit; start += 4)
        {
            size_t matches = 0;
            for (size_t i = 1; i < count; ++i)
            {
                const uint32_t prev = static_cast<uint32_t>(extractIntel(samples[i - 1], start, width));
                const uint32_t cur = static_cast<uint32_t>(extractIntel(samples[i], start, width));
                if (((prev + 1u) % modulo) == cur)
                    ++matches;
            }

            const float confidence = clampConfidence(static_cast<float>(matches) / static_cast<float>(count - 1));
            if (confidence < kCounterThreshold)
                continue;

            if (!best_for_width.found || confidence > best_for_width.confidence ||
                (confidence == best_for_width.confidence && start < best_for_width.start_bit))
            {
                best_for_width.found = true;
                best_for_width.start_bit = start;
                best_for_width.bit_length = width;
                best_for_width.confidence = confidence;
            }
        }

        if (best_for_width.found && candidates.count < (sizeof(candidates.items) / sizeof(candidates.items[0])))
            candidates.items[candidates.count++] = best_for_width;
    }

    return candidates;
}

bool collectGroups(const RawSamplePoint *samples, size_t count, uint8_t start_bit, uint8_t bit_length,
                   GroupStats *groups, size_t &group_count)
{
    group_count = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t value = static_cast<uint8_t>(extractIntel(samples[i], start_bit, bit_length));
        size_t group_index = group_count;
        for (size_t g = 0; g < group_count; ++g)
        {
            if (groups[g].value == value)
            {
                group_index = g;
                break;
            }
        }

        if (group_index == group_count)
        {
            if (group_count >= kMaxMuxValues)
                return false;
            GroupStats &group = groups[group_count++];
            group = GroupStats{};
            group.seen = true;
            group.value = value;
            std::memcpy(group.first_payload, samples[i].data, sizeof(group.first_payload));
        }

        GroupStats &group = groups[group_index];
        for (size_t byte_index = 0; byte_index < sizeof(group.first_payload); ++byte_index)
        {
            if (group.first_payload[byte_index] != samples[i].data[byte_index])
                group.diff_mask[byte_index] = 1;
        }
        ++group.sample_count;
    }
    return true;
}

bool overlapsCounter(uint8_t start_bit, uint8_t bit_length, const CounterCandidate &counter)
{
    if (!counter.found)
        return false;

    const uint8_t end = start_bit + bit_length;
    const uint8_t counter_end = counter.start_bit + counter.bit_length;
    return !(end <= counter.start_bit || counter_end <= start_bit);
}

bool overlapsAnyCounter(uint8_t start_bit, uint8_t bit_length, const CounterCandidates &counters)
{
    for (size_t i = 0; i < counters.count; ++i)
    {
        if (overlapsCounter(start_bit, bit_length, counters.items[i]))
            return true;
    }
    return false;
}

bool findBestMux(const RawSamplePoint *samples, size_t count, const CounterCandidates &counters,
                 SignalHint &hint)
{
    const uint8_t widths[] = {4, 8};
    float best_confidence = 0.0f;
    bool found = false;

    for (uint8_t width : widths)
    {
        const uint8_t limit = static_cast<uint8_t>(64 - width);
        for (uint8_t start = 0; start <= limit; start += 4)
        {
            if (overlapsAnyCounter(start, width, counters))
                continue;

            GroupStats groups[kMaxMuxValues] = {};
            size_t group_count = 0;
            if (!collectGroups(samples, count, start, width, groups, group_count))
                continue;
            if (group_count < 2 || group_count > 4)
                continue;

            bool enough_samples = true;
            for (size_t g = 0; g < group_count; ++g)
            {
                if (groups[g].sample_count < 2)
                {
                    enough_samples = false;
                    break;
                }
            }
            if (!enough_samples)
                continue;

            size_t stable_diff_bytes = 0;
            for (size_t byte_index = 0; byte_index < 8; ++byte_index)
            {
                const bool byte_changes_within_group = [&]() {
                    for (size_t g = 0; g < group_count; ++g)
                    {
                        if (groups[g].diff_mask[byte_index] != 0)
                            return true;
                    }
                    return false;
                }();
                if (byte_changes_within_group)
                    continue;

                bool differs_across_groups = false;
                for (size_t g = 1; g < group_count; ++g)
                {
                    if (groups[g].first_payload[byte_index] != groups[0].first_payload[byte_index])
                    {
                        differs_across_groups = true;
                        break;
                    }
                }
                if (differs_across_groups)
                    ++stable_diff_bytes;
            }

            if (stable_diff_bytes == 0)
                continue;

            const float coverage = static_cast<float>(stable_diff_bytes) / 8.0f;
            const float compactness = 1.0f - (static_cast<float>(group_count - 2) * 0.15f);
            const float confidence = clampConfidence(coverage * compactness + 0.40f);
            if (confidence < kMuxThreshold || confidence <= best_confidence)
                continue;

            found = true;
            best_confidence = confidence;
            hint.kind = HintKind::Mux;
            hint.bit_range.start_bit = start;
            hint.bit_range.bit_length = width;
            hint.confidence = confidence;
            setEvidence(hint.evidence, sizeof(hint.evidence), "selector groups=%u bytes=%u",
                        static_cast<unsigned>(group_count), static_cast<unsigned>(stable_diff_bytes));
        }
    }

    return found;
}

bool appendHint(SignalHint *out, size_t cap, size_t &written, const SignalHint &hint)
{
    if (written >= cap)
        return false;
    out[written++] = hint;
    return true;
}

bool valueHasHighDiversity(const RawSamplePoint *samples, size_t count, uint8_t start_bit, uint8_t bit_length)
{
    const size_t max_unique = bit_length <= 4 ? 16 : 32;
    uint64_t seen[32] = {};
    size_t unique = 0;

    for (size_t i = 0; i < count; ++i)
    {
        const uint64_t value = extractIntel(samples[i], start_bit, bit_length);
        bool known = false;
        for (size_t j = 0; j < unique; ++j)
        {
            if (seen[j] == value)
            {
                known = true;
                break;
            }
        }
        if (known)
            continue;
        if (unique < max_unique)
            seen[unique] = value;
        ++unique;
    }

    const float ratio = static_cast<float>(unique) / static_cast<float>(count);
    return ratio >= 0.66f;
}

bool highNibbleIsConstant(const RawSamplePoint *samples, size_t count, uint8_t byte_index)
{
    const uint8_t high_nibble = samples[0].data[byte_index] & 0xF0u;
    for (size_t i = 1; i < count; ++i)
    {
        if ((samples[i].data[byte_index] & 0xF0u) != high_nibble)
            return false;
    }
    return true;
}

bool lowNibbleHasHighDiversity(const RawSamplePoint *samples, size_t count, uint8_t byte_index)
{
    bool seen[16] = {};
    size_t unique = 0;

    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t value = samples[i].data[byte_index] & 0x0Fu;
        if (seen[value])
            continue;
        seen[value] = true;
        ++unique;
    }

    const float ratio = static_cast<float>(unique) / static_cast<float>(count);
    return ratio >= 0.66f;
}

bool isSameRange(const HintBitRange &range, uint8_t start_bit, uint8_t bit_length)
{
    return range.start_bit == start_bit && range.bit_length == bit_length;
}
}

size_t signalFindHints(const RawSamplePoint *samples, size_t count, SignalHint *out, size_t cap)
{
    if (!samples || !out || cap == 0 || count < 4)
        return 0;

    size_t written = 0;
    const CounterCandidates counters = findCounterCandidates(samples, count);
    for (size_t i = 0; i < counters.count && written < cap; ++i)
    {
        const CounterCandidate &counter = counters.items[i];
        SignalHint hint{};
        hint.kind = HintKind::Counter;
        hint.bit_range.start_bit = counter.start_bit;
        hint.bit_range.bit_length = counter.bit_length;
        hint.confidence = clampConfidence(counter.confidence);
        setEvidence(hint.evidence, sizeof(hint.evidence), "+1 modulo matches=%u/%u",
                    static_cast<unsigned>((count - 1) * hint.confidence + 0.5f),
                    static_cast<unsigned>(count - 1));
        appendHint(out, cap, written, hint);
    }

    SignalHint mux_hint{};
    if (findBestMux(samples, count, counters, mux_hint))
        appendHint(out, cap, written, mux_hint);

    HintBitRange tail_ranges[2] = {};
    tail_ranges[0].start_bit = 60;
    tail_ranges[0].bit_length = 4;
    tail_ranges[1].start_bit = 56;
    tail_ranges[1].bit_length = 8;

    for (const HintBitRange &range : tail_ranges)
    {
        if (overlapsAnyCounter(range.start_bit, range.bit_length, counters) ||
            (written > 0 && out[written - 1].kind == HintKind::Mux &&
             isSameRange(range, out[written - 1].bit_range.start_bit, out[written - 1].bit_range.bit_length)))
            continue;

        if (range.start_bit == 60)
        {
            if (!highNibbleIsConstant(samples, count, 7) || !lowNibbleHasHighDiversity(samples, count, 7))
                continue;
        }
        else
        {
            if (!valueHasHighDiversity(samples, count, range.start_bit, range.bit_length))
                continue;

            if (highNibbleIsConstant(samples, count, 7) && lowNibbleHasHighDiversity(samples, count, 7))
                continue;
        }

        SignalHint hint{};
        hint.kind = HintKind::Checksum;
        hint.bit_range = range;
        hint.confidence = range.bit_length == 8 ? 0.70f : 0.60f;
        setEvidence(hint.evidence, sizeof(hint.evidence), "tail field is a candidate checksum");
        appendHint(out, cap, written, hint);
        break;
    }

    for (size_t i = 0; i < written; ++i)
        out[i].confidence = clampConfidence(out[i].confidence);
    return written;
}
