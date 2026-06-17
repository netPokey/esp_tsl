#pragma once
#include <cstdint>
#include "analyzer_types.h"

class Recorder;
class ReplayService;
class IdTable;

enum class RecordTriggerMode : uint8_t
{
    Disabled,
    NewId,
    IdChange,
    AnyChange,
};

enum class RecordTriggerState : uint8_t
{
    Idle,
    Armed,
    Triggered,
    Failed,
};

enum class RecordTriggerArmResult : uint8_t
{
    Ok,
    RecorderUnavailable,
    AlreadyRecording,
    ReplayRunning,
    InvalidTarget,
};

struct RecordTriggerConfig
{
    RecordTriggerMode mode = RecordTriggerMode::Disabled;
    uint8_t channel = 0;
    uint16_t id = 0;
};

class RecordTriggerService
{
public:
    void init(Recorder *recorder, ReplayService *replay, IdTable *table);
    RecordTriggerArmResult arm(const RecordTriggerConfig &config);
    void disarm();
    void observe(const CapturedFrame &frame);

    RecordTriggerState state() const { return state_; }
    RecordTriggerMode mode() const { return config_.mode; }
    uint8_t channel() const { return config_.channel; }
    uint16_t id() const { return config_.id; }
    const char *error() const { return error_; }

private:
    bool needsTable(RecordTriggerMode mode) const;
    bool validTarget(const RecordTriggerConfig &config) const;
    bool frameChanged(const CapturedFrame &frame) const;
    void trigger();
    RecordTriggerArmResult failArm(RecordTriggerArmResult result, const char *error);

    Recorder *recorder_ = nullptr;
    ReplayService *replay_ = nullptr;
    IdTable *table_ = nullptr;
    RecordTriggerConfig config_ = {};
    RecordTriggerState state_ = RecordTriggerState::Idle;
    const char *error_ = nullptr;
};
