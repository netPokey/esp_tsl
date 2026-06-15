#include <unity.h>
#include "analyzer/recorder.h"

static CapturedFrame frameWithId(uint32_t id)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = 1;
    f.data[0] = static_cast<uint8_t>(id & 0xFF);
    return f;
}

void test_init_and_start_reset_state()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    TEST_ASSERT_FALSE(r.active());
    TEST_ASSERT_EQUAL_UINT(0, r.count());
    TEST_ASSERT_EQUAL_UINT(4, r.capacity());
    r.start();
    TEST_ASSERT_TRUE(r.active());
    TEST_ASSERT_EQUAL_UINT(0, r.count());
    TEST_ASSERT_EQUAL_UINT(0, r.dropped());
}

void test_push_accumulates_count()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(1));
    r.push(frameWithId(2));
    TEST_ASSERT_EQUAL_UINT(2, r.count());
    TEST_ASSERT_EQUAL_UINT(0, r.dropped());
}

void test_ring_overwrites_oldest_and_counts_dropped()
{
    CapturedFrame storage[3];
    Recorder r;
    r.init(storage, 3);
    r.start();
    for (uint32_t i = 1; i <= 5; ++i)
        r.push(frameWithId(i));
    TEST_ASSERT_EQUAL_UINT(3, r.count());
    TEST_ASSERT_EQUAL_UINT(2, r.dropped());
    CapturedFrame out[3];
    size_t n = r.collect(out, 3, 0);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT(3, out[0].id);
    TEST_ASSERT_EQUAL_UINT(4, out[1].id);
    TEST_ASSERT_EQUAL_UINT(5, out[2].id);
}

void test_collect_old_to_new_when_not_full()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(10));
    r.push(frameWithId(20));
    CapturedFrame out[4];
    size_t n = r.collect(out, 4, 0);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(10, out[0].id);
    TEST_ASSERT_EQUAL_UINT(20, out[1].id);
}

void test_collect_with_skip_paging()
{
    CapturedFrame storage[5];
    Recorder r;
    r.init(storage, 5);
    r.start();
    for (uint32_t i = 1; i <= 5; ++i)
        r.push(frameWithId(i));
    CapturedFrame out[5];
    size_t n = r.collect(out, 2, 2);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(3, out[0].id);
    TEST_ASSERT_EQUAL_UINT(4, out[1].id);
}

void test_collect_with_skip_after_wrap()
{
    // 满缓冲后 head 折返（oldest=head_!=0），验证 skip 跨模取数顺序正确
    CapturedFrame storage[3];
    Recorder r;
    r.init(storage, 3);
    r.start();
    for (uint32_t i = 1; i <= 5; ++i)   // 保留旧->新 {3,4,5}，head_=2
        r.push(frameWithId(i));
    CapturedFrame out[3];
    size_t n = r.collect(out, 2, 1);    // 跳过 3，取 {4,5}
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(4, out[0].id);
    TEST_ASSERT_EQUAL_UINT(5, out[1].id);
}

void test_collect_skip_beyond_count_returns_zero()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(1));
    CapturedFrame out[4];
    TEST_ASSERT_EQUAL_UINT(0, r.collect(out, 4, 5));
}

void test_stop_keeps_content()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(7));
    r.stop();
    TEST_ASSERT_FALSE(r.active());
    TEST_ASSERT_EQUAL_UINT(1, r.count());
    CapturedFrame out[4];
    TEST_ASSERT_EQUAL_UINT(1, r.collect(out, 4, 0));
    TEST_ASSERT_EQUAL_UINT(7, out[0].id);
}

void test_uninitialized_is_safe()
{
    Recorder r;
    r.start();
    r.push(frameWithId(1));
    TEST_ASSERT_EQUAL_UINT(0, r.count());
    CapturedFrame out[2];
    TEST_ASSERT_EQUAL_UINT(0, r.collect(out, 2, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_and_start_reset_state);
    RUN_TEST(test_push_accumulates_count);
    RUN_TEST(test_ring_overwrites_oldest_and_counts_dropped);
    RUN_TEST(test_collect_old_to_new_when_not_full);
    RUN_TEST(test_collect_with_skip_paging);
    RUN_TEST(test_collect_with_skip_after_wrap);
    RUN_TEST(test_collect_skip_beyond_count_returns_zero);
    RUN_TEST(test_stop_keeps_content);
    RUN_TEST(test_uninitialized_is_safe);
    return UNITY_END();
}
