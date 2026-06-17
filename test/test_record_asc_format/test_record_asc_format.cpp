#include <unity.h>
#include <cstring>
#include <string>
#include "analyzer/record_asc_format.h"
#include "analyzer/recorder.h"

static CapturedFrame makeFrame(uint32_t id, uint8_t dlc, uint8_t channel, uint64_t ts_us)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = dlc;
    f.channel = channel;
    f.ts_us = ts_us;
    for (uint8_t i = 0; i < 8; ++i)
        f.data[i] = static_cast<uint8_t>(i + 1);
    return f;
}

static void pushFrame(Recorder &rec, uint32_t id, uint8_t channel, uint64_t ts_us, uint8_t value)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = 1;
    f.channel = channel;
    f.ts_us = ts_us;
    f.data[0] = value;
    rec.push(f);
}

void test_asc_header_is_exact_text()
{
    char buf[128];
    size_t n = recordAscHeader(buf, sizeof(buf));
    const char *expected =
        "date Tue Jun 16 00:00:00.000 2026\n"
        "base hex  timestamps absolute\n"
        "internal events logged\n"
        "Begin Triggerblock\n";
    TEST_ASSERT_EQUAL_STRING(expected, buf);
    TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
}

void test_asc_footer_is_exact_text()
{
    char buf[32];
    size_t n = recordAscFooter(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("End Triggerblock\n", buf);
    TEST_ASSERT_EQUAL_UINT(strlen("End Triggerblock\n"), n);
}

void test_asc_line_formats_example_fields()
{
    CapturedFrame f = makeFrame(0x321, 8, 1, 10500ULL);
    char buf[128];
    size_t n = recordAscLine(buf, sizeof(buf), f, 0ULL);
    const char *expected = "   0.010500 2 321 Rx d 8 01 02 03 04 05 06 07 08\n";
    TEST_ASSERT_EQUAL_STRING(expected, buf);
    TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
}

void test_asc_line_maps_unknown_channel_and_pads_id()
{
    CapturedFrame f = makeFrame(0x7, 1, 9, 1000000ULL);
    char buf[128];
    recordAscLine(buf, sizeof(buf), f, 1000000ULL);
    TEST_ASSERT_EQUAL_STRING("   0.000000 0 007 Rx d 1 01\n", buf);
}

void test_asc_line_clamps_negative_relative_time_to_zero()
{
    CapturedFrame f = makeFrame(0x42, 0, 0, 500ULL);
    char buf[128];
    recordAscLine(buf, sizeof(buf), f, 1000ULL);
    TEST_ASSERT_EQUAL_STRING("   0.000000 1 042 Rx d 0\n", buf);
}

void test_asc_line_emits_at_most_eight_data_bytes()
{
    CapturedFrame f = makeFrame(0x400, 12, 0, 1000000ULL);
    char buf[128];
    recordAscLine(buf, sizeof(buf), f, 1000000ULL);
    TEST_ASSERT_EQUAL_STRING("   0.000000 1 400 Rx d 12 01 02 03 04 05 06 07 08\n", buf);
}

void test_asc_formatters_do_not_write_when_buffer_too_small()
{
    char tiny[8] = "KEEP";
    TEST_ASSERT_EQUAL_UINT(0, recordAscHeader(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("KEEP", tiny);

    TEST_ASSERT_EQUAL_UINT(0, recordAscFooter(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("KEEP", tiny);

    CapturedFrame f = makeFrame(0x123, 8, 0, 1000000ULL);
    TEST_ASSERT_EQUAL_UINT(0, recordAscLine(tiny, sizeof(tiny), f, 0ULL));
    TEST_ASSERT_EQUAL_STRING("KEEP", tiny);
}

void test_asc_fill_streams_header_frames_footer_then_zero()
{
    CapturedFrame storage[4];
    Recorder rec;
    rec.init(storage, 4);
    rec.start();
    pushFrame(rec, 0x100, 0, 1000000ULL, 0xAA);
    pushFrame(rec, 0x101, 1, 1500000ULL, 0xBB);
    const size_t total = rec.count();

    RecordAscCursor cur;
    char buf[512];
    size_t n = recordAscFill(buf, sizeof(buf), rec, total, cur);
    buf[n] = '\0';

    TEST_ASSERT_EQUAL_STRING(
        "date Tue Jun 16 00:00:00.000 2026\n"
        "base hex  timestamps absolute\n"
        "internal events logged\n"
        "Begin Triggerblock\n"
        "   0.000000 1 100 Rx d 1 AA\n"
        "   0.500000 2 101 Rx d 1 BB\n"
        "End Triggerblock\n",
        buf);
    TEST_ASSERT_EQUAL_UINT(0, recordAscFill(buf, sizeof(buf), rec, total, cur));
}

void test_asc_fill_resumes_on_record_boundaries_with_small_buffers()
{
    CapturedFrame storage[8];
    Recorder rec;
    rec.init(storage, 8);
    rec.start();
    for (uint32_t i = 0; i < 3; ++i)
        pushFrame(rec, 0x200 + i, static_cast<uint8_t>(i % 2), 1000000ULL + i * 250000ULL, static_cast<uint8_t>(0x10 + i));
    const size_t total = rec.count();

    RecordAscCursor fullCur;
    char full[512];
    size_t fullN = recordAscFill(full, sizeof(full), rec, total, fullCur);
    full[fullN] = '\0';

    RecordAscCursor smallCur;
    char chunk[111];
    std::string acc;
    int guard = 0;
    for (; guard < 100; ++guard)
    {
        size_t n = recordAscFill(chunk, sizeof(chunk), rec, total, smallCur);
        if (n == 0)
            break;
        acc.append(chunk, n);
    }

    TEST_ASSERT_TRUE(guard < 100);
    TEST_ASSERT_EQUAL_STRING(full, acc.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_asc_header_is_exact_text);
    RUN_TEST(test_asc_footer_is_exact_text);
    RUN_TEST(test_asc_line_formats_example_fields);
    RUN_TEST(test_asc_line_maps_unknown_channel_and_pads_id);
    RUN_TEST(test_asc_line_clamps_negative_relative_time_to_zero);
    RUN_TEST(test_asc_line_emits_at_most_eight_data_bytes);
    RUN_TEST(test_asc_formatters_do_not_write_when_buffer_too_small);
    RUN_TEST(test_asc_fill_streams_header_frames_footer_then_zero);
    RUN_TEST(test_asc_fill_resumes_on_record_boundaries_with_small_buffers);
    return UNITY_END();
}
