#include "record_format.h"
#include <cstdio>
#include <cstring>

static char channelChar(uint8_t channel)
{
    if (channel == 0) return 'A';
    if (channel == 1) return 'B';
    return '?';
}

size_t recordCsvHeader(char *out, size_t cap)
{
    const char *h = "time_s,channel,id,dlc,data\n";
    size_t len = strlen(h);
    if (cap < len + 1)
        return 0;
    memcpy(out, h, len + 1);
    return len;
}

size_t recordCsvLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us)
{
    char tmp[128];
    uint64_t rel = frame.ts_us >= base_ts_us ? frame.ts_us - base_ts_us : 0;
    double t = static_cast<double>(rel) / 1000000.0;
    int prefix = snprintf(tmp, sizeof(tmp), "%.6f,%c,0x%03X,%u,",
                          t, channelChar(frame.channel),
                          static_cast<unsigned>(frame.id),
                          static_cast<unsigned>(frame.dlc));
    if (prefix < 0)
        return 0;
    size_t pos = static_cast<size_t>(prefix);
    uint8_t n = frame.dlc > 8 ? 8 : frame.dlc;
    for (uint8_t i = 0; i < n; ++i)
    {
        int w = snprintf(tmp + pos, sizeof(tmp) - pos, "%02X", frame.data[i]);
        if (w < 0) return 0;
        pos += static_cast<size_t>(w);
    }
    if (pos + 2 > sizeof(tmp))
        return 0;
    tmp[pos++] = '\n';
    tmp[pos] = '\0';
    if (cap < pos + 1)
        return 0;
    memcpy(out, tmp, pos + 1);
    return pos;
}

size_t recordCsvFill(char *buf, size_t maxLen, const Recorder &rec, size_t total, RecordCsvCursor &cursor)
{
    size_t written = 0;
    if (!cursor.header_sent)
    {
        size_t h = recordCsvHeader(buf, maxLen);
        if (h == 0)
            return 0;
        cursor.header_sent = true;
        written = h;
    }
    while (cursor.frame_index < total)
    {
        CapturedFrame f;
        if (rec.collect(&f, 1, cursor.frame_index) == 0)
            break;
        // base 取最旧帧(序号 0)的时间戳；前提是 cursor 从 frame_index==0 起。
        if (cursor.frame_index == 0 && !cursor.base_set)
        {
            cursor.base_ts_us = f.ts_us;
            cursor.base_set = true;
        }
        size_t w = recordCsvLine(buf + written, maxLen - written, f, cursor.base_ts_us);
        if (w == 0)
            break;
        written += w;
        ++cursor.frame_index;
    }
    return written;
}
