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

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_delta_header_and_one_record);
    RUN_TEST(test_frame_delta_respects_buffer_cap);
    RUN_TEST(test_bus_stats_layout);
    return UNITY_END();
}
