#pragma once
#include <cstddef>
#include <cstdint>

// WebSocket 二进制协议的消息类型。
// 最小化后只有两类服务器推送：ID 增量表与总线统计；浏览器不再向设备发命令。
enum WsMsgType : uint8_t
{
    WS_MSG_FRAME_DELTA = 0x01,
    WS_MSG_BUS_STATS = 0x02,
};

#pragma pack(push, 1)
// 单个 (channel,id) 的最新状态快照，紧跟在消息头 [type,count] 之后。
// 布局必须与 data/analyzer/app.js 的 parseDelta() 字节偏移完全一致（当前每条 45 字节）。
struct WsFrameRecord
{
    uint8_t channel;
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t last_rx_ms;
    uint16_t byte_age_ms[8];
    uint32_t rx_count;
    uint16_t last_delta_ms;
    uint16_t period_ms;
    uint16_t jitter_ms;
    uint16_t change_score;
    uint8_t flags;
};

// 总线统计快照，紧跟在消息头 [type] 之后。
// rx_err_* / bus_off_* 暂未填充，保留字段用于后续扩展并稳定前端 dropped 偏移。
struct WsBusStats
{
    uint16_t fps_a;
    uint16_t fps_b;
    uint16_t load_a_x10;
    uint16_t load_b_x10;
    uint32_t rx_err_a;
    uint32_t rx_err_b;
    uint8_t bus_off_a;
    uint8_t bus_off_b;
    uint32_t dropped;
};
#pragma pack(pop)

// builder 返回实际写入字节数；cap 不足以容纳最小消息头时返回 0。
// frame delta 会按 cap 自动裁剪 count，调用方可用固定缓冲批量发送。
size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count);
size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats);
