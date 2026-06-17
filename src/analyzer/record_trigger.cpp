#include "analyzer/record_trigger.h"
#include "analyzer/id_table.h"
#include "analyzer/recorder.h"
#include "analyzer/replay_service.h"
#include <cstring>

void RecordTriggerService::init(Recorder *recorder, ReplayService *replay, IdTable *table)
{
    recorder_ = recorder;
    replay_ = replay;
    table_ = table;
    config_ = {};
    state_ = RecordTriggerState::Idle;
    error_ = nullptr;
}

RecordTriggerArmResult RecordTriggerService::arm(const RecordTriggerConfig &config)
{
    if (!recorder_)
        return failArm(RecordTriggerArmResult::RecorderUnavailable, "recorder_unavailable");
    if (recorder_->active())
        return failArm(RecordTriggerArmResult::AlreadyRecording, "already_recording");
    if (replay_ && replay_->state() == ReplayState::Running)
        return failArm(RecordTriggerArmResult::ReplayRunning, "replay_running");
    if (config.mode == RecordTriggerMode::Disabled || !validTarget(config))
        return failArm(RecordTriggerArmResult::InvalidTarget, "invalid_target");
    if (needsTable(config.mode) && !table_)
        return failArm(RecordTriggerArmResult::InvalidTarget, "invalid_target");

    config_ = config;
    state_ = RecordTriggerState::Armed;
    error_ = nullptr;
    return RecordTriggerArmResult::Ok;
}

void RecordTriggerService::disarm()
{
    config_ = {};
    state_ = RecordTriggerState::Idle;
    error_ = nullptr;
}

void RecordTriggerService::observe(const CapturedFrame &frame)
{
    if (state_ != RecordTriggerState::Armed)
        return;
    if (frame.channel >= kChannelCount || frame.id >= kStdIdCount)
        return;

    bool should_trigger = false;
    switch (config_.mode)
    {
    case RecordTriggerMode::NewId:
        should_trigger = table_ && !table_->record(frame.channel, frame.id).present;
        break;
    case RecordTriggerMode::AnyChange:
        should_trigger = table_ && table_->record(frame.channel, frame.id).present && frameChanged(frame);
        break;
    case RecordTriggerMode::IdChange:
        should_trigger = frame.channel == config_.channel && frame.id == config_.id &&
            table_ && table_->record(frame.channel, frame.id).present && frameChanged(frame);
        break;
    case RecordTriggerMode::Disabled:
        break;
    }

    if (should_trigger)
        trigger();
}

bool RecordTriggerService::needsTable(RecordTriggerMode mode) const
{
    return mode == RecordTriggerMode::NewId ||
        mode == RecordTriggerMode::IdChange ||
        mode == RecordTriggerMode::AnyChange;
}

bool RecordTriggerService::validTarget(const RecordTriggerConfig &config) const
{
    if (config.mode != RecordTriggerMode::IdChange)
        return true;
    return config.channel < kChannelCount && config.id < kStdIdCount;
}

bool RecordTriggerService::frameChanged(const CapturedFrame &frame) const
{
    const IdRecord &record = table_->record(frame.channel, frame.id);
    const uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
    if (record.dlc != dlc)
        return true;
    return std::memcmp(record.data, frame.data, dlc) != 0;
}

void RecordTriggerService::trigger()
{
    if (replay_ && replay_->state() == ReplayState::Running)
    {
        state_ = RecordTriggerState::Failed;
        error_ = "replay_running";
        return;
    }
    if (!recorder_)
    {
        state_ = RecordTriggerState::Failed;
        error_ = "recorder_unavailable";
        return;
    }
    if (!recorder_->active())
        recorder_->start();

    state_ = RecordTriggerState::Triggered;
    error_ = nullptr;
}

RecordTriggerArmResult RecordTriggerService::failArm(RecordTriggerArmResult result, const char *error)
{
    state_ = RecordTriggerState::Failed;
    error_ = error;
    return result;
}
