#include "analyzer/replay_service.h"
#include "analyzer/recorder.h"

void ReplayService::init(Recorder *recorder, TxService *tx, CapturedFrame *storage, size_t capacity)
{
    recorder_ = recorder;
    tx_ = tx;
    storage_ = storage;
    capacity_ = capacity;
    target_ = ReplayTarget::Original;
    state_ = ReplayState::Idle;
    total_ = 0;
    sent_ = 0;
    next_due_ms_ = 0;
    last_tx_result_ = TxSendResult::Ok;
    last_start_result_ = ReplayStartResult::Ok;
}

ReplayStartResult ReplayService::start(ReplayTarget target, uint32_t now_ms)
{
    if (state_ == ReplayState::Running)
        return finishStart(ReplayStartResult::Busy);

    if (!recorder_ || !tx_ || !storage_ || capacity_ == 0)
        return finishStart(ReplayStartResult::RecorderUnavailable);
    if (recorder_->active())
        return finishStart(ReplayStartResult::RecordingActive);

    const size_t count = recorder_->count();
    if (count == 0)
        return finishStart(ReplayStartResult::Empty);
    if (count > capacity_)
        return finishStart(ReplayStartResult::TooManyFrames);

    const size_t collected = recorder_->collect(storage_, capacity_, 0);
    if (collected == 0)
        return finishStart(ReplayStartResult::Empty);
    if (collected > capacity_)
        return finishStart(ReplayStartResult::TooManyFrames);

    target_ = target;
    state_ = ReplayState::Running;
    total_ = collected;
    sent_ = 0;
    next_due_ms_ = now_ms;
    last_tx_result_ = TxSendResult::Ok;
    return finishStart(ReplayStartResult::Ok);
}

void ReplayService::tick(uint32_t now_ms)
{
    if (state_ != ReplayState::Running)
        return;
    if (static_cast<int32_t>(now_ms - next_due_ms_) < 0)
        return;

    const CapturedFrame &frame = storage_[sent_];
    const TxSendResult result = tx_->sendSingle(targetChannel(frame), frame.id, frame.dlc, frame.data, now_ms);
    if (result != TxSendResult::Ok)
    {
        last_tx_result_ = result;
        state_ = ReplayState::Failed;
        return;
    }

    ++sent_;
    last_tx_result_ = result;

    if (sent_ >= total_)
    {
        state_ = ReplayState::Completed;
        return;
    }

    next_due_ms_ = now_ms + intervalMs(storage_[sent_ - 1], storage_[sent_]);
}

ReplayStopResult ReplayService::stop()
{
    if (state_ != ReplayState::Running)
        return ReplayStopResult::NotRunning;

    state_ = ReplayState::Stopped;
    return ReplayStopResult::Ok;
}

uint8_t ReplayService::targetChannel(const CapturedFrame &frame) const
{
    if (target_ == ReplayTarget::ForceA)
        return 0;
    if (target_ == ReplayTarget::ForceB)
        return 1;
    return frame.channel;
}

uint32_t ReplayService::intervalMs(const CapturedFrame &previous, const CapturedFrame &current) const
{
    if (current.ts_us <= previous.ts_us)
        return kReplayMinIntervalMs;

    const uint64_t delta_us = current.ts_us - previous.ts_us;
    const uint32_t delta_ms = static_cast<uint32_t>(delta_us / 1000);
    if (delta_ms < kReplayMinIntervalMs)
        return kReplayMinIntervalMs;
    return delta_ms;
}

ReplayStartResult ReplayService::finishStart(ReplayStartResult result)
{
    last_start_result_ = result;
    return result;
}
