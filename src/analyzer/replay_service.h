#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer/analyzer_types.h"
#include "analyzer/tx_service.h"

class Recorder;

constexpr uint32_t kReplayMinIntervalMs = 1;

enum class ReplayTarget : uint8_t
{
    Original,
    ForceA,
    ForceB,
};

enum class ReplayStartResult : uint8_t
{
    Ok,
    Busy,
    RecorderUnavailable,
    RecordingActive,
    Empty,
    TooManyFrames,
};

enum class ReplayStopResult : uint8_t
{
    Ok,
    NotRunning,
};

enum class ReplayState : uint8_t
{
    Idle,
    Running,
    Completed,
    Stopped,
    Failed,
};

class ReplayService
{
public:
    void init(Recorder *recorder, TxService *tx, CapturedFrame *storage, size_t capacity);
    ReplayStartResult start(ReplayTarget target, uint32_t now_ms);
    void tick(uint32_t now_ms);
    ReplayStopResult stop();
    ReplayState state() const { return state_; }
    size_t total() const { return total_; }
    size_t sent() const { return sent_; }
    TxSendResult lastTxResult() const { return last_tx_result_; }
    ReplayStartResult lastStartResult() const { return last_start_result_; }

private:
    uint8_t targetChannel(const CapturedFrame &frame) const;
    uint32_t intervalMs(const CapturedFrame &previous, const CapturedFrame &current) const;
    ReplayStartResult finishStart(ReplayStartResult result);

    Recorder *recorder_ = nullptr;
    TxService *tx_ = nullptr;
    CapturedFrame *storage_ = nullptr;
    size_t capacity_ = 0;
    ReplayTarget target_ = ReplayTarget::Original;
    ReplayState state_ = ReplayState::Idle;
    size_t total_ = 0;
    size_t sent_ = 0;
    uint32_t next_due_ms_ = 0;
    TxSendResult last_tx_result_ = TxSendResult::Ok;
    ReplayStartResult last_start_result_ = ReplayStartResult::Ok;
};
