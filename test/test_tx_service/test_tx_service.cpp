#include <unity.h>
#include "analyzer/tx_service.h"
#include "analyzer/analyzer_control.h"
#include "can_helpers.h"

class FakeCanDriver : public CanDriver
{
public:
    bool init() override { return true; }
    void setFilters(const uint32_t *, uint8_t) override {}
    bool setBusMode(CanBusMode mode) override
    {
        last_mode = mode;
        return true;
    }
    bool enableInterrupt(void (*)()) override { return false; }
    bool read(CanFrame &) override { return false; }
    void send(const CanFrame &frame) override
    {
        ++send_count;
        last_frame = frame;
    }

    int send_count = 0;
    CanFrame last_frame{};
    CanBusMode last_mode = CanBusMode::ListenOnly;
};

static FakeCanDriver canA;
static FakeCanDriver canB;
static TxService tx;
static uint8_t payload[8] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};

static void allowChannel(uint8_t channel)
{
    setCanTxEnabled(true);
    setAnalyzerChannelTxEnabled(channel, true);
    markAnalyzerChannelOnline(channel, true);
}

void setUp()
{
    setCanTxEnabled(false);
    setAnalyzerChannelTxEnabled(0, false);
    setAnalyzerChannelTxEnabled(1, false);
    markAnalyzerChannelOnline(0, false);
    markAnalyzerChannelOnline(1, false);
    canA = FakeCanDriver{};
    canB = FakeCanDriver{};
    tx.init(&canA, &canB);
}

void tearDown() {}

void test_master_off_rejects()
{
    setAnalyzerChannelTxEnabled(0, true);
    markAnalyzerChannelOnline(0, true);
    TEST_ASSERT_EQUAL(TxSendResult::TxDisabled, tx.sendSingle(0, 0x123, 1, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
}

void test_channel_tx_off_rejects()
{
    setCanTxEnabled(true);
    markAnalyzerChannelOnline(0, true);
    TEST_ASSERT_EQUAL(TxSendResult::TxDisabled, tx.sendSingle(0, 0x123, 1, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
}

void test_channel_offline_rejects()
{
    setCanTxEnabled(true);
    setAnalyzerChannelTxEnabled(0, true);
    TEST_ASSERT_EQUAL(TxSendResult::TxDisabled, tx.sendSingle(0, 0x123, 1, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
}

void test_invalid_channel_rejects()
{
    TEST_ASSERT_EQUAL(TxSendResult::InvalidChannel, tx.sendSingle(2, 0x123, 1, payload, 100));
}

void test_missing_driver_rejects()
{
    tx.init(nullptr, &canB);
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::DriverUnavailable, tx.sendSingle(0, 0x123, 1, payload, 100));
}

void test_invalid_id_rejects()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidId, tx.sendSingle(0, 0x800, 1, payload, 100));
}

void test_invalid_dlc_rejects()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidDlc, tx.sendSingle(0, 0x123, 9, payload, 100));
}

void test_null_data_with_payload_rejects()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidDlc, tx.sendSingle(0, 0x123, 1, nullptr, 100));
}

void test_dlc_zero_allows_null_data()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(0, 0x123, 0, nullptr, 100));
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    TEST_ASSERT_EQUAL_UINT8(0, canA.last_frame.dlc);
}

void test_success_sends_to_selected_driver()
{
    allowChannel(1);
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(1, 0x321, 3, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
    TEST_ASSERT_EQUAL_INT(1, canB.send_count);
    TEST_ASSERT_EQUAL_UINT32(0x321, canB.last_frame.id);
    TEST_ASSERT_EQUAL_UINT8(3, canB.last_frame.dlc);
    TEST_ASSERT_EQUAL_UINT8(0x10, canB.last_frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x12, canB.last_frame.data[2]);
}

void test_rate_limit_is_global_and_only_after_success()
{
    allowChannel(0);
    allowChannel(1);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidId, tx.sendSingle(0, 0x900, 1, payload, 100));
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(0, 0x100, 1, payload, 100));
    TEST_ASSERT_EQUAL(TxSendResult::RateLimited, tx.sendSingle(1, 0x101, 1, payload, 109));
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(1, 0x101, 1, payload, 110));
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    TEST_ASSERT_EQUAL_INT(1, canB.send_count);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_master_off_rejects);
    RUN_TEST(test_channel_tx_off_rejects);
    RUN_TEST(test_channel_offline_rejects);
    RUN_TEST(test_invalid_channel_rejects);
    RUN_TEST(test_missing_driver_rejects);
    RUN_TEST(test_invalid_id_rejects);
    RUN_TEST(test_invalid_dlc_rejects);
    RUN_TEST(test_null_data_with_payload_rejects);
    RUN_TEST(test_dlc_zero_allows_null_data);
    RUN_TEST(test_success_sends_to_selected_driver);
    RUN_TEST(test_rate_limit_is_global_and_only_after_success);
    return UNITY_END();
}
