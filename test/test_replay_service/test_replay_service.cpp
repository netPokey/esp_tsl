#include <unity.h>
#include <cstddef>
#include <cstdint>
#include "analyzer/replay_service.h"
#include "analyzer/recorder.h"
#include "analyzer/tx_service.h"
#include "drivers/can_driver.h"
#include "analyzer/analyzer_control.h"

class FakeCanDriver : public CanDriver
{
public:
    bool init() override { return true; }
    void setFilters(const uint32_t *, uint8_t) override {}
    bool setBusMode(CanBusMode) override { return true; }
    bool enableInterrupt(void (*)()) override { return false; }
    bool read(CanFrame &) override { return false; }
    void send(const CanFrame &frame) override
    {
        ++send_count;
        sent[send_count - 1] = frame;
    }

    int send_count = 0;
    CanFrame sent[16] = {};
};

static CapturedFrame recorder_storage[8];
static CapturedFrame replay_storage[8];
static Recorder recorder;
static FakeCanDriver canA;
static FakeCanDriver canB;
static TxService tx;
static ReplayService replay;

static CapturedFrame frame(uint32_t id, uint8_t channel, uint64_t ts_us, uint8_t dlc = 2)
{
    CapturedFrame f = {};
    f.id = id;
    f.channel = channel;
    f.ts_us = ts_us;
    f.dlc = dlc;
    for (uint8_t i = 0; i < dlc && i < 8; ++i)
        f.data[i] = static_cast<uint8_t>(id + i);
    return f;
}

static void allowTx()
{
    setCanTxEnabled(true);
    for (uint8_t channel = 0; channel < 2; ++channel)
    {
        setAnalyzerChannelTxEnabled(channel, true);
        markAnalyzerChannelOnline(channel, true);
    }
}

static void resetServices(size_t replay_capacity = 8)
{
    setCanTxEnabled(false);
    setAnalyzerChannelTxEnabled(0, false);
    setAnalyzerChannelTxEnabled(1, false);
    markAnalyzerChannelOnline(0, false);
    markAnalyzerChannelOnline(1, false);
    canA = FakeCanDriver{};
    canB = FakeCanDriver{};
    tx.init(&canA, &canB);
    recorder.init(recorder_storage, 8);
    replay.init(&recorder, &tx, replay_storage, replay_capacity);
}

static void recordFrames(const CapturedFrame *frames, size_t count)
{
    recorder.start();
    for (size_t i = 0; i < count; ++i)
        recorder.push(frames[i]);
    recorder.stop();
}

void setUp()
{
    resetServices();
}

void tearDown() {}

void test_start_rejects_missing_dependencies()
{
    ReplayService missingRecorder;
    missingRecorder.init(nullptr, &tx, replay_storage, 8);
    TEST_ASSERT_EQUAL(ReplayStartResult::RecorderUnavailable, missingRecorder.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::RecorderUnavailable, missingRecorder.lastStartResult());

    ReplayService missingTx;
    missingTx.init(&recorder, nullptr, replay_storage, 8);
    TEST_ASSERT_EQUAL(ReplayStartResult::RecorderUnavailable, missingTx.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::RecorderUnavailable, missingTx.lastStartResult());

    ReplayService missingStorage;
    missingStorage.init(&recorder, &tx, nullptr, 8);
    TEST_ASSERT_EQUAL(ReplayStartResult::RecorderUnavailable, missingStorage.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::RecorderUnavailable, missingStorage.lastStartResult());
}

void test_start_rejects_active_empty_and_over_capacity_recorder()
{
    recorder.start();
    TEST_ASSERT_EQUAL(ReplayStartResult::RecordingActive, replay.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::RecordingActive, replay.lastStartResult());
    recorder.stop();

    TEST_ASSERT_EQUAL(ReplayStartResult::Empty, replay.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::Empty, replay.lastStartResult());

    CapturedFrame frames[3] = {frame(1, 0, 0), frame(2, 0, 1000), frame(3, 0, 2000)};
    recordFrames(frames, 3);
    resetServices(2);
    recordFrames(frames, 3);
    TEST_ASSERT_EQUAL(ReplayStartResult::TooManyFrames, replay.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::TooManyFrames, replay.lastStartResult());
}

void test_start_collects_old_to_new_snapshot_and_running_rejects_second_start()
{
    CapturedFrame frames[5] = {
        frame(1, 0, 0),
        frame(2, 1, 1000),
        frame(3, 0, 2000),
        frame(4, 1, 3000),
        frame(5, 0, 4000),
    };
    recorder.init(recorder_storage, 3);
    replay.init(&recorder, &tx, replay_storage, 8);
    recordFrames(frames, 5);

    allowTx();
    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.lastStartResult());
    TEST_ASSERT_EQUAL(ReplayState::Running, replay.state());
    TEST_ASSERT_EQUAL_UINT(3, replay.total());
    TEST_ASSERT_EQUAL_UINT(0, replay.sent());
    TEST_ASSERT_EQUAL(ReplayStartResult::Busy, replay.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStartResult::Busy, replay.lastStartResult());

    replay.tick(100);
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    TEST_ASSERT_EQUAL_UINT32(3, canA.sent[0].id);
}

void test_tick_sends_first_immediately_then_schedules_by_timestamp_delta()
{
    CapturedFrame frames[3] = {
        frame(0x100, 0, 10000),
        frame(0x101, 0, 30000),
        frame(0x102, 0, 60000),
    };
    recordFrames(frames, 3);
    allowTx();

    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::Original, 100));
    replay.tick(100);
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    TEST_ASSERT_EQUAL(ReplayState::Running, replay.state());
    TEST_ASSERT_EQUAL_UINT(1, replay.sent());

    replay.tick(119);
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    replay.tick(120);
    TEST_ASSERT_EQUAL_INT(2, canA.send_count);
    TEST_ASSERT_EQUAL_UINT32(0x101, canA.sent[1].id);
    TEST_ASSERT_EQUAL(ReplayState::Running, replay.state());
    TEST_ASSERT_EQUAL_UINT(2, replay.sent());

    replay.tick(149);
    TEST_ASSERT_EQUAL_INT(2, canA.send_count);
    replay.tick(150);
    TEST_ASSERT_EQUAL_INT(3, canA.send_count);
    TEST_ASSERT_EQUAL_UINT32(0x102, canA.sent[2].id);
    TEST_ASSERT_EQUAL(ReplayState::Completed, replay.state());
    TEST_ASSERT_EQUAL_UINT(3, replay.sent());
}

void test_tick_uses_minimum_one_ms_for_sub_ms_equal_or_backward_timestamps()
{
    CapturedFrame frames[4] = {
        frame(0x200, 0, 10000),
        frame(0x201, 0, 10400),
        frame(0x202, 0, 10400),
        frame(0x203, 0, 10300),
    };
    recordFrames(frames, 4);
    allowTx();

    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::Original, 100));
    replay.tick(100);
    replay.tick(100);
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    replay.tick(110);
    TEST_ASSERT_EQUAL_INT(2, canA.send_count);
    replay.tick(110);
    TEST_ASSERT_EQUAL_INT(2, canA.send_count);
    replay.tick(120);
    TEST_ASSERT_EQUAL_INT(3, canA.send_count);
    replay.tick(120);
    TEST_ASSERT_EQUAL_INT(3, canA.send_count);
    replay.tick(130);

    TEST_ASSERT_EQUAL_INT(4, canA.send_count);
    TEST_ASSERT_EQUAL_UINT32(0x200, canA.sent[0].id);
    TEST_ASSERT_EQUAL_UINT32(0x201, canA.sent[1].id);
    TEST_ASSERT_EQUAL_UINT32(0x202, canA.sent[2].id);
    TEST_ASSERT_EQUAL_UINT32(0x203, canA.sent[3].id);
    TEST_ASSERT_EQUAL(ReplayState::Completed, replay.state());
}

void test_original_and_forced_targets_select_output_channel()
{
    CapturedFrame frames[2] = {frame(0x300, 1, 0), frame(0x301, 0, 10000)};
    recordFrames(frames, 2);
    allowTx();

    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::Original, 100));
    replay.tick(100);
    replay.tick(110);
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    TEST_ASSERT_EQUAL_INT(1, canB.send_count);
    TEST_ASSERT_EQUAL_UINT32(0x300, canB.sent[0].id);
    TEST_ASSERT_EQUAL_UINT32(0x301, canA.sent[0].id);

    resetServices();
    recordFrames(frames, 2);
    allowTx();
    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::ForceA, 200));
    replay.tick(200);
    replay.tick(210);
    TEST_ASSERT_EQUAL_INT(2, canA.send_count);
    TEST_ASSERT_EQUAL_INT(0, canB.send_count);

    resetServices();
    recordFrames(frames, 2);
    allowTx();
    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::ForceB, 300));
    replay.tick(300);
    replay.tick(310);
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
    TEST_ASSERT_EQUAL_INT(2, canB.send_count);
}

void test_tick_failure_stops_replay_and_records_last_tx_result()
{
    CapturedFrame frames[2] = {frame(0x400, 0, 0), frame(0x401, 0, 1000)};
    recordFrames(frames, 2);
    allowTx();
    setAnalyzerChannelTxEnabled(0, false);

    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::Original, 100));
    replay.tick(100);
    TEST_ASSERT_EQUAL(ReplayState::Failed, replay.state());
    TEST_ASSERT_EQUAL_UINT(0, replay.sent());
    TEST_ASSERT_EQUAL(TxSendResult::TxDisabled, replay.lastTxResult());
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
}

void test_tick_non_running_noops_and_stop_reports_status()
{
    replay.tick(100);
    TEST_ASSERT_EQUAL(ReplayStopResult::NotRunning, replay.stop());

    CapturedFrame frames[1] = {frame(0x500, 0, 0)};
    recordFrames(frames, 1);
    allowTx();
    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayStopResult::Ok, replay.stop());
    TEST_ASSERT_EQUAL(ReplayState::Stopped, replay.state());
    replay.tick(100);
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
    TEST_ASSERT_EQUAL(ReplayStopResult::NotRunning, replay.stop());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_rejects_missing_dependencies);
    RUN_TEST(test_start_rejects_active_empty_and_over_capacity_recorder);
    RUN_TEST(test_start_collects_old_to_new_snapshot_and_running_rejects_second_start);
    RUN_TEST(test_tick_sends_first_immediately_then_schedules_by_timestamp_delta);
    RUN_TEST(test_tick_uses_minimum_one_ms_for_sub_ms_equal_or_backward_timestamps);
    RUN_TEST(test_original_and_forced_targets_select_output_channel);
    RUN_TEST(test_tick_failure_stops_replay_and_records_last_tx_result);
    RUN_TEST(test_tick_non_running_noops_and_stop_reports_status);
    return UNITY_END();
}
