#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"
#include "recorder.h"

size_t recordAscHeader(char *out, size_t cap);
size_t recordAscFooter(char *out, size_t cap);
size_t recordAscLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us);

struct RecordAscCursor
{
    size_t frame_index = 0;
    bool header_sent = false;
    bool footer_sent = false;
    uint64_t base_ts_us = 0;
    bool base_set = false;
};

size_t recordAscFill(char *buf, size_t maxLen, const Recorder &rec, size_t total, RecordAscCursor &cursor);
