#include <unity.h>
#include "analyzer/signal_window.h"

static CapturedFrame makeFrame(uint64_t ts_us, uint8_t channel, uint16_t id, uint8_t dlc,
                               uint8_t b0 = 0, uint8_t b1 = 0, uint8_t b2 = 0)
{
    CapturedFrame f{};
    f.ts_us = ts_us;
    f.channel = channel;
    f.id = id;
    f.dlc = dlc;
    f.data[0] = b0;
    f.data[1] = b1;
    f.data[2] = b2;
    return f;
}

void test_watch_duplicate_unwatch_and_slot_reuse()
{
    WatchedSignalWindow window;
    RawSamplePoint storage[2][3] = {};
    WindowSlot slots[2] = {{storage[0]}, {storage[1]}};
    window.init(slots, 2, 3);

    TEST_ASSERT_TRUE(window.watch(0, 0x100));
    TEST_ASSERT_TRUE(window.isWatched(0, 0x100));
    TEST_ASSERT_TRUE(window.watch(0, 0x100));
    TEST_ASSERT_TRUE(window.isWatched(0, 0x100));

    TEST_ASSERT_TRUE(window.watch(1, 0x200));
    TEST_ASSERT_TRUE(window.isWatched(1, 0x200));
    TEST_ASSERT_FALSE(window.watch(0, 0x300));

    window.unwatch(0, 0x100);
    TEST_ASSERT_FALSE(window.isWatched(0, 0x100));
    TEST_ASSERT_TRUE(window.watch(0, 0x300));
    TEST_ASSERT_TRUE(window.isWatched(0, 0x300));
}

void test_unwatched_frames_are_not_stored()
{
    WatchedSignalWindow window;
    RawSamplePoint storage[1][4] = {};
    WindowSlot slots[1] = {{storage[0]}};
    window.init(slots, 1, 4);

    window.push(makeFrame(1000, 0, 0x123, 2, 0x11, 0x22));

    RawSamplePoint out[4] = {};
    TEST_ASSERT_EQUAL_size_t(0, window.copySamples(0, 0x123, out, 4));
}

void test_push_preserves_order_and_overwrites_oldest()
{
    WatchedSignalWindow window;
    RawSamplePoint storage[1][3] = {};
    WindowSlot slots[1] = {{storage[0]}};
    window.init(slots, 1, 3);
    TEST_ASSERT_TRUE(window.watch(0, 0x123));

    window.push(makeFrame(1000, 0, 0x123, 1, 0x10));
    window.push(makeFrame(2000, 0, 0x123, 2, 0x20, 0x21));
    window.push(makeFrame(3000, 0, 0x123, 3, 0x30, 0x31, 0x32));
    window.push(makeFrame(4000, 0, 0x123, 4, 0x40, 0x41, 0x42));

    RawSamplePoint out[4] = {};
    const size_t n = window.copySamples(0, 0x123, out, 4);

    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT64(2000, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT8(2, out[0].dlc);
    TEST_ASSERT_EQUAL_UINT8(0x20, out[0].data[0]);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].sequence);

    TEST_ASSERT_EQUAL_UINT64(3000, out[1].ts_us);
    TEST_ASSERT_EQUAL_UINT8(3, out[1].dlc);
    TEST_ASSERT_EQUAL_UINT8(0x30, out[1].data[0]);
    TEST_ASSERT_EQUAL_UINT32(2, out[1].sequence);

    TEST_ASSERT_EQUAL_UINT64(4000, out[2].ts_us);
    TEST_ASSERT_EQUAL_UINT8(4, out[2].dlc);
    TEST_ASSERT_EQUAL_UINT8(0x40, out[2].data[0]);
    TEST_ASSERT_EQUAL_UINT32(3, out[2].sequence);
}

void test_copy_samples_respects_cap_and_keeps_newest_samples_in_old_to_new_order()
{
    WatchedSignalWindow window;
    RawSamplePoint storage[1][4] = {};
    WindowSlot slots[1] = {{storage[0]}};
    window.init(slots, 1, 4);
    TEST_ASSERT_TRUE(window.watch(1, 0x321));

    window.push(makeFrame(1000, 1, 0x321, 1, 0x01));
    window.push(makeFrame(2000, 1, 0x321, 1, 0x02));
    window.push(makeFrame(3000, 1, 0x321, 1, 0x03));

    RawSamplePoint out[2] = {};
    const size_t n = window.copySamples(1, 0x321, out, 2);

    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_UINT64(2000, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT8(0x02, out[0].data[0]);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].sequence);
    TEST_ASSERT_EQUAL_UINT64(3000, out[1].ts_us);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[1].data[0]);
    TEST_ASSERT_EQUAL_UINT32(2, out[1].sequence);
}

void test_copy_samples_returns_zero_for_empty_or_unwatched_slot()
{
    WatchedSignalWindow window;
    RawSamplePoint storage[2][2] = {};
    WindowSlot slots[2] = {{storage[0]}, {storage[1]}};
    window.init(slots, 2, 2);
    TEST_ASSERT_TRUE(window.watch(0, 0x111));

    RawSamplePoint out[2] = {};
    TEST_ASSERT_EQUAL_size_t(0, window.copySamples(0, 0x111, out, 2));
    TEST_ASSERT_EQUAL_size_t(0, window.copySamples(1, 0x222, out, 2));
}

void test_watch_limit_is_enforced_without_duplicates_counting_against_capacity()
{
    WatchedSignalWindow window;
    RawSamplePoint storage[2][2] = {};
    WindowSlot slots[2] = {{storage[0]}, {storage[1]}};
    window.init(slots, 2, 2);

    TEST_ASSERT_TRUE(window.watch(0, 0x100));
    TEST_ASSERT_TRUE(window.watch(0, 0x100));
    TEST_ASSERT_TRUE(window.watch(1, 0x101));
    TEST_ASSERT_FALSE(window.watch(0, 0x102));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_watch_duplicate_unwatch_and_slot_reuse);
    RUN_TEST(test_unwatched_frames_are_not_stored);
    RUN_TEST(test_push_preserves_order_and_overwrites_oldest);
    RUN_TEST(test_copy_samples_respects_cap_and_keeps_newest_samples_in_old_to_new_order);
    RUN_TEST(test_copy_samples_returns_zero_for_empty_or_unwatched_slot);
    RUN_TEST(test_watch_limit_is_enforced_without_duplicates_counting_against_capacity);
    return UNITY_END();
}
