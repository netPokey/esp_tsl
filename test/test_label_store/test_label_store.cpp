#include <cstring>
#include <unity.h>
#include "analyzer/label_store.h"

static LabelStore store;

void setUp() { store.begin(); }
void tearDown() {}

void test_upsert_adds_entry() {
    TEST_ASSERT_TRUE(store.upsert(1, 0x123, "Motor"));

    TEST_ASSERT_EQUAL_size_t(1, store.count());
    const LabelEntry *entries = store.entries();
    TEST_ASSERT_EQUAL_UINT8(1, entries[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x123, entries[0].id);
    TEST_ASSERT_EQUAL_STRING("Motor", entries[0].text);
}

void test_upsert_overwrites_same_channel_and_id() {
    TEST_ASSERT_TRUE(store.upsert(0, 0x321, "Old"));
    TEST_ASSERT_TRUE(store.upsert(0, 0x321, "New"));

    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_STRING("New", store.entries()[0].text);
}

void test_upsert_empty_string_removes_entry() {
    TEST_ASSERT_TRUE(store.upsert(0, 0x111, "Keep"));

    TEST_ASSERT_TRUE(store.upsert(0, 0x111, ""));

    TEST_ASSERT_EQUAL_size_t(0, store.count());
}

void test_remove_shifts_remaining_entries() {
    TEST_ASSERT_TRUE(store.upsert(0, 0x100, "First"));
    TEST_ASSERT_TRUE(store.upsert(0, 0x101, "Second"));
    TEST_ASSERT_TRUE(store.upsert(1, 0x102, "Third"));

    TEST_ASSERT_TRUE(store.remove(0, 0x101));

    TEST_ASSERT_EQUAL_size_t(2, store.count());
    const LabelEntry *entries = store.entries();
    TEST_ASSERT_EQUAL_UINT16(0x100, entries[0].id);
    TEST_ASSERT_EQUAL_STRING("First", entries[0].text);
    TEST_ASSERT_EQUAL_UINT8(1, entries[1].channel);
    TEST_ASSERT_EQUAL_UINT16(0x102, entries[1].id);
    TEST_ASSERT_EQUAL_STRING("Third", entries[1].text);
}

void test_capacity_rejects_overflow() {
    for (size_t i = 0; i < kMaxLabels; ++i) {
        TEST_ASSERT_TRUE(store.upsert(0, static_cast<uint16_t>(i), "Label"));
    }

    TEST_ASSERT_FALSE(store.upsert(1, 0x700, "Overflow"));
    TEST_ASSERT_EQUAL_size_t(kMaxLabels, store.count());
}

void test_text_truncates_to_23_chars_and_nul() {
    TEST_ASSERT_TRUE(store.upsert(0, 0x555, "123456789012345678901234567890"));

    const char expected[] = "12345678901234567890123";
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_STRING(expected, store.entries()[0].text);
    TEST_ASSERT_EQUAL_CHAR('\0', store.entries()[0].text[kLabelTextLen - 1]);
    TEST_ASSERT_EQUAL_size_t(kLabelTextLen - 1, strlen(store.entries()[0].text));
}

void test_load_from_blob_rejects_invalid_channel_and_empty_text() {
    LabelEntry entries[3] = {};
    entries[0].channel = 2;
    entries[0].id = 0x100;
    strcpy(entries[0].text, "bad-channel");
    entries[1].channel = 0;
    entries[1].id = 0x101;
    entries[1].text[0] = '\0';
    entries[2].channel = 1;
    entries[2].id = 0x102;
    strcpy(entries[2].text, "ok");

    TEST_ASSERT_TRUE(store.loadFromBlobForTest(entries, 3));

    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT8(1, store.entries()[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x102, store.entries()[0].id);
    TEST_ASSERT_EQUAL_STRING("ok", store.entries()[0].text);
}

void test_load_from_blob_forces_text_termination() {
    LabelEntry entry = {};
    entry.channel = 0;
    entry.id = 0x222;
    memset(entry.text, 'A', sizeof(entry.text));

    TEST_ASSERT_TRUE(store.loadFromBlobForTest(&entry, 1));

    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_CHAR('\0', store.entries()[0].text[kLabelTextLen - 1]);
    TEST_ASSERT_EQUAL_size_t(kLabelTextLen - 1, strlen(store.entries()[0].text));
}

void test_failed_load_preserves_existing_labels() {
    TEST_ASSERT_TRUE(store.upsert(0, 0x10, "keep"));

    TEST_ASSERT_FALSE(store.loadFromBlobForTest(nullptr, kMaxLabels + 1));

    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT8(0, store.entries()[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x10, store.entries()[0].id);
    TEST_ASSERT_EQUAL_STRING("keep", store.entries()[0].text);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_upsert_adds_entry);
    RUN_TEST(test_upsert_overwrites_same_channel_and_id);
    RUN_TEST(test_upsert_empty_string_removes_entry);
    RUN_TEST(test_remove_shifts_remaining_entries);
    RUN_TEST(test_capacity_rejects_overflow);
    RUN_TEST(test_text_truncates_to_23_chars_and_nul);
    RUN_TEST(test_load_from_blob_rejects_invalid_channel_and_empty_text);
    RUN_TEST(test_load_from_blob_forces_text_termination);
    RUN_TEST(test_failed_load_preserves_existing_labels);
    return UNITY_END();
}
