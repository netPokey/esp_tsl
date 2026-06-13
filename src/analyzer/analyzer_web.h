#pragma once
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats);
void analyzerWebBegin();
void analyzerWebLoop();
