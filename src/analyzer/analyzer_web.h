#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"

inline bool analyzerWebBodyChunkIsValid(size_t index, size_t len, size_t total, size_t max_total)
{
    return total <= max_total && index <= total && len <= (total - index);
}

inline bool analyzerWebBodyChunkCompletes(size_t index, size_t len, size_t total)
{
    return index <= total && len <= (total - index) && index + len == total;
}

#if defined(NATIVE_BUILD)
inline bool analyzerWebBodyChunkIsValidForTest(size_t index, size_t len, size_t total, size_t max_total)
{
    return analyzerWebBodyChunkIsValid(index, len, total, max_total);
}

inline bool analyzerWebBodyChunkCompletesForTest(size_t index, size_t len, size_t total)
{
    return analyzerWebBodyChunkCompletes(index, len, total);
}
#endif

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats);
void analyzerWebBegin();
void analyzerWebLoop();
