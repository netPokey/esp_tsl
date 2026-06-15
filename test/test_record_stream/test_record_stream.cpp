#include <unity.h>
#include <string>
#include "analyzer/record_format.h"
#include "analyzer/recorder.h"

static CapturedFrame mk(uint32_t id, uint64_t ts_us)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = 1;
    f.channel = 0;
    f.ts_us = ts_us;
    f.data[0] = 0xAB;
    return f;
}

void test_empty_recorder_emits_header_only()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    RecordCsvCursor cur;
    char buf[256];
    size_t n = recordCsvFill(buf, sizeof(buf), r, r.count(), cur);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("time_s,channel,id,dlc,data\n", buf);
    TEST_ASSERT_EQUAL_UINT(0, recordCsvFill(buf, sizeof(buf), r, r.count(), cur));
}

void test_single_call_header_plus_all_lines()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(mk(0x100, 1000000ULL));
    r.push(mk(0x101, 1500000ULL));
    RecordCsvCursor cur;
    char buf[512];
    size_t n = recordCsvFill(buf, sizeof(buf), r, r.count(), cur);
    buf[n] = '\0';
    std::string s(buf);
    TEST_ASSERT_EQUAL_STRING(
        "time_s,channel,id,dlc,data\n"
        "0.000000,A,0x100,1,AB\n"
        "0.500000,A,0x101,1,AB\n",
        s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, recordCsvFill(buf, sizeof(buf), r, r.count(), cur));
}

void test_small_buffer_resumes_across_calls()
{
    CapturedFrame storage[8];
    Recorder r;
    r.init(storage, 8);
    r.start();
    for (uint32_t i = 0; i < 5; ++i)
        r.push(mk(0x200 + i, 1000000ULL + i * 1000000ULL));
    const size_t total = r.count();
    RecordCsvCursor curBig;
    char big[1024];
    size_t bigN = recordCsvFill(big, sizeof(big), r, total, curBig);
    big[bigN] = '\0';

    RecordCsvCursor curSmall;
    std::string acc;
    char small[40];
    int guard = 0;
    for (; guard < 100; ++guard)
    {
        size_t m = recordCsvFill(small, sizeof(small), r, total, curSmall);
        if (m == 0)
            break;
        acc.append(small, m);
    }
    TEST_ASSERT_TRUE(guard < 100);   // 确保是正常结束而非 guard 耗尽
    TEST_ASSERT_EQUAL_STRING(big, acc.c_str());
}

void test_header_exactly_fills_chunk()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(mk(0x100, 1000000ULL));
    RecordCsvCursor cur;
    char tight[27];
    size_t first = recordCsvFill(tight, sizeof(tight), r, r.count(), cur);
    TEST_ASSERT_EQUAL_UINT(strlen("time_s,channel,id,dlc,data\n"), first);
    TEST_ASSERT_TRUE(cur.header_sent);
    TEST_ASSERT_EQUAL_UINT(0, cur.frame_index);
}

void test_line_exactly_fills_chunk()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(mk(0x100, 1000000ULL));
    const size_t total = r.count();
    RecordCsvCursor cur;
    char header[27];
    TEST_ASSERT_EQUAL_UINT(sizeof(header), recordCsvFill(header, sizeof(header), r, total, cur));
    char line[24];
    size_t second = recordCsvFill(line, sizeof(line), r, total, cur);
    TEST_ASSERT_EQUAL_UINT(strlen("0.000000,A,0x100,1,AB\n"), second);
    TEST_ASSERT_EQUAL_UINT(1, cur.frame_index);
    TEST_ASSERT_EQUAL_UINT(0, recordCsvFill(line, sizeof(line), r, total, cur));
}

void test_header_fits_but_first_line_resumes_next_call()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(mk(0x100, 1000000ULL));
    const size_t total = r.count();
    RecordCsvCursor cur;
    char tight[28];
    size_t first = recordCsvFill(tight, sizeof(tight), r, total, cur);
    tight[first] = '\0';
    TEST_ASSERT_EQUAL_STRING("time_s,channel,id,dlc,data\n", tight);
    TEST_ASSERT_TRUE(cur.header_sent);
    TEST_ASSERT_EQUAL_UINT(0, cur.frame_index);
    char buf[256];
    size_t second = recordCsvFill(buf, sizeof(buf), r, total, cur);
    buf[second] = '\0';
    TEST_ASSERT_EQUAL_STRING("0.000000,A,0x100,1,AB\n", buf);
    TEST_ASSERT_EQUAL_UINT(0, recordCsvFill(buf, sizeof(buf), r, total, cur));
}

void test_base_ts_taken_from_oldest_frame()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(mk(0x300, 5000000ULL));
    r.push(mk(0x301, 7000000ULL));
    RecordCsvCursor cur;
    char buf[512];
    size_t n = recordCsvFill(buf, sizeof(buf), r, r.count(), cur);
    buf[n] = '\0';
    std::string s(buf);
    TEST_ASSERT_TRUE(s.find("0.000000,A,0x300") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("2.000000,A,0x301") != std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_recorder_emits_header_only);
    RUN_TEST(test_single_call_header_plus_all_lines);
    RUN_TEST(test_small_buffer_resumes_across_calls);
    RUN_TEST(test_header_exactly_fills_chunk);
    RUN_TEST(test_line_exactly_fills_chunk);
    RUN_TEST(test_header_fits_but_first_line_resumes_next_call);
    RUN_TEST(test_base_ts_taken_from_oldest_frame);
    return UNITY_END();
}
