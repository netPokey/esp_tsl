#include <cstring>
#include <unity.h>

#include "analyzer/signal_hints.h"

namespace
{
RawSamplePoint makeSample(uint8_t b0 = 0, uint8_t b1 = 0, uint8_t b2 = 0, uint8_t b3 = 0,
                         uint8_t b4 = 0, uint8_t b5 = 0, uint8_t b6 = 0, uint8_t b7 = 0)
{
    RawSamplePoint sample{};
    sample.dlc = 8;
    sample.data[0] = b0;
    sample.data[1] = b1;
    sample.data[2] = b2;
    sample.data[3] = b3;
    sample.data[4] = b4;
    sample.data[5] = b5;
    sample.data[6] = b6;
    sample.data[7] = b7;
    return sample;
}

const SignalHint *findHint(const SignalHint *hints, size_t count, HintKind kind, uint8_t start_bit, uint8_t bit_length)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (hints[i].kind == kind && hints[i].bit_range.start_bit == start_bit &&
            hints[i].bit_range.bit_length == bit_length)
            return &hints[i];
    }
    return nullptr;
}

void assertHintsWellFormed(const SignalHint *hints, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        TEST_ASSERT_TRUE(hints[i].confidence >= 0.0f);
        TEST_ASSERT_TRUE(hints[i].confidence <= 1.0f);

        const void *nul = std::memchr(hints[i].evidence, '\0', sizeof(hints[i].evidence));
        TEST_ASSERT_NOT_NULL(nul);
        TEST_ASSERT_TRUE(std::strlen(hints[i].evidence) < sizeof(hints[i].evidence));
    }
}
}

void test_returns_zero_when_sample_count_too_small()
{
    RawSamplePoint samples[3] = {
        makeSample(0x00),
        makeSample(0x01),
        makeSample(0x02),
    };
    SignalHint hints[4] = {};

    TEST_ASSERT_EQUAL_size_t(0, signalFindHints(samples, 3, hints, 4));
}

void test_detects_obvious_4bit_rolling_counter()
{
    RawSamplePoint samples[6] = {
        makeSample(0x00),
        makeSample(0x01),
        makeSample(0x02),
        makeSample(0x03),
        makeSample(0x04),
        makeSample(0x05),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 6, hints, 4);
    const SignalHint *hint = findHint(hints, count, HintKind::Counter, 0, 4);

    TEST_ASSERT_NOT_NULL(hint);
    TEST_ASSERT_TRUE(hint->confidence >= 0.99f);
    assertHintsWellFormed(hints, count);
}

void test_detects_counter_wraparound()
{
    RawSamplePoint samples[5] = {
        makeSample(0x0E),
        makeSample(0x0F),
        makeSample(0x00),
        makeSample(0x01),
        makeSample(0x02),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 5, hints, 4);
    const SignalHint *hint = findHint(hints, count, HintKind::Counter, 0, 4);

    TEST_ASSERT_NOT_NULL(hint);
    TEST_ASSERT_TRUE(hint->confidence >= 0.99f);
    assertHintsWellFormed(hints, count);
}

void test_counter_jump_lowers_confidence()
{
    RawSamplePoint samples[6] = {
        makeSample(0x00),
        makeSample(0x01),
        makeSample(0x02),
        makeSample(0x06),
        makeSample(0x07),
        makeSample(0x08),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 6, hints, 4);
    const SignalHint *hint = findHint(hints, count, HintKind::Counter, 0, 4);

    TEST_ASSERT_NOT_NULL(hint);
    TEST_ASSERT_TRUE(hint->confidence >= 0.75f);
    TEST_ASSERT_TRUE(hint->confidence <= 0.85f);
    assertHintsWellFormed(hints, count);
}

void test_does_not_report_counter_for_heavily_disordered_series()
{
    RawSamplePoint samples[6] = {
        makeSample(0x00),
        makeSample(0x07),
        makeSample(0x03),
        makeSample(0x09),
        makeSample(0x01),
        makeSample(0x06),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 6, hints, 4);

    TEST_ASSERT_NULL(findHint(hints, count, HintKind::Counter, 0, 4));
    assertHintsWellFormed(hints, count);
}

void test_detects_mux_when_small_selector_partitions_payload_patterns()
{
    RawSamplePoint samples[6] = {
        makeSample(0x00, 0x10, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x11),
        makeSample(0x00, 0x10, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x22),
        makeSample(0x01, 0x20, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x33),
        makeSample(0x01, 0x20, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x44),
        makeSample(0x02, 0x30, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x55),
        makeSample(0x02, 0x30, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x66),
    };
    SignalHint hints[6] = {};

    const size_t count = signalFindHints(samples, 6, hints, 6);
    const SignalHint *hint = findHint(hints, count, HintKind::Mux, 0, 4);

    TEST_ASSERT_NOT_NULL(hint);
    TEST_ASSERT_TRUE(hint->confidence >= 0.6f);
    assertHintsWellFormed(hints, count);
}

void test_does_not_report_mux_for_ordinary_varying_field()
{
    RawSamplePoint samples[6] = {
        makeSample(0x00, 0x10, 0x20),
        makeSample(0x01, 0x11, 0x21),
        makeSample(0x00, 0x12, 0x22),
        makeSample(0x01, 0x13, 0x23),
        makeSample(0x00, 0x14, 0x24),
        makeSample(0x01, 0x15, 0x25),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 6, hints, 4);

    TEST_ASSERT_NULL(findHint(hints, count, HintKind::Mux, 0, 4));
    assertHintsWellFormed(hints, count);
}

void test_detects_tail_byte_checksum_candidate()
{
    RawSamplePoint samples[6] = {
        makeSample(0x10, 0x01, 0x22, 0x33, 0x44, 0x55, 0x66, 0x5A),
        makeSample(0x20, 0x02, 0x23, 0x34, 0x45, 0x56, 0x67, 0xC3),
        makeSample(0x30, 0x03, 0x24, 0x35, 0x46, 0x57, 0x68, 0x1F),
        makeSample(0x40, 0x04, 0x25, 0x36, 0x47, 0x58, 0x69, 0x88),
        makeSample(0x50, 0x05, 0x26, 0x37, 0x48, 0x59, 0x6A, 0x42),
        makeSample(0x60, 0x06, 0x27, 0x38, 0x49, 0x5A, 0x6B, 0xE7),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 6, hints, 4);
    const SignalHint *hint = findHint(hints, count, HintKind::Checksum, 56, 8);

    TEST_ASSERT_NOT_NULL(hint);
    TEST_ASSERT_TRUE(hint->confidence >= 0.6f);
    TEST_ASSERT_NOT_NULL(std::strstr(hint->evidence, "candidate"));
    assertHintsWellFormed(hints, count);
}

void test_detects_8bit_counter_candidate()
{
    RawSamplePoint samples[6] = {
        makeSample(0x10),
        makeSample(0x11),
        makeSample(0x12),
        makeSample(0x13),
        makeSample(0x14),
        makeSample(0x15),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 6, hints, 4);

    TEST_ASSERT_NOT_NULL(findHint(hints, count, HintKind::Counter, 0, 8));
    assertHintsWellFormed(hints, count);
}

void test_detects_16bit_counter_candidate()
{
    RawSamplePoint samples[6] = {
        makeSample(0x34, 0x12),
        makeSample(0x35, 0x12),
        makeSample(0x36, 0x12),
        makeSample(0x37, 0x12),
        makeSample(0x38, 0x12),
        makeSample(0x39, 0x12),
    };
    SignalHint hints[4] = {};

    const size_t count = signalFindHints(samples, 6, hints, 4);

    TEST_ASSERT_NOT_NULL(findHint(hints, count, HintKind::Counter, 0, 16));
    assertHintsWellFormed(hints, count);
}

void test_detects_tail_nibble_checksum_candidate()
{
    RawSamplePoint samples[6] = {
        makeSample(0x10, 0x01, 0x22, 0x33, 0x44, 0x55, 0x66, 0xA0),
        makeSample(0x20, 0x02, 0x23, 0x34, 0x45, 0x56, 0x67, 0xA3),
        makeSample(0x30, 0x03, 0x24, 0x35, 0x46, 0x57, 0x68, 0xAF),
        makeSample(0x40, 0x04, 0x25, 0x36, 0x47, 0x58, 0x69, 0xA8),
        makeSample(0x50, 0x05, 0x26, 0x37, 0x48, 0x59, 0x6A, 0xA2),
        makeSample(0x60, 0x06, 0x27, 0x38, 0x49, 0x5A, 0x6B, 0xA7),
    };
    SignalHint hints[8] = {};

    const size_t count = signalFindHints(samples, 6, hints, 8);
    const SignalHint *hint = findHint(hints, count, HintKind::Checksum, 60, 4);

    TEST_ASSERT_NOT_NULL(hint);
    TEST_ASSERT_NOT_NULL(std::strstr(hint->evidence, "candidate"));
    TEST_ASSERT_NULL(findHint(hints, count, HintKind::Checksum, 56, 8));
    assertHintsWellFormed(hints, count);
}

void test_respects_output_capacity()
{
    RawSamplePoint samples[6] = {
        makeSample(0x00, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A),
        makeSample(0x01, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3),
        makeSample(0x02, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F),
        makeSample(0x03, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88),
        makeSample(0x04, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42),
        makeSample(0x05, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE7),
    };
    SignalHint hints[1] = {};

    TEST_ASSERT_EQUAL_size_t(1, signalFindHints(samples, 6, hints, 1));
    assertHintsWellFormed(hints, 1);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_returns_zero_when_sample_count_too_small);
    RUN_TEST(test_detects_obvious_4bit_rolling_counter);
    RUN_TEST(test_detects_counter_wraparound);
    RUN_TEST(test_counter_jump_lowers_confidence);
    RUN_TEST(test_does_not_report_counter_for_heavily_disordered_series);
    RUN_TEST(test_detects_mux_when_small_selector_partitions_payload_patterns);
    RUN_TEST(test_does_not_report_mux_for_ordinary_varying_field);
    RUN_TEST(test_detects_tail_byte_checksum_candidate);
    RUN_TEST(test_detects_8bit_counter_candidate);
    RUN_TEST(test_detects_16bit_counter_candidate);
    RUN_TEST(test_detects_tail_nibble_checksum_candidate);
    RUN_TEST(test_respects_output_capacity);
    return UNITY_END();
}
