#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"
#include "recorder.h"

// 写 CSV 表头 "time_s,channel,id,dlc,data\n" 到 out（含结尾 NUL 不计入返回值）。
// 返回写入的字符数（不含 NUL）。若 cap 不足则不写入并返回 0。
size_t recordCsvHeader(char *out, size_t cap);

// 写单帧 CSV 行（含结尾 '\n'）。
// time_s = (frame.ts_us - base_ts_us) / 1e6，保留 6 位小数；
// channel: 0->'A' 1->'B' 其它->'?'；id: "0x" + 3 位大写 hex；
// dlc: 十进制；data: dlc 个字节连续大写 hex（无分隔，dlc>8 截断到 8）。
// 返回写入字符数（不含 NUL）；若 cap 不足容纳整行则不写入并返回 0。
size_t recordCsvLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us);

// 下载流式游标：跨多次回调保持进度。
struct RecordCsvCursor
{
    size_t frame_index = 0;   // 下一个待输出帧（旧->新序号）
    bool header_sent = false;
    uint64_t base_ts_us = 0;
    bool base_set = false;
};

// 向 buf（容量 maxLen）追加尽可能多的 CSV 内容并推进 cursor。
// 首次调用先写表头；随后按旧->新从 rec 取帧格式化，半行容不下则停在边界，下次续传。
// total 为下载开始时快照的帧数（避免并发 push 越界）。返回本次写入字节数；返回 0 表示已结束。
size_t recordCsvFill(char *buf, size_t maxLen, const Recorder &rec, size_t total, RecordCsvCursor &cursor);
