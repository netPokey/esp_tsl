#include "analyzer/ws_protocol.h"
#include <cstring>

// 线协议尺寸是前端 parseDelta()/parseStats() 的硬编码偏移依赖；这里编译期锁死，
// 一旦有人改字段或对齐导致尺寸漂移，立刻在编译期暴露而不是上线后解析错位。
static_assert(sizeof(WsFrameRecord) == 45, "WsFrameRecord 必须是 45 字节，与 app.js parseDelta 一致");
static_assert(sizeof(WsBusStats) == 22, "WsBusStats 必须是 22 字节，与 app.js parseStats 一致");

// 输出格式：[0]=WS_MSG_FRAME_DELTA, [1]=实际记录数, 后续为连续 WsFrameRecord。
// 若缓冲不够 count 条，自动裁剪到能放下的条数，避免调用方重复计算边界。
size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count)
{
    if (cap < 2)
        return 0;

    const size_t recSize = sizeof(WsFrameRecord);
    size_t maxByCap = (cap - 2) / recSize;
    if (maxByCap > count)
        maxByCap = count;

    buf[0] = WS_MSG_FRAME_DELTA;
    buf[1] = static_cast<uint8_t>(maxByCap);
    memcpy(buf + 2, recs, maxByCap * recSize);
    return 2 + maxByCap * recSize;
}

// 输出格式：[0]=WS_MSG_BUS_STATS, 后续直接拷贝 packed WsBusStats。
size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats)
{
    if (cap < 1 + sizeof(WsBusStats))
        return 0;

    buf[0] = WS_MSG_BUS_STATS;
    memcpy(buf + 1, &stats, sizeof(WsBusStats));
    return 1 + sizeof(WsBusStats);
}