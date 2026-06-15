#include <unity.h>
#include "analyzer/signal_codec.h"

void test_extracts_8bit_intel_from_data2()
{
    const uint8_t data[8] = {0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint64_t raw = signalExtractUnsigned(data, 16, 8, SignalEndian::Intel);
    TEST_ASSERT_EQUAL_UINT64(0xAB, raw);
}

void test_extracts_16bit_intel_little_endian_across_two_bytes()
{
    const uint8_t data[8] = {0x00, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint64_t raw = signalExtractUnsigned(data, 8, 16, SignalEndian::Intel);
    TEST_ASSERT_EQUAL_UINT64(0x1234, raw);
}

void test_extracts_16bit_motorola_network_order_across_two_bytes()
{
    const uint8_t data[8] = {0x00, 0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint64_t raw = signalExtractUnsigned(data, 8, 16, SignalEndian::Motorola);
    TEST_ASSERT_EQUAL_UINT64(0x1234, raw);
}

void test_decodes_signed_8bit_twos_complement()
{
    const uint8_t data[8] = {0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    SignalSpec spec;
    spec.start_bit = 0;
    spec.bit_length = 8;
    spec.endian = SignalEndian::Intel;
    spec.is_signed = true;

    const SignalDecodeResult result = signalDecode(data, spec);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_INT64(-16, result.raw);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -16.0f, result.physical);
}

void test_applies_scale_and_offset()
{
    const uint8_t data[8] = {10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    SignalSpec spec;
    spec.start_bit = 0;
    spec.bit_length = 8;
    spec.scale = 0.5f;
    spec.offset = 1.0f;

    const SignalDecodeResult result = signalDecode(data, spec);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_INT64(10, result.raw);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 6.0f, result.physical);
}

void test_mux_match_controls_decode_validity()
{
    const uint8_t data[8] = {0xA5, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    SignalSpec spec;
    spec.start_bit = 8;
    spec.bit_length = 8;
    spec.has_mux = true;
    spec.mux_start_bit = 0;
    spec.mux_bit_length = 8;
    spec.mux_value = 0xA5;

    TEST_ASSERT_TRUE(signalMuxMatches(data, spec));
    SignalDecodeResult result = signalDecode(data, spec);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL_INT64(0x3C, result.raw);

    spec.mux_value = 0x5A;
    TEST_ASSERT_FALSE(signalMuxMatches(data, spec));
    result = signalDecode(data, spec);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL_INT64(0, result.raw);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, result.physical);
}

void test_invalid_bit_length_and_out_of_range_are_rejected()
{
    const uint8_t data[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    TEST_ASSERT_EQUAL_UINT64(0, signalExtractUnsigned(data, 0, 0, SignalEndian::Intel));
    TEST_ASSERT_EQUAL_UINT64(0, signalExtractUnsigned(data, 60, 8, SignalEndian::Motorola));

    SignalSpec spec;
    spec.start_bit = 0;
    spec.bit_length = 0;
    SignalDecodeResult result = signalDecode(data, spec);
    TEST_ASSERT_FALSE(result.valid);

    spec.start_bit = 60;
    spec.bit_length = 8;
    result = signalDecode(data, spec);
    TEST_ASSERT_FALSE(result.valid);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_extracts_8bit_intel_from_data2);
    RUN_TEST(test_extracts_16bit_intel_little_endian_across_two_bytes);
    RUN_TEST(test_extracts_16bit_motorola_network_order_across_two_bytes);
    RUN_TEST(test_decodes_signed_8bit_twos_complement);
    RUN_TEST(test_applies_scale_and_offset);
    RUN_TEST(test_mux_match_controls_decode_validity);
    RUN_TEST(test_invalid_bit_length_and_out_of_range_are_rejected);
    return UNITY_END();
}
