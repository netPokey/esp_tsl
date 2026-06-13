#include <unity.h>
#include "analyzer/bus_stats.h"

static CapturedFrame frame(uint8_t ch, uint8_t dlc)
{
    CapturedFrame f;
    f.channel = ch;
    f.id = 0x123;
    f.dlc = dlc;
    return f;
}

void test_counts_fps_by_channel_after_one_second()
{
    BusStatsTracker stats;
    stats.begin(0);
    stats.noteRx(frame(0, 8));
    stats.noteRx(frame(0, 8));
    stats.noteRx(frame(1, 4));
    stats.update(1000, 3);

    const BusStatsSnapshot s = stats.snapshot();
    TEST_ASSERT_EQUAL_UINT16(2, s.fps[0]);
    TEST_ASSERT_EQUAL_UINT16(1, s.fps[1]);
    TEST_ASSERT_EQUAL_UINT32(3, s.dropped);
}

void test_bus_load_is_nonzero_for_rx_traffic()
{
    BusStatsTracker stats;
    stats.begin(0);
    for (int i = 0; i < 100; ++i)
        stats.noteRx(frame(0, 8));
    stats.update(1000, 0);

    const BusStatsSnapshot s = stats.snapshot();
    TEST_ASSERT_GREATER_THAN_UINT16(0, s.load_x10[0]);
    TEST_ASSERT_EQUAL_UINT16(0, s.load_x10[1]);
}

void test_no_update_before_window_elapsed()
{
    BusStatsTracker stats;
    stats.begin(1000);
    stats.noteRx(frame(0, 8));
    stats.update(1500, 0);

    const BusStatsSnapshot s = stats.snapshot();
    TEST_ASSERT_EQUAL_UINT16(0, s.fps[0]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_counts_fps_by_channel_after_one_second);
    RUN_TEST(test_bus_load_is_nonzero_for_rx_traffic);
    RUN_TEST(test_no_update_before_window_elapsed);
    return UNITY_END();
}
