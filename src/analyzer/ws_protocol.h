#pragma once
#include <cstddef>
#include <cstdint>

enum WsMsgType : uint8_t
{
    WS_MSG_FRAME_DELTA = 0x01,
    WS_MSG_BUS_STATS = 0x02,
    WS_MSG_DIFF = 0x03,
};

enum WsDiffSubtype : uint8_t
{
    WS_DIFF_SNAPSHOT = 0x01,
    WS_DIFF_PRETRIGGER = 0x02,
    WS_DIFF_BASELINE = 0x03,
    WS_DIFF_LABELS = 0x04,
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

struct WsPretriggerRecord
{
    uint8_t channel;
    uint16_t id;
    uint16_t first_seen_ms_ago;
    uint16_t last_seen_ms_ago;
    uint16_t frames;
    uint16_t changes;
    uint8_t dlc;
    uint8_t data[8];
};

struct WsDiffRecord
{
    uint8_t channel;
    uint16_t id;
    uint8_t kind;
    uint8_t dlc_a;
    uint8_t data_a[8];
    uint8_t dlc_b;
    uint8_t data_b[8];
};

struct WsBaselineRecord
{
    uint8_t channel;
    uint16_t id;
};
#pragma pack(pop)

size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count);
size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats);
size_t wsBuildSnapshotDiff(uint8_t *buf, size_t cap, const WsDiffRecord *recs, uint8_t count);
size_t wsBuildPretrigger(uint8_t *buf, size_t cap, const WsPretriggerRecord *recs, uint8_t count);
size_t wsBuildBaseline(uint8_t *buf, size_t cap, const WsBaselineRecord *recs, uint8_t count);
