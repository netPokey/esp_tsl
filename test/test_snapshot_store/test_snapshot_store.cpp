#include <unity.h>
#include "analyzer/snapshot_store.h"

static IdRecord records[kChannelCount * kStdIdCount];
static IdTable table;
static SnapshotRecord slotA[kChannelCount * kStdIdCount];
static SnapshotRecord slotB[kChannelCount * kStdIdCount];
static SnapshotStore store;

void setUp() { table.init(records); store.init(slotA, slotB); }
void tearDown() {}

static CapturedFrame frame(uint8_t ch, uint32_t id, uint8_t d0) {
    CapturedFrame f{};
    f.channel = ch;
    f.id = id;
    f.dlc = 1;
    f.data[0] = d0;
    f.ts_us = 1000;
    return f;
}

void test_diff_reports_added_id() {
    store.capture(SnapshotSlot::A, table);
    table.update(frame(0, 0x120, 0x11));
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8]{};
    const size_t n = store.diff(out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x120, out[0].id);
    TEST_ASSERT_EQUAL_UINT8(SNAPSHOT_DIFF_ADDED, out[0].kind);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].dlc_a);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].dlc_b);
    TEST_ASSERT_EQUAL_UINT8(0x11, out[0].data_b[0]);
}

void test_diff_reports_removed_id() {
    table.update(frame(1, 0x220, 0x44));
    store.capture(SnapshotSlot::A, table);
    table.init(records);
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8]{};
    const size_t n = store.diff(out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x220, out[0].id);
    TEST_ASSERT_EQUAL_UINT8(SNAPSHOT_DIFF_REMOVED, out[0].kind);
}

void test_diff_reports_changed_data() {
    table.update(frame(0, 0x333, 0x01));
    store.capture(SnapshotSlot::A, table);
    table.update(frame(0, 0x333, 0x02));
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8]{};
    const size_t n = store.diff(out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(SNAPSHOT_DIFF_CHANGED, out[0].kind);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[0].data_a[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, out[0].data_b[0]);
}

void test_diff_omits_identical_records() {
    table.update(frame(0, 0x444, 0x99));
    store.capture(SnapshotSlot::A, table);
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8]{};
    TEST_ASSERT_EQUAL_size_t(0, store.diff(out, 8));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_diff_reports_added_id);
    RUN_TEST(test_diff_reports_removed_id);
    RUN_TEST(test_diff_reports_changed_data);
    RUN_TEST(test_diff_omits_identical_records);
    return UNITY_END();
}
