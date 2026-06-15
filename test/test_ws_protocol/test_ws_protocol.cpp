#include <unity.h>
#include <cstring>
#include "analyzer/analyzer_web.h"
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

void test_signal_samples_layout()
{
    WsSignalSampleRecord recs[2];
    memset(recs, 0, sizeof(recs));
    recs[0].channel = 1;
    recs[0].id = 0x321;
    recs[0].dlc = 4;
    recs[0].data[0] = 0x10;
    recs[0].data[1] = 0x20;
    recs[0].data[2] = 0x30;
    recs[0].data[3] = 0x40;
    recs[0].sample_age_ms = 250;
    recs[0].sequence_lo = 0x10203040;
    recs[1].channel = 0;
    recs[1].id = 0x123;
    recs[1].dlc = 2;
    recs[1].data[0] = 0xAA;
    recs[1].data[1] = 0xBB;
    recs[1].sample_age_ms = 12;
    recs[1].sequence_lo = 77;

    uint8_t buf[128];
    const size_t n = wsBuildSignalSamples(buf, sizeof(buf), recs, 2);

    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_SAMPLES, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(2, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + 2 * sizeof(WsSignalSampleRecord), n);
    const WsSignalSampleRecord *out = reinterpret_cast<const WsSignalSampleRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x321, out[0].id);
    TEST_ASSERT_EQUAL_UINT8(4, out[0].dlc);
    TEST_ASSERT_EQUAL_UINT8(0x30, out[0].data[2]);
    TEST_ASSERT_EQUAL_UINT16(250, out[0].sample_age_ms);
    TEST_ASSERT_EQUAL_UINT32(0x10203040, out[0].sequence_lo);
    TEST_ASSERT_EQUAL_UINT8(0, out[1].channel);
    TEST_ASSERT_EQUAL_UINT16(0x123, out[1].id);
    TEST_ASSERT_EQUAL_UINT8(0xBB, out[1].data[1]);
    TEST_ASSERT_EQUAL_UINT32(77, out[1].sequence_lo);
}

void test_signal_hints_layout()
{
    WsSignalHintRecord recs[2];
    memset(recs, 0, sizeof(recs));
    recs[0].kind = 3;
    recs[0].start_bit = 12;
    recs[0].bit_length = 16;
    recs[0].confidence_x1000 = 875;
    memcpy(recs[0].evidence, "rpm", 4);
    recs[1].kind = 7;
    recs[1].start_bit = 40;
    recs[1].bit_length = 8;
    recs[1].confidence_x1000 = 1000;
    memcpy(recs[1].evidence, "gear=4", 7);

    uint8_t buf[128];
    const size_t n = wsBuildSignalHints(buf, sizeof(buf), recs, 2);

    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_HINTS, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(2, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + 2 * sizeof(WsSignalHintRecord), n);
    const WsSignalHintRecord *out = reinterpret_cast<const WsSignalHintRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT8(3, out[0].kind);
    TEST_ASSERT_EQUAL_UINT8(12, out[0].start_bit);
    TEST_ASSERT_EQUAL_UINT8(16, out[0].bit_length);
    TEST_ASSERT_EQUAL_UINT16(875, out[0].confidence_x1000);
    TEST_ASSERT_EQUAL_UINT8('r', out[0].evidence[0]);
    TEST_ASSERT_EQUAL_UINT8('m', out[0].evidence[2]);
    TEST_ASSERT_EQUAL_UINT8('\0', out[0].evidence[3]);
    TEST_ASSERT_EQUAL_UINT8(7, out[1].kind);
    TEST_ASSERT_EQUAL_UINT8(40, out[1].start_bit);
    TEST_ASSERT_EQUAL_UINT16(1000, out[1].confidence_x1000);
    TEST_ASSERT_EQUAL_UINT8('4', out[1].evidence[5]);
    TEST_ASSERT_EQUAL_UINT8('\0', out[1].evidence[6]);
}

void test_signal_builders_allow_zero_count_null_records()
{
    uint8_t buf[8];
    memset(buf, 0xCC, sizeof(buf));

    size_t n = wsBuildSignalSamples(buf, sizeof(buf), nullptr, 0);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_SAMPLES, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, buf[3]);

    memset(buf, 0xCC, sizeof(buf));
    n = wsBuildSignalHints(buf, sizeof(buf), nullptr, 0);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_HINTS, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, buf[3]);
}

void test_signal_builders_reject_too_small_cap()
{
    WsSignalSampleRecord sample;
    memset(&sample, 0, sizeof(sample));
    WsSignalHintRecord hint;
    memset(&hint, 0, sizeof(hint));
    uint8_t buf[2];
    memset(buf, 0xCC, sizeof(buf));

    TEST_ASSERT_EQUAL_size_t(0, wsBuildSignalSamples(buf, sizeof(buf), &sample, 1));
    TEST_ASSERT_EQUAL_size_t(0, wsBuildSignalHints(buf, sizeof(buf), &hint, 1));
}

void test_signal_builders_exact_header_cap()
{
    WsSignalSampleRecord sample;
    memset(&sample, 0, sizeof(sample));
    sample.channel = 1;
    sample.id = 0x456;
    WsSignalHintRecord hint;
    memset(&hint, 0, sizeof(hint));
    hint.kind = 2;
    uint8_t buf[3];

    size_t n = wsBuildSignalSamples(buf, sizeof(buf), &sample, 1);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_SAMPLES, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);

    n = wsBuildSignalHints(buf, sizeof(buf), &hint, 1);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_HINTS, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
}

void test_signal_samples_respect_buffer_cap()
{
    WsSignalSampleRecord recs[2];
    memset(recs, 0, sizeof(recs));
    recs[0].id = 0x101;
    recs[1].id = 0x202;

    uint8_t buf[3 + sizeof(WsSignalSampleRecord) + 1];
    const size_t n = wsBuildSignalSamples(buf, sizeof(buf), recs, 2);

    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsSignalSampleRecord), n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_SAMPLES, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    const WsSignalSampleRecord *out = reinterpret_cast<const WsSignalSampleRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT16(0x101, out[0].id);
}

void test_signal_hints_respect_buffer_cap()
{
    WsSignalHintRecord recs[2];
    memset(recs, 0, sizeof(recs));
    recs[0].kind = 4;
    recs[1].kind = 5;

    uint8_t buf[3 + sizeof(WsSignalHintRecord) + 1];
    const size_t n = wsBuildSignalHints(buf, sizeof(buf), recs, 2);

    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsSignalHintRecord), n);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_HINTS, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    const WsSignalHintRecord *out = reinterpret_cast<const WsSignalHintRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT8(4, out[0].kind);
}

void test_analyzer_web_signal_helpers_clamp_confidence_and_age()
{
    TEST_ASSERT_EQUAL_UINT16(0, analyzerWebConfidenceX1000ForTest(-0.1f));
    TEST_ASSERT_EQUAL_UINT16(0, analyzerWebConfidenceX1000ForTest(0.0f));
    TEST_ASSERT_EQUAL_UINT16(875, analyzerWebConfidenceX1000ForTest(0.8746f));
    TEST_ASSERT_EQUAL_UINT16(1000, analyzerWebConfidenceX1000ForTest(1.0f));
    TEST_ASSERT_EQUAL_UINT16(1000, analyzerWebConfidenceX1000ForTest(1.25f));

    TEST_ASSERT_EQUAL_UINT16(500, analyzerWebSampleAgeMsForTest(2000000ULL, 1500000ULL));
    TEST_ASSERT_EQUAL_UINT16(0, analyzerWebSampleAgeMsForTest(1500000ULL, 2000000ULL));
    TEST_ASSERT_EQUAL_UINT16(65535, analyzerWebSampleAgeMsForTest(70000000ULL, 0ULL));
}

void test_analyzer_web_body_chunk_helpers_validate_bounds_and_completion()
{
    TEST_ASSERT_TRUE(analyzerWebBodyChunkIsValidForTest(0, 32, 64, 128));
    TEST_ASSERT_TRUE(analyzerWebBodyChunkIsValidForTest(32, 32, 64, 128));
    TEST_ASSERT_FALSE(analyzerWebBodyChunkIsValidForTest(0, 129, 129, 128));
    TEST_ASSERT_FALSE(analyzerWebBodyChunkIsValidForTest(65, 1, 64, 128));
    TEST_ASSERT_FALSE(analyzerWebBodyChunkIsValidForTest(63, 2, 64, 128));

    TEST_ASSERT_FALSE(analyzerWebBodyChunkCompletesForTest(0, 32, 64));
    TEST_ASSERT_TRUE(analyzerWebBodyChunkCompletesForTest(32, 32, 64));
    TEST_ASSERT_TRUE(analyzerWebBodyChunkCompletesForTest(0, 0, 0));
    TEST_ASSERT_FALSE(analyzerWebBodyChunkCompletesForTest(64, 1, 64));
}

void test_analyzer_web_parses_only_explicit_valid_channels_and_slots()
{
    uint8_t channel = 99;
    TEST_ASSERT_TRUE(analyzerWebParseChannelForTest("A", channel));
    TEST_ASSERT_EQUAL_UINT8(0, channel);
    TEST_ASSERT_TRUE(analyzerWebParseChannelForTest("a", channel));
    TEST_ASSERT_EQUAL_UINT8(0, channel);
    TEST_ASSERT_TRUE(analyzerWebParseChannelForTest("B", channel));
    TEST_ASSERT_EQUAL_UINT8(1, channel);
    TEST_ASSERT_TRUE(analyzerWebParseChannelForTest("b", channel));
    TEST_ASSERT_EQUAL_UINT8(1, channel);
    channel = 77;
    TEST_ASSERT_FALSE(analyzerWebParseChannelForTest(nullptr, channel));
    TEST_ASSERT_EQUAL_UINT8(77, channel);
    TEST_ASSERT_FALSE(analyzerWebParseChannelForTest("", channel));
    TEST_ASSERT_FALSE(analyzerWebParseChannelForTest("C", channel));
    TEST_ASSERT_FALSE(analyzerWebParseChannelForTest("AA", channel));

    SnapshotSlot slot = SnapshotSlot::B;
    TEST_ASSERT_TRUE(analyzerWebParseSlotForTest("A", slot));
    TEST_ASSERT_TRUE(slot == SnapshotSlot::A);
    TEST_ASSERT_TRUE(analyzerWebParseSlotForTest("b", slot));
    TEST_ASSERT_TRUE(slot == SnapshotSlot::B);
    TEST_ASSERT_FALSE(analyzerWebParseSlotForTest(nullptr, slot));
    TEST_ASSERT_TRUE(slot == SnapshotSlot::B);
    TEST_ASSERT_FALSE(analyzerWebParseSlotForTest("C", slot));
    TEST_ASSERT_FALSE(analyzerWebParseSlotForTest("BB", slot));
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
    RUN_TEST(test_signal_samples_layout);
    RUN_TEST(test_signal_hints_layout);
    RUN_TEST(test_signal_builders_allow_zero_count_null_records);
    RUN_TEST(test_signal_builders_reject_too_small_cap);
    RUN_TEST(test_signal_builders_exact_header_cap);
    RUN_TEST(test_signal_samples_respect_buffer_cap);
    RUN_TEST(test_signal_hints_respect_buffer_cap);
    RUN_TEST(test_analyzer_web_signal_helpers_clamp_confidence_and_age);
    RUN_TEST(test_analyzer_web_body_chunk_helpers_validate_bounds_and_completion);
    RUN_TEST(test_analyzer_web_parses_only_explicit_valid_channels_and_slots);
    return UNITY_END();
}
