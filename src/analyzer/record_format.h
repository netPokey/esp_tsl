#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"

// 写 CSV 表头 "time_s,channel,id,dlc,data\n" 到 out（含结尾 NUL 不计入返回值）。
// 返回写入的字符数（不含 NUL）。若 cap 不足则写入尽可能多并返回 0。
size_t recordCsvHeader(char *out, size_t cap);

// 写单帧 CSV 行（含结尾 '\n'）。
// time_s = (frame.ts_us - base_ts_us) / 1e6，保留 6 位小数；
// channel: 0->'A' 1->'B' 其它->'?'；id: "0x" + 3 位大写 hex；
// dlc: 十进制；data: dlc 个字节连续大写 hex（无分隔，dlc>8 截断到 8）。
// 返回写入字符数（不含 NUL）；若 cap 不足容纳整行则不写入并返回 0。
size_t recordCsvLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us);
