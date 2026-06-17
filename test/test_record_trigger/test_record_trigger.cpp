#include <unity.h>
#include <cstring>
#include "analyzer/record_trigger.h"
#include "analyzer/recorder.h"
#include "analyzer/id_table.h"
#include "analyzer/replay_service.h"
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
        last_sent = frame;
    }

    int send_count = 0;
    CanFrame last_sent = {};
};

static CapturedFrame recorder_storage[8];
static CapturedFrame replay_storage[8];
static IdRecord id_storage[kChannelCount][kStdIdCount];
static Recorder recorder;
static IdTable table;
static FakeCanDriver can_a;
static FakeCanDriver can_b;
static TxService tx;
static ReplayService replay;
static RecordTriggerService trigger;

static CapturedFrame frame(uint32_t id, uint8_t channel, uint8_t dlc = 2, uint8_t first = 0x10)
{
    CapturedFrame f = {};
    f.id = id;
    f.channel = channel;
    f.dlc = dlc;
    f.ts_us = 1000;
    for (uint8_t i = 0; i < 8; ++i)
        f.data[i] = static_cast<uint8_t>(first + i);
    return f;
}

static void resetTxControl()
{
    setCanTxEnabled(false);
    for (uint8_t channel = 0; channel < kChannelCount; ++channel)
    {
        setAnalyzerChannelTxEnabled(channel, false);
        markAnalyzerChannelOnline(channel, false);
    }
}

static void allowTx()
{
    setCanTxEnabled(true);
    for (uint8_t channel = 0; channel < kChannelCount; ++channel)
    {
        setAnalyzerChannelTxEnabled(channel, true);
        markAnalyzerChannelOnline(channel, true);
    }
}

static void makeReplayRunning()
{
    CapturedFrame f = frame(0x100, 0);
    recorder.start();
    recorder.push(f);
    recorder.stop();
    allowTx();
    TEST_ASSERT_EQUAL(ReplayStartResult::Ok, replay.start(ReplayTarget::Original, 100));
    TEST_ASSERT_EQUAL(ReplayState::Running, replay.state());
}

static void armOk(RecordTriggerMode mode, uint8_t channel = 0, uint16_t id = 0)
{
    RecordTriggerConfig cfg{mode, channel, id};
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::Ok, trigger.arm(cfg));
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
    TEST_ASSERT_EQUAL(mode, trigger.mode());
    TEST_ASSERT_NULL(trigger.error());
}

void setUp()
{
    resetTxControl();
    recorder.init(recorder_storage, 8);
    table.init(&id_storage[0][0]);
    can_a = FakeCanDriver{};
    can_b = FakeCanDriver{};
    tx.init(&can_a, &can_b);
    replay.init(&recorder, &tx, replay_storage, 8);
    trigger.init(&recorder, &replay, &table);
}

void tearDown() {}

void test_arm_rejects_unavailable_recorder_active_recorder_and_running_replay()
{
    RecordTriggerService missing;
    missing.init(nullptr, &replay, &table);
    RecordTriggerConfig cfg{RecordTriggerMode::NewId, 1, 0x900};
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::RecorderUnavailable, missing.arm(cfg));
    TEST_ASSERT_EQUAL(RecordTriggerState::Failed, missing.state());
    TEST_ASSERT_EQUAL_STRING("recorder_unavailable", missing.error());

    recorder.start();
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::AlreadyRecording, trigger.arm(cfg));
    TEST_ASSERT_EQUAL(RecordTriggerState::Failed, trigger.state());
    TEST_ASSERT_EQUAL_STRING("already_recording", trigger.error());
    recorder.stop();

    makeReplayRunning();
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::ReplayRunning, trigger.arm(cfg));
    TEST_ASSERT_EQUAL(RecordTriggerState::Failed, trigger.state());
    TEST_ASSERT_EQUAL_STRING("replay_running", trigger.error());
}

void test_arm_validates_mode_target_and_table_requirements()
{
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::InvalidTarget,
                      trigger.arm({RecordTriggerMode::Disabled, 0, 0}));
    TEST_ASSERT_EQUAL(RecordTriggerState::Failed, trigger.state());
    TEST_ASSERT_EQUAL_STRING("invalid_target", trigger.error());

    TEST_ASSERT_EQUAL(RecordTriggerArmResult::InvalidTarget,
                      trigger.arm({RecordTriggerMode::IdChange, kChannelCount, 0x100}));
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::InvalidTarget,
                      trigger.arm({RecordTriggerMode::IdChange, 0, kStdIdCount}));

    RecordTriggerService missingTable;
    missingTable.init(&recorder, &replay, nullptr);
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::InvalidTarget,
                      missingTable.arm({RecordTriggerMode::NewId, 0, 0}));
    TEST_ASSERT_EQUAL_STRING("invalid_target", missingTable.error());

    TEST_ASSERT_EQUAL(RecordTriggerArmResult::Ok,
                      trigger.arm({RecordTriggerMode::NewId, kChannelCount, kStdIdCount}));
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
    trigger.disarm();
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::Ok,
                      trigger.arm({RecordTriggerMode::AnyChange, kChannelCount, kStdIdCount}));
}

void test_disarm_returns_to_idle_and_clears_errors()
{
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::InvalidTarget,
                      trigger.arm({RecordTriggerMode::Disabled, 0, 0}));
    TEST_ASSERT_EQUAL(RecordTriggerState::Failed, trigger.state());
    TEST_ASSERT_NOT_NULL(trigger.error());

    trigger.disarm();
    TEST_ASSERT_EQUAL(RecordTriggerState::Idle, trigger.state());
    TEST_ASSERT_NULL(trigger.error());

    trigger.observe(frame(0x222, 0));
    TEST_ASSERT_EQUAL(RecordTriggerState::Idle, trigger.state());
    TEST_ASSERT_FALSE(recorder.active());
}

void test_new_id_triggers_from_missing_old_record_and_does_not_push_frame()
{
    armOk(RecordTriggerMode::NewId, 1, 0x555);

    CapturedFrame f = frame(0x123, 1);
    trigger.observe(f);

    TEST_ASSERT_EQUAL(RecordTriggerState::Triggered, trigger.state());
    TEST_ASSERT_TRUE(recorder.active());
    TEST_ASSERT_EQUAL_UINT(0, recorder.count());
    TEST_ASSERT_NULL(trigger.error());
}

void test_new_id_does_not_trigger_when_old_record_is_present()
{
    CapturedFrame f = frame(0x123, 1);
    table.update(f);
    armOk(RecordTriggerMode::NewId);

    trigger.observe(f);

    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
    TEST_ASSERT_FALSE(recorder.active());
}

void test_any_change_triggers_only_when_existing_record_data_or_dlc_differs()
{
    CapturedFrame original = frame(0x200, 0, 2, 0x20);
    table.update(original);
    armOk(RecordTriggerMode::AnyChange);

    trigger.observe(frame(0x201, 0, 2, 0x20));
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());

    trigger.observe(original);
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());

    CapturedFrame changedData = original;
    changedData.data[1] ^= 0x01;
    trigger.observe(changedData);
    TEST_ASSERT_EQUAL(RecordTriggerState::Triggered, trigger.state());
    TEST_ASSERT_TRUE(recorder.active());
}

void test_any_change_triggers_when_existing_record_dlc_differs()
{
    CapturedFrame original = frame(0x210, 1, 2, 0x30);
    table.update(original);
    armOk(RecordTriggerMode::AnyChange);

    CapturedFrame changedDlc = original;
    changedDlc.dlc = 3;
    changedDlc.data[2] = 0x55;
    trigger.observe(changedDlc);

    TEST_ASSERT_EQUAL(RecordTriggerState::Triggered, trigger.state());
    TEST_ASSERT_TRUE(recorder.active());
}

void test_id_change_triggers_only_configured_channel_id_and_old_record_change()
{
    CapturedFrame target = frame(0x321, 1, 2, 0x40);
    table.update(target);
    armOk(RecordTriggerMode::IdChange, 1, 0x321);
    TEST_ASSERT_EQUAL_UINT8(1, trigger.channel());
    TEST_ASSERT_EQUAL_UINT16(0x321, trigger.id());

    CapturedFrame otherChannel = target;
    otherChannel.channel = 0;
    otherChannel.data[0] ^= 0x01;
    trigger.observe(otherChannel);
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());

    CapturedFrame otherId = target;
    otherId.id = 0x322;
    otherId.data[0] ^= 0x01;
    trigger.observe(otherId);
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());

    trigger.observe(target);
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());

    CapturedFrame changed = target;
    changed.data[0] ^= 0x01;
    trigger.observe(changed);
    TEST_ASSERT_EQUAL(RecordTriggerState::Triggered, trigger.state());
    TEST_ASSERT_TRUE(recorder.active());
}

void test_observe_ignores_frames_outside_table_bounds()
{
    armOk(RecordTriggerMode::NewId);
    trigger.observe(frame(kStdIdCount, 0));
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
    TEST_ASSERT_FALSE(recorder.active());

    trigger.observe(frame(0x100, kChannelCount));
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
    TEST_ASSERT_FALSE(recorder.active());

    trigger.disarm();
    CapturedFrame zero = frame(0, 0, 2, 0x10);
    table.update(zero);
    armOk(RecordTriggerMode::AnyChange);

    CapturedFrame invalidIdDifferent = frame(kStdIdCount, 0, 2, 0x60);
    trigger.observe(invalidIdDifferent);
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
    TEST_ASSERT_FALSE(recorder.active());

    CapturedFrame invalidChannelDifferent = frame(0, kChannelCount, 2, 0x70);
    trigger.observe(invalidChannelDifferent);
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
    TEST_ASSERT_FALSE(recorder.active());
}

void test_observe_uses_old_table_record_before_update()
{
    CapturedFrame f = frame(0x456, 0, 2, 0x50);
    armOk(RecordTriggerMode::NewId);

    trigger.observe(f);
    TEST_ASSERT_EQUAL(RecordTriggerState::Triggered, trigger.state());

    recorder.stop();
    table.update(f);
    trigger.disarm();
    armOk(RecordTriggerMode::NewId);
    trigger.observe(f);
    TEST_ASSERT_EQUAL(RecordTriggerState::Armed, trigger.state());
}

void test_trigger_failure_paths_do_not_start_recording()
{
    armOk(RecordTriggerMode::NewId);
    makeReplayRunning();
    trigger.observe(frame(0x600, 0));
    TEST_ASSERT_EQUAL(RecordTriggerState::Failed, trigger.state());
    TEST_ASSERT_EQUAL_STRING("replay_running", trigger.error());
    TEST_ASSERT_FALSE(recorder.active());

    trigger.disarm();
    RecordTriggerService missingRecorder;
    missingRecorder.init(nullptr, &replay, &table);
    TEST_ASSERT_EQUAL(RecordTriggerArmResult::RecorderUnavailable,
                      missingRecorder.arm({RecordTriggerMode::NewId, 0, 0}));

    replay.stop();
    trigger.disarm();
    armOk(RecordTriggerMode::NewId);
    recorder.start();
    trigger.observe(frame(0x601, 0));
    TEST_ASSERT_EQUAL(RecordTriggerState::Triggered, trigger.state());
    TEST_ASSERT_TRUE(recorder.active());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_arm_rejects_unavailable_recorder_active_recorder_and_running_replay);
    RUN_TEST(test_arm_validates_mode_target_and_table_requirements);
    RUN_TEST(test_disarm_returns_to_idle_and_clears_errors);
    RUN_TEST(test_new_id_triggers_from_missing_old_record_and_does_not_push_frame);
    RUN_TEST(test_new_id_does_not_trigger_when_old_record_is_present);
    RUN_TEST(test_any_change_triggers_only_when_existing_record_data_or_dlc_differs);
    RUN_TEST(test_any_change_triggers_when_existing_record_dlc_differs);
    RUN_TEST(test_id_change_triggers_only_configured_channel_id_and_old_record_change);
    RUN_TEST(test_observe_ignores_frames_outside_table_bounds);
    RUN_TEST(test_observe_uses_old_table_record_before_update);
    RUN_TEST(test_trigger_failure_paths_do_not_start_recording);
    return UNITY_END();
}
