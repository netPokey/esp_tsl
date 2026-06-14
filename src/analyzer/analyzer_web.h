#pragma once
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/label_store.h"
#include "analyzer/pretrigger_buffer.h"
#include "analyzer/snapshot_store.h"

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels);
void analyzerWebBegin();
void analyzerWebLoop();
