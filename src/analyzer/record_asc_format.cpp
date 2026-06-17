#include "record_asc_format.h"
#include <cstdio>
#include <cstring>

static unsigned ascChannel(uint8_t channel)
{
    if (channel == 0) return 1;
    if (channel == 1) return 2;
    return 0;
}

static size_t copyComplete(char *out, size_t cap, const char *text, size_t len)
{
    if (cap < len)
        return 0;
    memcpy(out, text, len);
    if (cap > len)
        out[len] = '\0';
    return len;
}

size_t recordAscHeader(char *out, size_t cap)
{
    const char *header =
        "date Tue Jun 16 00:00:00.000 2026\n"
        "base hex  timestamps absolute\n"
        "internal events logged\n"
        "Begin Triggerblock\n";
    return copyComplete(out, cap, header, strlen(header));
}

size_t recordAscFooter(char *out, size_t cap)
{
    const char *footer = "End Triggerblock\n";
    return copyComplete(out, cap, footer, strlen(footer));
}

size_t recordAscLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us)
{
    char tmp[160];
    uint64_t rel = frame.ts_us >= base_ts_us ? frame.ts_us - base_ts_us : 0;
    double t = static_cast<double>(rel) / 1000000.0;
    int prefix = snprintf(tmp, sizeof(tmp), "%11.6f %u %03X Rx d %u",
                          t,
                          ascChannel(frame.channel),
                          static_cast<unsigned>(frame.id),
                          static_cast<unsigned>(frame.dlc));
    if (prefix < 0)
        return 0;
    size_t pos = static_cast<size_t>(prefix);
    uint8_t n = frame.dlc > 8 ? 8 : frame.dlc;
    for (uint8_t i = 0; i < n; ++i)
    {
        int w = snprintf(tmp + pos, sizeof(tmp) - pos, " %02X", frame.data[i]);
        if (w < 0)
            return 0;
        pos += static_cast<size_t>(w);
        if (pos >= sizeof(tmp))
            return 0;
    }
    if (pos + 2 > sizeof(tmp))
        return 0;
    tmp[pos++] = '\n';
    tmp[pos] = '\0';
    return copyComplete(out, cap, tmp, pos);
}

size_t recordAscFill(char *buf, size_t maxLen, const Recorder &rec, size_t total, RecordAscCursor &cursor)
{
    size_t written = 0;
    if (!cursor.header_sent)
    {
        size_t h = recordAscHeader(buf, maxLen);
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
        if (cursor.frame_index == 0 && !cursor.base_set)
        {
            cursor.base_ts_us = f.ts_us;
            cursor.base_set = true;
        }
        size_t w = recordAscLine(buf + written, maxLen - written, f, cursor.base_ts_us);
        if (w == 0)
            break;
        written += w;
        ++cursor.frame_index;
    }
    if (cursor.frame_index >= total && !cursor.footer_sent)
    {
        size_t f = recordAscFooter(buf + written, maxLen - written);
        if (f == 0)
            return written;
        cursor.footer_sent = true;
        written += f;
    }
    return written;
}
