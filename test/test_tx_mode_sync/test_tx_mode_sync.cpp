#include <unity.h>
#include "analyzer/analyzer_control.h"
#include "analyzer/tx_mode_sync.h"

class FakeDriver : public CanDriver
{
public:
    bool init() override { return true; }
    void setFilters(const uint32_t *, uint8_t) override {}
    bool setBusMode(CanBusMode mode) override
    {
        ++setModeCalls;
        lastMode = mode;
        return true;
    }
    bool enableInterrupt(void (*)()) override { return false; }
    bool read(CanFrame &) override { return false; }
    void send(const CanFrame &) override {}

    int setModeCalls = 0;
    CanBusMode lastMode = CanBusMode::Normal;
};

void resetControls()
{
    setCanTxEnabled(false);
    setAnalyzerChannelTxEnabled(0, false);
    setAnalyzerChannelTxEnabled(1, false);
    markAnalyzerChannelOnline(0, false);
    markAnalyzerChannelOnline(1, false);
}

void test_sync_applies_initial_listen_only_once()
{
    resetControls();
    markAnalyzerChannelOnline(0, true);
    markAnalyzerChannelOnline(1, true);
    FakeDriver driverA;
    FakeDriver driverB;
    TxModeSync sync;

    sync.sync(&driverA, &driverB);
    sync.sync(&driverA, &driverB);

    TEST_ASSERT_EQUAL(1, driverA.setModeCalls);
    TEST_ASSERT_EQUAL(1, driverB.setModeCalls);
    TEST_ASSERT_EQUAL(static_cast<int>(CanBusMode::ListenOnly), static_cast<int>(driverA.lastMode));
    TEST_ASSERT_EQUAL(static_cast<int>(CanBusMode::ListenOnly), static_cast<int>(driverB.lastMode));
}

void test_sync_reapplies_when_tx_permission_changes()
{
    resetControls();
    markAnalyzerChannelOnline(0, true);
    setAnalyzerChannelTxEnabled(0, true);
    FakeDriver driverA;
    TxModeSync sync;

    sync.sync(&driverA, nullptr);
    setCanTxEnabled(true);
    sync.sync(&driverA, nullptr);
    sync.sync(&driverA, nullptr);

    TEST_ASSERT_EQUAL(2, driverA.setModeCalls);
    TEST_ASSERT_EQUAL(static_cast<int>(CanBusMode::Normal), static_cast<int>(driverA.lastMode));
}

void test_sync_skips_offline_channels()
{
    resetControls();
    FakeDriver driverA;
    TxModeSync sync;

    sync.sync(&driverA, nullptr);

    TEST_ASSERT_EQUAL(0, driverA.setModeCalls);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sync_applies_initial_listen_only_once);
    RUN_TEST(test_sync_reapplies_when_tx_permission_changes);
    RUN_TEST(test_sync_skips_offline_channels);
    return UNITY_END();
}
