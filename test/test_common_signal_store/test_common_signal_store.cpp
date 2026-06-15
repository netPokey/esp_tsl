#include <cstring>
#include <unity.h>
#include "analyzer/common_signal_store.h"

static CommonSignalStore store;

void setUp()
{
    store.begin();
}

void tearDown() {}

static CommonSignalSpec makeSpec(uint8_t channel,
                                 uint16_t id,
                                 uint8_t start_bit,
                                 uint8_t bit_length,
                                 uint8_t endian,
                                 uint8_t is_signed,
                                 float scale,
                                 float offset,
                                 const char *label)
{
    CommonSignalSpec spec = {};
    spec.channel = channel;
    spec.id = id;
    spec.start_bit = start_bit;
    spec.bit_length = bit_length;
    spec.endian = endian;
    spec.is_signed = is_signed;
    spec.scale = scale;
    spec.offset = offset;
    if (label)
        strncpy(spec.label, label, sizeof(spec.label));
    return spec;
}

void test_replace_all_writes_multiple_entries()
{
    const CommonSignalSpec specs[] = {
        makeSpec(0, 0x100, 0, 8, 0, 0, 1.0f, 0.0f, "RPM"),
        makeSpec(1, 0x200, 8, 16, 1, 1, 0.5f, -10.0f, "Torque"),
    };

    TEST_ASSERT_TRUE(store.replaceAll(specs, 2));
    TEST_ASSERT_EQUAL_size_t(2, store.count());

    const CommonSignalSpec *entries = store.entries();
    TEST_ASSERT_EQUAL_UINT8(0, entries[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x100, entries[0].id);
    TEST_ASSERT_EQUAL_UINT8(0, entries[0].start_bit);
    TEST_ASSERT_EQUAL_UINT8(8, entries[0].bit_length);
    TEST_ASSERT_EQUAL_UINT8(0, entries[0].endian);
    TEST_ASSERT_EQUAL_UINT8(0, entries[0].is_signed);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, entries[0].scale);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, entries[0].offset);
    TEST_ASSERT_EQUAL_STRING("RPM", entries[0].label);

    TEST_ASSERT_EQUAL_UINT8(1, entries[1].channel);
    TEST_ASSERT_EQUAL_UINT16(0x200, entries[1].id);
    TEST_ASSERT_EQUAL_UINT8(8, entries[1].start_bit);
    TEST_ASSERT_EQUAL_UINT8(16, entries[1].bit_length);
    TEST_ASSERT_EQUAL_UINT8(1, entries[1].endian);
    TEST_ASSERT_EQUAL_UINT8(1, entries[1].is_signed);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, entries[1].scale);
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, entries[1].offset);
    TEST_ASSERT_EQUAL_STRING("Torque", entries[1].label);
}

void test_replace_all_replaces_old_entries_atomically()
{
    const CommonSignalSpec old_specs[] = {
        makeSpec(0, 0x120, 0, 8, 0, 0, 1.0f, 0.0f, "OldA"),
        makeSpec(1, 0x121, 8, 8, 0, 0, 1.0f, 0.0f, "OldB"),
    };
    const CommonSignalSpec new_specs[] = {
        makeSpec(1, 0x321, 16, 12, 1, 1, 2.0f, 3.0f, "NewOnly"),
    };

    TEST_ASSERT_TRUE(store.replaceAll(old_specs, 2));
    TEST_ASSERT_TRUE(store.replaceAll(new_specs, 1));

    TEST_ASSERT_EQUAL_size_t(1, store.count());
    const CommonSignalSpec *entries = store.entries();
    TEST_ASSERT_EQUAL_UINT8(1, entries[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x321, entries[0].id);
    TEST_ASSERT_EQUAL_STRING("NewOnly", entries[0].label);
}

void test_replace_all_rejects_count_above_limit_and_preserves_existing_entries()
{
    CommonSignalSpec valid = makeSpec(0, 0x10, 0, 8, 0, 0, 1.0f, 0.0f, "Keep");
    TEST_ASSERT_TRUE(store.replaceAll(&valid, 1));

    CommonSignalSpec too_many[kMaxCommonSignals + 1] = {};
    for (size_t i = 0; i < kMaxCommonSignals + 1; ++i)
    {
        too_many[i] = makeSpec(static_cast<uint8_t>(i & 1),
                               static_cast<uint16_t>(i),
                               0,
                               8,
                               0,
                               0,
                               1.0f,
                               0.0f,
                               "Overflow");
    }

    TEST_ASSERT_FALSE(store.replaceAll(too_many, kMaxCommonSignals + 1));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT16(0x10, store.entries()[0].id);
    TEST_ASSERT_EQUAL_STRING("Keep", store.entries()[0].label);
}

void test_replace_all_rejects_invalid_fields_and_preserves_existing_entries()
{
    const CommonSignalSpec keep = makeSpec(1, 0x55, 0, 8, 0, 0, 1.0f, 0.0f, "Keep");
    TEST_ASSERT_TRUE(store.replaceAll(&keep, 1));

    CommonSignalSpec bad_channel = makeSpec(2, 0x100, 0, 8, 0, 0, 1.0f, 0.0f, "BadChannel");
    TEST_ASSERT_FALSE(store.replaceAll(&bad_channel, 1));
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);

    CommonSignalSpec bad_id = makeSpec(0, 0x800, 0, 8, 0, 0, 1.0f, 0.0f, "BadId");
    TEST_ASSERT_FALSE(store.replaceAll(&bad_id, 1));
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);

    CommonSignalSpec bad_bit_length = makeSpec(0, 0x101, 0, 0, 0, 0, 1.0f, 0.0f, "BadLen0");
    TEST_ASSERT_FALSE(store.replaceAll(&bad_bit_length, 1));
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);

    CommonSignalSpec bad_bit_length_large = makeSpec(0, 0x102, 0, 65, 0, 0, 1.0f, 0.0f, "BadLen65");
    TEST_ASSERT_FALSE(store.replaceAll(&bad_bit_length_large, 1));
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);

    CommonSignalSpec bad_range = makeSpec(0, 0x103, 60, 8, 0, 0, 1.0f, 0.0f, "BadRange");
    TEST_ASSERT_FALSE(store.replaceAll(&bad_range, 1));
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);

    CommonSignalSpec bad_endian = makeSpec(0, 0x104, 0, 8, 2, 0, 1.0f, 0.0f, "BadEndian");
    TEST_ASSERT_FALSE(store.replaceAll(&bad_endian, 1));
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);

    CommonSignalSpec bad_signed = makeSpec(0, 0x105, 0, 8, 0, 2, 1.0f, 0.0f, "BadSigned");
    TEST_ASSERT_FALSE(store.replaceAll(&bad_signed, 1));
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);

    CommonSignalSpec bad_empty = makeSpec(0, 0x106, 0, 8, 0, 0, 1.0f, 0.0f, "X");
    bad_empty.label[0] = '\0';
    TEST_ASSERT_FALSE(store.replaceAll(&bad_empty, 1));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT8(1, store.entries()[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x55, store.entries()[0].id);
    TEST_ASSERT_EQUAL_STRING("Keep", store.entries()[0].label);
}

void test_replace_all_truncates_label_and_forces_nul_termination()
{
    const CommonSignalSpec spec = makeSpec(0,
                                           0x555,
                                           0,
                                           8,
                                           0,
                                           0,
                                           1.0f,
                                           0.0f,
                                           "123456789012345678901234567890");

    TEST_ASSERT_TRUE(store.replaceAll(&spec, 1));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_STRING("12345678901234567890123", store.entries()[0].label);
    TEST_ASSERT_EQUAL_CHAR('\0', store.entries()[0].label[kCommonSignalLabelLen - 1]);
    TEST_ASSERT_EQUAL_size_t(kCommonSignalLabelLen - 1, strlen(store.entries()[0].label));
}

void test_load_from_blob_for_test_accepts_valid_blob()
{
    CommonSignalSpec blob[] = {
        makeSpec(0, 0x200, 0, 12, 0, 0, 2.5f, 1.0f, "Speed"),
        makeSpec(1, 0x201, 12, 16, 1, 1, 0.25f, -2.0f, "Angle"),
    };

    TEST_ASSERT_TRUE(store.loadFromBlobForTest(blob, 2));
    TEST_ASSERT_EQUAL_size_t(2, store.count());
    TEST_ASSERT_EQUAL_STRING("Speed", store.entries()[0].label);
    TEST_ASSERT_EQUAL_STRING("Angle", store.entries()[1].label);
}

void test_load_from_blob_for_test_rejects_invalid_blob_and_preserves_existing_entries()
{
    const CommonSignalSpec keep = makeSpec(0, 0x99, 0, 8, 0, 0, 1.0f, 0.0f, "Keep");
    TEST_ASSERT_TRUE(store.replaceAll(&keep, 1));

    CommonSignalSpec blob[] = {
        makeSpec(0, 0x300, 0, 8, 0, 0, 1.0f, 0.0f, "ValidBeforeBad"),
        makeSpec(0, 0x301, 63, 2, 0, 0, 1.0f, 0.0f, "BadRange"),
    };

    TEST_ASSERT_FALSE(store.loadFromBlobForTest(blob, 2));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT16(0x99, store.entries()[0].id);
    TEST_ASSERT_EQUAL_STRING("Keep", store.entries()[0].label);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_replace_all_writes_multiple_entries);
    RUN_TEST(test_replace_all_replaces_old_entries_atomically);
    RUN_TEST(test_replace_all_rejects_count_above_limit_and_preserves_existing_entries);
    RUN_TEST(test_replace_all_rejects_invalid_fields_and_preserves_existing_entries);
    RUN_TEST(test_replace_all_truncates_label_and_forces_nul_termination);
    RUN_TEST(test_load_from_blob_for_test_accepts_valid_blob);
    RUN_TEST(test_load_from_blob_for_test_rejects_invalid_blob_and_preserves_existing_entries);
    return UNITY_END();
}
