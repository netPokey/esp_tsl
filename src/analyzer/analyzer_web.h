#pragma once
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"

void analyzerWebSetContext(FrameQueue *queue, IdTable *table);
void analyzerWebBegin();
void analyzerWebLoop();
