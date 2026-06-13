#include <unity.h>
#include "analyzer/id_table.h"

static IdTable table;
static IdRecord storage[2][2048];

void setUp()
{
    table.init(&storage[0][0]);
}

void test_first_frame_marks_present_and_counts()
{
    CapturedFrame f;
    f.channel = 0; f.id = 0x132; f.dlc = 3; f.ts_us = 1000;
    f.data[0] = 0x11; f.data[1] = 0x22; f.data[2] = 0x33;
    table.update(f);

    const IdRecord &r = table.record(0, 0x132);
    TEST_ASSERT_TRUE(r.present);
    TEST_ASSERT_EQUAL_UINT32(1, r.rx_count);
    TEST_ASSERT_EQUAL_UINT8(3, r.dlc);
    TEST_ASSERT_EQUAL_UINT8(0x22, r.data[1]);
}

void test_dedup_same_id_increments_count_not_new_slot()
{
    CapturedFrame f;
    f.channel = 1; f.id = 0x200; f.dlc = 1; f.ts_us = 1000;
    table.update(f);
    f.ts_us = 2000;
    table.update(f);
    TEST_ASSERT_EQUAL_UINT32(2, table.record(1, 0x200).rx_count);
}

void test_byte_change_ts_updates_only_on_change()
{
    CapturedFrame f;
    f.channel = 0; f.id = 0x300; f.dlc = 2; f.ts_us = 1000;
    f.data[0] = 0xAA; f.data[1] = 0xBB;
    table.update(f);

    f.ts_us = 5000;
    f.data[0] = 0xAA;
    f.data[1] = 0xCC;
    table.update(f);

    const IdRecord &r = table.record(0, 0x300);
    TEST_ASSERT_EQUAL_UINT64(1000, r.byte_change_ts[0]);
    TEST_ASSERT_EQUAL_UINT64(5000, r.byte_change_ts[1]);
}

void test_delta_time_tracks_prev_and_last()
{
    CapturedFrame f;
    f.channel = 0; f.id = 0x400; f.dlc = 1; f.ts_us = 1000;
    table.update(f);
    f.ts_us = 3500;
    table.update(f);
    const IdRecord &r = table.record(0, 0x400);
    TEST_ASSERT_EQUAL_UINT64(3500, r.last_rx_ts);
    TEST_ASSERT_EQUAL_UINT64(1000, r.prev_rx_ts);
}

void test_channel_isolation()
{
    CapturedFrame f;
    f.id = 0x500; f.dlc = 1; f.ts_us = 1000;
    f.channel = 0; table.update(f);
    TEST_ASSERT_TRUE(table.record(0, 0x500).present);
    TEST_ASSERT_FALSE(table.record(1, 0x500).present);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_frame_marks_present_and_counts);
    RUN_TEST(test_dedup_same_id_increments_count_not_new_slot);
    RUN_TEST(test_byte_change_ts_updates_only_on_change);
    RUN_TEST(test_delta_time_tracks_prev_and_last);
    RUN_TEST(test_channel_isolation);
    return UNITY_END();
}
