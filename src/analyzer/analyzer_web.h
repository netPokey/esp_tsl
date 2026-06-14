#pragma once
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/label_store.h"
#include "analyzer/pretrigger_buffer.h"
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

#if !defined(ARDUINO)
inline bool analyzerWebParseChannelForTest(const char *text, uint8_t &channel)
{
    return analyzerWebParseChannelToken(text, channel);
}

inline bool analyzerWebParseSlotForTest(const char *text, SnapshotSlot &slot)
{
    return analyzerWebParseSlotToken(text, slot);
}
#endif

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels);
void analyzerWebBegin();
void analyzerWebLoop();
