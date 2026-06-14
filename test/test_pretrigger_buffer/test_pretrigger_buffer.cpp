#include <unity.h>
#include <cstring>
#include "analyzer/pretrigger_buffer.h"

static CapturedFrame makeFrame(uint64_t ts_us, uint8_t channel, uint16_t id, uint8_t dlc,
                               uint8_t b0 = 0, uint8_t b1 = 0, uint8_t b2 = 0)
{
    CapturedFrame f;
    f.ts_us = ts_us;
    f.channel = channel;
    f.id = id;
    f.dlc = dlc;
    f.data[0] = b0;
    f.data[1] = b1;
    f.data[2] = b2;
    return f;
}

void test_collect_returns_frames_in_window()
{
    PretriggerBuffer buf;
    CapturedFrame storage[8] = {};
    buf.init(storage, 8);

    buf.push(makeFrame(1000000ULL, 0, 0x100, 1, 0x11));
    buf.push(makeFrame(2000000ULL, 0, 0x101, 1, 0x22));
    buf.push(makeFrame(3000000ULL, 1, 0x102, 1, 0x33));

    CapturedFrame out[8] = {};
    const size_t n = buf.collect(3000000ULL, 5000000U, out, 8);

    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT64(1000000ULL, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT64(2000000ULL, out[1].ts_us);
    TEST_ASSERT_EQUAL_UINT64(3000000ULL, out[2].ts_us);
}

void test_collect_excludes_frames_older_than_window()
{
    PretriggerBuffer buf;
    CapturedFrame storage[8] = {};
    buf.init(storage, 8);

    buf.push(makeFrame(1000000ULL, 0, 0x100, 1, 0x11));
    buf.push(makeFrame(6500000ULL, 0, 0x101, 1, 0x22));
    buf.push(makeFrame(7000000ULL, 1, 0x102, 1, 0x33));

    CapturedFrame out[8] = {};
    const size_t n = buf.collect(7000000ULL, 5000000U, out, 8);

    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_UINT64(6500000ULL, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT64(7000000ULL, out[1].ts_us);
}

void test_ring_overwrites_oldest_when_full()
{
    PretriggerBuffer buf;
    CapturedFrame storage[8] = {};
    buf.init(storage, 8);

    for (uint64_t sec = 1; sec <= 10; ++sec)
        buf.push(makeFrame(sec * 1000000ULL, 0, static_cast<uint16_t>(0x100 + sec), 1, static_cast<uint8_t>(sec)));

    CapturedFrame out[8] = {};
    const size_t n = buf.collect(10000000ULL, 10000000U, out, 8);

    TEST_ASSERT_EQUAL_size_t(7, n);
    for (size_t i = 0; i < n; ++i)
        TEST_ASSERT_EQUAL_UINT64((static_cast<uint64_t>(i) + 4ULL) * 1000000ULL, out[i].ts_us);
}

void test_summarize_groups_by_channel_id()
{
    PretriggerBuffer buf;
    CapturedFrame storage[8] = {};
    buf.init(storage, 8);

    buf.push(makeFrame(5000000ULL, 0, 0x100, 2, 0x01, 0x02));
    buf.push(makeFrame(6500000ULL, 0, 0x100, 2, 0x01, 0x03));
    buf.push(makeFrame(6800000ULL, 1, 0x100, 2, 0x09, 0x08));

    WsPretriggerRecord out[8] = {};
    const size_t n = buf.summarize(7000000ULL, 5000000U, out, 8);

    TEST_ASSERT_EQUAL_size_t(2, n);

    TEST_ASSERT_EQUAL_UINT8(0, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x100, out[0].id);
    TEST_ASSERT_EQUAL_UINT16(2, out[0].frames);
    TEST_ASSERT_EQUAL_UINT16(1, out[0].changes);
    TEST_ASSERT_EQUAL_UINT8(2, out[0].dlc);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[0].data[1]);
    TEST_ASSERT_EQUAL_UINT16(2000, out[0].first_seen_ms_ago);
    TEST_ASSERT_EQUAL_UINT16(500, out[0].last_seen_ms_ago);

    TEST_ASSERT_EQUAL_UINT8(1, out[1].channel);
    TEST_ASSERT_EQUAL_UINT16(0x100, out[1].id);
    TEST_ASSERT_EQUAL_UINT16(1, out[1].frames);
    TEST_ASSERT_EQUAL_UINT16(0, out[1].changes);
    TEST_ASSERT_EQUAL_UINT16(200, out[1].first_seen_ms_ago);
    TEST_ASSERT_EQUAL_UINT16(200, out[1].last_seen_ms_ago);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_collect_returns_frames_in_window);
    RUN_TEST(test_collect_excludes_frames_older_than_window);
    RUN_TEST(test_ring_overwrites_oldest_when_full);
    RUN_TEST(test_summarize_groups_by_channel_id);
    return UNITY_END();
}
