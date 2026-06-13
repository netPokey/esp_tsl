#pragma once
#include <cstddef>
#include <cstdint>

enum WsMsgType : uint8_t
{
    WS_MSG_FRAME_DELTA = 0x01,
    WS_MSG_BUS_STATS = 0x02,
    WS_MSG_DIFF = 0x03,
};

#pragma pack(push, 1)
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

size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count);
size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats);
