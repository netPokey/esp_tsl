#include <unity.h>
#include <cstring>
#include "analyzer/record_format.h"

static CapturedFrame makeFrame(uint32_t id, uint8_t dlc, uint8_t channel, uint64_t ts_us)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = dlc;
    f.channel = channel;
    f.ts_us = ts_us;
    for (uint8_t i = 0; i < 8; ++i)
        f.data[i] = static_cast<uint8_t>(0x10 + i);
    return f;
}

void test_header_is_expected_text()
{
    char buf[64];
    size_t n = recordCsvHeader(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("time_s,channel,id,dlc,data\n", buf);
    TEST_ASSERT_EQUAL_UINT(strlen("time_s,channel,id,dlc,data\n"), n);
}

void test_line_basic_fields()
{
    CapturedFrame f = makeFrame(0x123, 3, 0, 2500000ULL);
    char buf[128];
    size_t n = recordCsvLine(buf, sizeof(buf), f, 500000ULL);
    TEST_ASSERT_EQUAL_STRING("2.000000,A,0x123,3,101112\n", buf);
    TEST_ASSERT_EQUAL_UINT(strlen("2.000000,A,0x123,3,101112\n"), n);
}

void test_line_channel_b_and_id_padding()
{
    CapturedFrame f = makeFrame(0x7, 1, 1, 1000000ULL);
    char buf[128];
    recordCsvLine(buf, sizeof(buf), f, 1000000ULL);
    TEST_ASSERT_EQUAL_STRING("0.000000,B,0x007,1,10\n", buf);
}

void test_line_dlc_zero_has_empty_data()
{
    CapturedFrame f = makeFrame(0x400, 0, 0, 1000000ULL);
    char buf[128];
    recordCsvLine(buf, sizeof(buf), f, 1000000ULL);
    TEST_ASSERT_EQUAL_STRING("0.000000,A,0x400,0,\n", buf);
}

void test_line_dlc_clamped_to_eight()
{
    CapturedFrame f = makeFrame(0x400, 12, 0, 1000000ULL);
    char buf[128];
    recordCsvLine(buf, sizeof(buf), f, 1000000ULL);
    TEST_ASSERT_EQUAL_STRING("0.000000,A,0x400,12,1011121314151617\n", buf);
}

void test_line_returns_zero_when_buffer_too_small()
{
    CapturedFrame f = makeFrame(0x123, 8, 0, 1000000ULL);
    char buf[8];
    size_t n = recordCsvLine(buf, sizeof(buf), f, 0ULL);
    TEST_ASSERT_EQUAL_UINT(0, n);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_is_expected_text);
    RUN_TEST(test_line_basic_fields);
    RUN_TEST(test_line_channel_b_and_id_padding);
    RUN_TEST(test_line_dlc_zero_has_empty_data);
    RUN_TEST(test_line_dlc_clamped_to_eight);
    RUN_TEST(test_line_returns_zero_when_buffer_too_small);
    return UNITY_END();
}
