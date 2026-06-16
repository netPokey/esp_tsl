#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/common_signal_store.h"
#include "analyzer/label_store.h"
#include "analyzer/pretrigger_buffer.h"
#include "analyzer/recorder.h"
#include "analyzer/replay_service.h"
#include "analyzer/signal_window.h"
#include "analyzer/snapshot_store.h"
#include "analyzer/tx_service.h"

inline bool analyzerWebParseChannelToken(const char *text, uint8_t &channel)
{
    if (!text || text[1] != '\0')
        return false;
    if (text[0] == 'A' || text[0] == 'a')
    {
        channel = 0;
        return true;
    }
    if (text[0] == 'B' || text[0] == 'b')
    {
        channel = 1;
        return true;
    }
    return false;
}

inline bool analyzerWebParseSlotToken(const char *text, SnapshotSlot &slot)
{
    uint8_t channel = 0;
    if (!analyzerWebParseChannelToken(text, channel))
        return false;
    slot = channel == 1 ? SnapshotSlot::B : SnapshotSlot::A;
    return true;
}

inline uint16_t analyzerWebSampleAgeMs(uint64_t now_us, uint64_t sample_ts_us)
{
    const uint64_t age_us = now_us > sample_ts_us ? now_us - sample_ts_us : 0;
    const uint64_t age_ms = age_us / 1000;
    return age_ms > 65535 ? 65535 : static_cast<uint16_t>(age_ms);
}

inline uint16_t analyzerWebConfidenceX1000(float confidence)
{
    if (confidence <= 0.0f)
        return 0;
    if (confidence >= 1.0f)
        return 1000;
    const float scaled = confidence * 1000.0f + 0.5f;
    return static_cast<uint16_t>(scaled);
}

inline bool analyzerWebBodyChunkIsValid(size_t index, size_t len, size_t total, size_t max_total)
{
    return total <= max_total && index <= total && len <= (total - index);
}

inline bool analyzerWebBodyChunkCompletes(size_t index, size_t len, size_t total)
{
    return index <= total && len <= (total - index) && index + len == total;
}

inline bool analyzerWebParseBoundedUint(const char *text, uint32_t max_value, uint32_t &out)
{
    if (!text || text[0] == '\0')
        return false;

    uint32_t value = 0;
    uint8_t base = 10;
    size_t index = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        index = 2;
        if (text[index] == '\0')
            return false;
    }

    for (; text[index] != '\0'; ++index)
    {
        const char c = text[index];
        uint8_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint8_t>(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = static_cast<uint8_t>(10 + c - 'a');
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = static_cast<uint8_t>(10 + c - 'A');
        else
            return false;
        if (digit >= base)
            return false;
        if (value > (max_value - digit) / base)
            return false;
        value = value * base + digit;
    }

    out = value;
    return true;
}

inline bool analyzerWebParseTxId(const char *text, uint32_t &id)
{
    return analyzerWebParseBoundedUint(text, kTxServiceMaxStandardId, id);
}

inline bool analyzerWebParseTxByte(const char *text, uint8_t &byte)
{
    uint32_t value = 0;
    if (!analyzerWebParseBoundedUint(text, 0xFF, value))
        return false;
    byte = static_cast<uint8_t>(value);
    return true;
}

inline bool analyzerWebAcceptTxJsonIdInt(int value, uint32_t &id)
{
    if (value < 0 || value > static_cast<int>(kTxServiceMaxStandardId))
        return false;
    id = static_cast<uint32_t>(value);
    return true;
}

inline bool analyzerWebAcceptTxJsonByteInt(int value, uint8_t &byte)
{
    if (value < 0 || value > 0xFF)
        return false;
    byte = static_cast<uint8_t>(value);
    return true;
}

inline bool analyzerWebParseTxSendJsonFields(const char *ch, bool id_is_int, int id_value, bool dlc_is_int, int dlc_value,
                                             const bool *data_is_int, const int *data_values, size_t data_count,
                                             uint8_t &channel, uint32_t &id, uint8_t &dlc, uint8_t *data)
{
    if (!analyzerWebParseChannelToken(ch, channel))
        return false;
    if (!id_is_int || !analyzerWebAcceptTxJsonIdInt(id_value, id))
        return false;
    if (!dlc_is_int || dlc_value < 0 || dlc_value > 8)
        return false;
    dlc = static_cast<uint8_t>(dlc_value);
    if (!data_is_int || !data_values || data_count < dlc)
        return false;
    for (uint8_t i = 0; i < dlc; ++i)
        if (!data_is_int[i] || !analyzerWebAcceptTxJsonByteInt(data_values[i], data[i]))
            return false;
    return true;
}

inline int analyzerWebTxHttpStatus(TxSendResult result)
{
    switch (result)
    {
    case TxSendResult::Ok:
        return 200;
    case TxSendResult::InvalidChannel:
    case TxSendResult::InvalidId:
    case TxSendResult::InvalidDlc:
        return 400;
    case TxSendResult::TxDisabled:
        return 409;
    case TxSendResult::RateLimited:
        return 429;
    case TxSendResult::DriverUnavailable:
        return 503;
    }
    return 500;
}

inline const char *analyzerWebTxAcceptedJson()
{
    return "{\"ok\":true,\"pending\":true}";
}

inline const char *analyzerWebBadRequestJson()
{
    return "{\"ok\":false,\"error\":\"bad_request\"}";
}

inline int analyzerWebTxBodyBusyStatus()
{
    return 409;
}

constexpr uint32_t kAnalyzerWebTxBodyBusyTimeoutMs = 5000;

struct TxBodyBusyState
{
    const void *owner = nullptr;
    uint32_t acquired_ms = 0;
};

inline bool analyzerWebTryAcquireTxBody(TxBodyBusyState &state, const void *owner, uint32_t now_ms)
{
    if (!owner)
        return false;
    if (!state.owner || state.owner == owner || now_ms - state.acquired_ms > kAnalyzerWebTxBodyBusyTimeoutMs)
    {
        state.owner = owner;
        state.acquired_ms = now_ms;
        return true;
    }
    return false;
}

inline bool analyzerWebTxBodyIsOwner(const TxBodyBusyState &state, const void *owner)
{
    return owner && state.owner == owner;
}

inline void analyzerWebReleaseTxBody(TxBodyBusyState &state, const void *owner)
{
    if (analyzerWebTxBodyIsOwner(state, owner))
    {
        state.owner = nullptr;
        state.acquired_ms = 0;
    }
}

inline const char *analyzerWebTxError(TxSendResult result)
{
    switch (result)
    {
    case TxSendResult::Ok:
        return "ok";
    case TxSendResult::InvalidChannel:
        return "invalid_channel";
    case TxSendResult::DriverUnavailable:
        return "driver_unavailable";
    case TxSendResult::TxDisabled:
        return "tx_disabled";
    case TxSendResult::InvalidId:
        return "invalid_id";
    case TxSendResult::InvalidDlc:
        return "invalid_dlc";
    case TxSendResult::RateLimited:
        return "rate_limited";
    }
    return "unknown";
}

inline bool analyzerWebParseReplayTarget(const char *text, ReplayTarget &target)
{
    if (!text)
        return false;
    if (strcmp(text, "original") == 0)
    {
        target = ReplayTarget::Original;
        return true;
    }
    if (strcmp(text, "A") == 0)
    {
        target = ReplayTarget::ForceA;
        return true;
    }
    if (strcmp(text, "B") == 0)
    {
        target = ReplayTarget::ForceB;
        return true;
    }
    return false;
}

inline const char *analyzerWebReplayStateString(ReplayState state)
{
    switch (state)
    {
    case ReplayState::Idle:
        return "idle";
    case ReplayState::Running:
        return "running";
    case ReplayState::Completed:
        return "completed";
    case ReplayState::Stopped:
        return "stopped";
    case ReplayState::Failed:
        return "failed";
    }
    return "failed";
}

inline const char *analyzerWebReplayStartError(ReplayStartResult result)
{
    switch (result)
    {
    case ReplayStartResult::Ok:
        return "";
    case ReplayStartResult::Busy:
        return "busy";
    case ReplayStartResult::RecorderUnavailable:
        return "recorder_unavailable";
    case ReplayStartResult::RecordingActive:
        return "recording_active";
    case ReplayStartResult::Empty:
        return "empty_recording";
    case ReplayStartResult::TooManyFrames:
        return "too_many_frames";
    }
    return "busy";
}

inline const char *analyzerWebReplayError(ReplayStartResult start_result, TxSendResult tx_result)
{
    if (tx_result != TxSendResult::Ok)
        return analyzerWebTxError(tx_result);
    return analyzerWebReplayStartError(start_result);
}

#if defined(NATIVE_BUILD)
inline bool analyzerWebParseChannelForTest(const char *text, uint8_t &channel)
{
    return analyzerWebParseChannelToken(text, channel);
}

inline bool analyzerWebParseSlotForTest(const char *text, SnapshotSlot &slot)
{
    return analyzerWebParseSlotToken(text, slot);
}

inline uint16_t analyzerWebSampleAgeMsForTest(uint64_t now_us, uint64_t sample_ts_us)
{
    return analyzerWebSampleAgeMs(now_us, sample_ts_us);
}

inline uint16_t analyzerWebConfidenceX1000ForTest(float confidence)
{
    return analyzerWebConfidenceX1000(confidence);
}

inline bool analyzerWebBodyChunkIsValidForTest(size_t index, size_t len, size_t total, size_t max_total)
{
    return analyzerWebBodyChunkIsValid(index, len, total, max_total);
}

inline bool analyzerWebBodyChunkCompletesForTest(size_t index, size_t len, size_t total)
{
    return analyzerWebBodyChunkCompletes(index, len, total);
}

inline bool analyzerWebParseTxIdForTest(const char *text, uint32_t &id)
{
    return analyzerWebParseTxId(text, id);
}

inline bool analyzerWebParseTxByteForTest(const char *text, uint8_t &byte)
{
    return analyzerWebParseTxByte(text, byte);
}

inline bool analyzerWebAcceptTxJsonIdIntForTest(int value, uint32_t &id)
{
    return analyzerWebAcceptTxJsonIdInt(value, id);
}

inline bool analyzerWebAcceptTxJsonByteIntForTest(int value, uint8_t &byte)
{
    return analyzerWebAcceptTxJsonByteInt(value, byte);
}

inline bool analyzerWebParseTxSendJsonFieldsForTest(const char *ch, bool id_is_int, int id_value, bool dlc_is_int, int dlc_value,
                                                    const bool *data_is_int, const int *data_values, size_t data_count,
                                                    uint8_t &channel, uint32_t &id, uint8_t &dlc, uint8_t *data)
{
    return analyzerWebParseTxSendJsonFields(ch, id_is_int, id_value, dlc_is_int, dlc_value,
                                            data_is_int, data_values, data_count, channel, id, dlc, data);
}

inline int analyzerWebTxStatusForTest(TxSendResult result)
{
    return analyzerWebTxHttpStatus(result);
}

inline const char *analyzerWebTxErrorForTest(TxSendResult result)
{
    return analyzerWebTxError(result);
}

inline bool analyzerWebParseReplayTargetForTest(const char *text, ReplayTarget &target)
{
    return analyzerWebParseReplayTarget(text, target);
}

inline const char *analyzerWebReplayStateStringForTest(ReplayState state)
{
    return analyzerWebReplayStateString(state);
}

inline const char *analyzerWebReplayStartErrorForTest(ReplayStartResult result)
{
    return analyzerWebReplayStartError(result);
}

inline const char *analyzerWebReplayErrorForTest(ReplayStartResult start_result, TxSendResult tx_result)
{
    return analyzerWebReplayError(start_result, tx_result);
}

inline const char *analyzerWebTxPendingJsonForTest()
{
    return analyzerWebTxAcceptedJson();
}

inline const char *analyzerWebTxBadRequestJsonForTest()
{
    return analyzerWebBadRequestJson();
}

inline int analyzerWebTxBodyBusyStatusForTest()
{
    return analyzerWebTxBodyBusyStatus();
}

inline uint32_t analyzerWebTxBodyBusyTimeoutMsForTest()
{
    return kAnalyzerWebTxBodyBusyTimeoutMs;
}

inline bool analyzerWebTryAcquireTxBodyForTest(TxBodyBusyState &state, const void *owner, uint32_t now_ms)
{
    return analyzerWebTryAcquireTxBody(state, owner, now_ms);
}

inline bool analyzerWebTxBodyIsOwnerForTest(const TxBodyBusyState &state, const void *owner)
{
    return analyzerWebTxBodyIsOwner(state, owner);
}

inline void analyzerWebReleaseTxBodyForTest(TxBodyBusyState &state, const void *owner)
{
    analyzerWebReleaseTxBody(state, owner);
}
#endif

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels,
                           WatchedSignalWindow *signals, CommonSignalStore *common_signals,
                           Recorder *recorder, TxService *tx_service, ReplayService *replay_service = nullptr);
void analyzerWebBegin();
void analyzerWebLoop();
