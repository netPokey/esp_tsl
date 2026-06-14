#include <unity.h>
#include <cstring>
#include "analyzer/ws_protocol.h"

void setUp() {}
void tearDown() {}

void test_frame_delta_header_and_one_record()
{
    WsFrameRecord rec;
    rec.channel = 0; rec.id = 0x132; rec.dlc = 3;
    rec.data[0] = 0xAA; rec.data[1] = 0xBB; rec.data[2] = 0xCC;
    rec.last_rx_ms = 0x01020304;
    for (int i = 0; i < 8; ++i) rec.byte_age_ms[i] = 0;
    rec.rx_count = 42;
    rec.last_delta_ms = 99;
    rec.period_ms = 100;
    rec.jitter_ms = 5;
    rec.change_score = 12;
    rec.flags = 0x02;

    uint8_t buf[256];
    const size_t n = wsBuildFrameDelta(buf, sizeof(buf), &rec, 1);

    TEST_ASSERT_EQUAL_UINT8(WS_MSG_FRAME_DELTA, buf[0]); // type
    TEST_ASSERT_EQUAL_UINT8(1, buf[1]);                  // count
    // 头 2 字节 + 1 条定长记录
    TEST_ASSERT_EQUAL_size_t(2 + sizeof(WsFrameRecord), n);
    // 小端 id 校验：记录区起点 buf+2
    const WsFrameRecord *out = reinterpret_cast<const WsFrameRecord *>(buf + 2);
    TEST_ASSERT_EQUAL_UINT16(0x132, out->id);
    TEST_ASSERT_EQUAL_UINT8(0xBB, out->data[1]);
    TEST_ASSERT_EQUAL_UINT32(42, out->rx_count);
    TEST_ASSERT_EQUAL_UINT16(99, out->last_delta_ms);
    TEST_ASSERT_EQUAL_UINT16(100, out->period_ms);
    TEST_ASSERT_EQUAL_UINT16(5, out->jitter_ms);
    TEST_ASSERT_EQUAL_UINT16(12, out->change_score);
    TEST_ASSERT_EQUAL_UINT8(0x02, out->flags);
}

void test_frame_delta_respects_buffer_cap()
{
    WsFrameRecord recs[4];
    memset(recs, 0, sizeof(recs));
    uint8_t small[2 + sizeof(WsFrameRecord) + 1]; // 只够装 1 条
    const size_t n = wsBuildFrameDelta(small, sizeof(small), recs, 4);
    TEST_ASSERT_EQUAL_size_t(2 + sizeof(WsFrameRecord), n); // 截断到 1 条
    TEST_ASSERT_EQUAL_UINT8(1, small[1]);
}

void test_bus_stats_layout()
{
    WsBusStats s;
    s.fps_a = 100; s.fps_b = 50;
    s.load_a_x10 = 421; s.load_b_x10 = 88;  // 42.1% / 8.8%
    s.rx_err_a = 0; s.rx_err_b = 3;
    s.bus_off_a = 0; s.bus_off_b = 1;
    s.dropped = 7;

    uint8_t buf[64];
    const size_t n = wsBuildBusStats(buf, sizeof(buf), s);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_BUS_STATS, buf[0]);
    TEST_ASSERT_EQUAL_size_t(1 + sizeof(WsBusStats), n);
    const WsBusStats *out = reinterpret_cast<const WsBusStats *>(buf + 1);
    TEST_ASSERT_EQUAL_UINT16(421, out->load_a_x10);
    TEST_ASSERT_EQUAL_UINT32(7, out->dropped);
}

void test_snapshot_diff_layout()
{
    WsDiffRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.channel = 1;
    rec.id = 0x456;
    rec.kind = 0xA5;
    rec.dlc_a = 3;
    rec.data_a[0] = 0x11;
    rec.data_a[1] = 0x22;
    rec.data_a[2] = 0x33;
    rec.dlc_b = 4;
    rec.data_b[0] = 0x44;
    rec.data_b[1] = 0x55;
    rec.data_b[2] = 0x66;
    rec.data_b[3] = 0x77;

    uint8_t buf[128];
    const size_t n = wsBuildSnapshotDiff(buf, sizeof(buf), &rec, 1);

    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_SNAPSHOT, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsDiffRecord), n);
    const WsDiffRecord *out = reinterpret_cast<const WsDiffRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT8(1, out->channel);
    TEST_ASSERT_EQUAL_UINT16(0x456, out->id);
    TEST_ASSERT_EQUAL_UINT8(0xA5, out->kind);
    TEST_ASSERT_EQUAL_UINT8(3, out->dlc_a);
    TEST_ASSERT_EQUAL_UINT8(0x22, out->data_a[1]);
    TEST_ASSERT_EQUAL_UINT8(4, out->dlc_b);
    TEST_ASSERT_EQUAL_UINT8(0x66, out->data_b[2]);
}

void test_diff_builders_allow_zero_count_null_records()
{
    uint8_t buf[8];
    memset(buf, 0xCC, sizeof(buf));

    const size_t n = wsBuildSnapshotDiff(buf, sizeof(buf), nullptr, 0);

    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_SNAPSHOT, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, buf[3]);
}

void test_diff_builders_reject_too_small_cap()
{
    WsDiffRecord rec;
    memset(&rec, 0, sizeof(rec));
    uint8_t buf[2];
    memset(buf, 0xCC, sizeof(buf));

    const size_t n = wsBuildSnapshotDiff(buf, sizeof(buf), &rec, 1);

    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_diff_builders_exact_header_cap()
{
    WsDiffRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.channel = 1;
    rec.id = 0x456;
    uint8_t buf[3];

    const size_t n = wsBuildSnapshotDiff(buf, sizeof(buf), &rec, 1);

    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_SNAPSHOT, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
}

void test_pretrigger_layout_and_cap()
{
    WsPretriggerRecord recs[4];
    memset(recs, 0, sizeof(recs));
    recs[0].channel = 0;
    recs[0].id = 0x123;
    recs[0].first_seen_ms_ago = 1000;
    recs[0].last_seen_ms_ago = 25;
    recs[0].frames = 7;
    recs[0].changes = 2;
    recs[0].dlc = 2;
    recs[0].data[0] = 0xAB;
    recs[0].data[1] = 0xCD;
    recs[1].id = 0x456;

    uint8_t buf[3 + sizeof(WsPretriggerRecord) + 1];
    const size_t n = wsBuildPretrigger(buf, sizeof(buf), recs, 4);

    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_PRETRIGGER, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsPretriggerRecord), n);
    const WsPretriggerRecord *out = reinterpret_cast<const WsPretriggerRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT16(0x123, out->id);
    TEST_ASSERT_EQUAL_UINT16(1000, out->first_seen_ms_ago);
    TEST_ASSERT_EQUAL_UINT16(25, out->last_seen_ms_ago);
    TEST_ASSERT_EQUAL_UINT16(7, out->frames);
    TEST_ASSERT_EQUAL_UINT16(2, out->changes);
    TEST_ASSERT_EQUAL_UINT8(0xCD, out->data[1]);
}

void test_baseline_layout()
{
    WsBaselineRecord recs[2];
    recs[0].channel = 0;
    recs[0].id = 0x111;
    recs[1].channel = 1;
    recs[1].id = 0x222;

    uint8_t buf[64];
    const size_t n = wsBuildBaseline(buf, sizeof(buf), recs, 2);

    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_BASELINE, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(2, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + 2 * sizeof(WsBaselineRecord), n);
    const WsBaselineRecord *out = reinterpret_cast<const WsBaselineRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x111, out[0].id);
    TEST_ASSERT_EQUAL_UINT8(1, out[1].channel);
    TEST_ASSERT_EQUAL_UINT16(0x222, out[1].id);
}

void test_baseline_respects_buffer_cap()
{
    WsBaselineRecord recs[2];
    recs[0].channel = 0;
    recs[0].id = 0x111;
    recs[1].channel = 1;
    recs[1].id = 0x222;

    uint8_t buf[3 + sizeof(WsBaselineRecord) + 1];
    const size_t n = wsBuildBaseline(buf, sizeof(buf), recs, 2);

    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsBaselineRecord), n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_BASELINE, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    const WsBaselineRecord *out = reinterpret_cast<const WsBaselineRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x111, out[0].id);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_delta_header_and_one_record);
    RUN_TEST(test_frame_delta_respects_buffer_cap);
    RUN_TEST(test_bus_stats_layout);
    RUN_TEST(test_snapshot_diff_layout);
    RUN_TEST(test_diff_builders_allow_zero_count_null_records);
    RUN_TEST(test_diff_builders_reject_too_small_cap);
    RUN_TEST(test_diff_builders_exact_header_cap);
    RUN_TEST(test_pretrigger_layout_and_cap);
    RUN_TEST(test_baseline_layout);
    RUN_TEST(test_baseline_respects_buffer_cap);
    return UNITY_END();
}
