#pragma once
#include <cstddef>
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/common_signal_store.h"
#include "analyzer/label_store.h"
#include "analyzer/pretrigger_buffer.h"
#include "analyzer/recorder.h"
#include "analyzer/signal_window.h"
#include "analyzer/snapshot_store.h"

inline bool analyzerWebParseChannelToken(const char *text, uint8_t &channel)
{
    if (!text || text[1] != '\0')
        return false;
    if (text[0] == 'A' || text[0] == 'a')
    {
        channel = 0;
        return true;
    }
    if (text[0] == 'B' || text[0] == 'b')
    {
        channel = 1;
        return true;
    }
    return false;
}

inline bool analyzerWebParseSlotToken(const char *text, SnapshotSlot &slot)
{
    uint8_t channel = 0;
    if (!analyzerWebParseChannelToken(text, channel))
        return false;
    slot = channel == 1 ? SnapshotSlot::B : SnapshotSlot::A;
    return true;
}

inline uint16_t analyzerWebSampleAgeMs(uint64_t now_us, uint64_t sample_ts_us)
{
    const uint64_t age_us = now_us > sample_ts_us ? now_us - sample_ts_us : 0;
    const uint64_t age_ms = age_us / 1000;
    return age_ms > 65535 ? 65535 : static_cast<uint16_t>(age_ms);
}

inline uint16_t analyzerWebConfidenceX1000(float confidence)
{
    if (confidence <= 0.0f)
        return 0;
    if (confidence >= 1.0f)
        return 1000;
    const float scaled = confidence * 1000.0f + 0.5f;
    return static_cast<uint16_t>(scaled);
}

inline bool analyzerWebBodyChunkIsValid(size_t index, size_t len, size_t total, size_t max_total)
{
    return total <= max_total && index <= total && len <= (total - index);
}

inline bool analyzerWebBodyChunkCompletes(size_t index, size_t len, size_t total)
{
    return index <= total && len <= (total - index) && index + len == total;
}

#if !defined(ARDUINO)
inline bool analyzerWebParseChannelForTest(const char *text, uint8_t &channel)
{
    return analyzerWebParseChannelToken(text, channel);
}

inline bool analyzerWebParseSlotForTest(const char *text, SnapshotSlot &slot)
{
    return analyzerWebParseSlotToken(text, slot);
}

inline uint16_t analyzerWebSampleAgeMsForTest(uint64_t now_us, uint64_t sample_ts_us)
{
    return analyzerWebSampleAgeMs(now_us, sample_ts_us);
}

inline uint16_t analyzerWebConfidenceX1000ForTest(float confidence)
{
    return analyzerWebConfidenceX1000(confidence);
}

inline bool analyzerWebBodyChunkIsValidForTest(size_t index, size_t len, size_t total, size_t max_total)
{
    return analyzerWebBodyChunkIsValid(index, len, total, max_total);
}

inline bool analyzerWebBodyChunkCompletesForTest(size_t index, size_t len, size_t total)
{
    return analyzerWebBodyChunkCompletes(index, len, total);
}
#endif

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels,
                           WatchedSignalWindow *signals, CommonSignalStore *common_signals,
                           Recorder *recorder);
void analyzerWebBegin();
void analyzerWebLoop();
