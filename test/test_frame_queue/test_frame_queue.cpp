#include <unity.h>
#include "analyzer/frame_queue.h"

static FrameQueue makeQueue(uint16_t cap, CapturedFrame *buf)
{
    FrameQueue q;
    q.init(buf, cap);
    return q;
}

void test_empty_pop_returns_false()
{
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame out;
    TEST_ASSERT_FALSE(q.pop(out));
}

void test_push_then_pop_roundtrip()
{
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame in;
    in.id = 0x132; in.dlc = 8; in.channel = 1; in.ts_us = 12345;
    in.data[0] = 0xAB;
    TEST_ASSERT_TRUE(q.push(in));
    CapturedFrame out;
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_EQUAL_UINT32(0x132, out.id);
    TEST_ASSERT_EQUAL_UINT8(8, out.dlc);
    TEST_ASSERT_EQUAL_UINT8(1, out.channel);
    TEST_ASSERT_EQUAL_UINT64(12345, out.ts_us);
    TEST_ASSERT_EQUAL_UINT8(0xAB, out.data[0]);
}

void test_full_queue_drops_newest_and_counts()
{
    // 容量 cap，实际可用 cap-1（留一格区分空/满）。
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame f;
    TEST_ASSERT_TRUE(q.push(f));
    TEST_ASSERT_TRUE(q.push(f));
    TEST_ASSERT_TRUE(q.push(f));
    TEST_ASSERT_FALSE(q.push(f));          // 第 4 次：满，丢弃
    TEST_ASSERT_EQUAL_UINT32(1, q.dropped());
}

void test_wraparound()
{
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame f, out;
    for (int round = 0; round < 10; ++round)
    {
        f.id = round;
        TEST_ASSERT_TRUE(q.push(f));
        TEST_ASSERT_TRUE(q.pop(out));
        TEST_ASSERT_EQUAL_UINT32(round, out.id);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_pop_returns_false);
    RUN_TEST(test_push_then_pop_roundtrip);
    RUN_TEST(test_full_queue_drops_newest_and_counts);
    RUN_TEST(test_wraparound);
    return UNITY_END();
}
