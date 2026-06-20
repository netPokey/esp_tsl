#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"

// AsyncWebServer 分片 body 边界校验：total 不能超过固定缓冲，index/len 必须落在 total 内。
// 这些 helper 放头文件内，方便 native 单测覆盖，不把 Arduino/AsyncWebServer 拉进测试。
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

// Web 层不创建核心对象，只保存组装点传入的队列/表/统计指针。
void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats);
// 注册 HTTP/WS 路由并启动服务器。
void analyzerWebBegin();
// 主循环调用：消费队列、处理 pending WiFi/电源动作、按节奏推送 WS。
void analyzerWebLoop();

// 设备日志透传：把诊断/串口消息存入环形缓冲，供 /api/log 在网页查看
// (设备在车上接 CAN 时通常无法同时连 USB 读串口)。Printf 同时 tee 到 Serial。
void analyzerWebLogInit();
void analyzerWebLogPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));