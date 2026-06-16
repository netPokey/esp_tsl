#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_control.h"
#include "analyzer/analyzer_wifi.h"
#include "analyzer/record_format.h"
#include "analyzer/recorder.h"
#include "analyzer/signal_hints.h"
#include "analyzer/ws_protocol.h"
#include "can_helpers.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <memory>

namespace
{
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

FrameQueue *g_queue = nullptr;
IdTable *g_table = nullptr;
BusStatsTracker *g_stats = nullptr;
PretriggerBuffer *g_pretrigger = nullptr;
SnapshotStore *g_snapshots = nullptr;
LabelStore *g_labels = nullptr;
WatchedSignalWindow *g_signals = nullptr;
CommonSignalStore *g_commonSignals = nullptr;
Recorder *g_recorder = nullptr;
TxService *g_txService = nullptr;

constexpr uint32_t kDirtyKeys = static_cast<uint32_t>(kChannelCount) * kStdIdCount;
uint8_t g_dirty[kDirtyKeys / 8] = {};

constexpr size_t kPushBufBytes = 1400;
constexpr size_t kMaxWsBatchRecords = 255;
constexpr size_t wsBatchCapacity(size_t byteCapacity)
{
    return byteCapacity < kMaxWsBatchRecords ? byteCapacity : kMaxWsBatchRecords;
}
constexpr size_t kFrameDeltaBatchCapacity = wsBatchCapacity((kPushBufBytes - 2) / sizeof(WsFrameRecord));
constexpr size_t kSnapshotDiffBatchCapacity = wsBatchCapacity((kPushBufBytes - 3) / sizeof(WsDiffRecord));
constexpr size_t kPretriggerBatchCapacity = wsBatchCapacity((kPushBufBytes - 3) / sizeof(WsPretriggerRecord));
constexpr size_t kBaselineBatchCapacity = wsBatchCapacity((kPushBufBytes - 3) / sizeof(WsBaselineRecord));
constexpr size_t kSignalSampleBatchCapacity = wsBatchCapacity((kPushBufBytes - 3) / sizeof(WsSignalSampleRecord));
constexpr size_t kSignalHintBatchCapacity = wsBatchCapacity((kPushBufBytes - 3) / sizeof(WsSignalHintRecord));
constexpr size_t kMaxSignalSamples = 64;
constexpr size_t kMaxSignalHints = 8;
static_assert(kFrameDeltaBatchCapacity >= 1, "frame delta batch capacity must be non-zero");
static_assert(kSnapshotDiffBatchCapacity >= 1, "snapshot diff batch capacity must be non-zero");
static_assert(kPretriggerBatchCapacity >= 1, "pretrigger batch capacity must be non-zero");
static_assert(kBaselineBatchCapacity >= 1, "baseline batch capacity must be non-zero");
static_assert(kSignalSampleBatchCapacity >= 1, "signal sample batch capacity must be non-zero");
static_assert(kSignalHintBatchCapacity >= 1, "signal hint batch capacity must be non-zero");
uint32_t g_lastPushMs = 0;
uint32_t g_lastStatsMs = 0;
constexpr uint32_t kPushIntervalMs = 66;
constexpr uint32_t kStatsIntervalMs = 1000;

enum class PendingCmdType : uint8_t
{
    TxMaster,
    TxEnable,
    Snapshot,
    Diff,
    Baseline,
    Mark,
    LabelSet,
    LabelDelete,
    P4Watch,
    P4Hints,
    RecordStart,
    RecordStop,
    TxSend
};

struct PendingCmd
{
    PendingCmdType type = PendingCmdType::Diff;
    uint8_t channel = 0;
    bool on = false;
    SnapshotSlot slot = SnapshotSlot::A;
    uint16_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
    char text[kLabelTextLen] = {};
};

constexpr size_t kPendingCmdCap = 8;
constexpr size_t kMaxWsCommandBytes = 256;
constexpr size_t kMaxCommonSignalsJsonBytes = 4096;
constexpr size_t kMaxWifiJsonBytes = 256;
constexpr size_t kMaxTxJsonBytes = 256;
constexpr uint32_t kPowerActionDelayMs = 200;
constexpr uint32_t kShutdownSleepDelayMs = 100;
PendingCmd g_pending[kPendingCmdCap];
volatile size_t g_pendingHead = 0;
volatile size_t g_pendingTail = 0;
uint32_t g_pendingDropped = 0;
portMUX_TYPE g_pendingMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_labelMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_wifiMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_powerMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_txBodyMux = portMUX_INITIALIZER_UNLOCKED;
char g_commonSignalBody[kMaxCommonSignalsJsonBytes] = {};
char g_wifiBody[kMaxWifiJsonBytes + 1] = {};
char g_txBody[kMaxTxJsonBytes + 1] = {};
TxBodyBusyState g_txBodyState;
AnalyzerWifiCredentials g_pendingWifi;
volatile bool g_pendingWifiValid = false;

enum class PendingPowerAction : uint8_t
{
    None,
    Restart,
    ShutdownPrepare,
    ShutdownSleep
};

volatile PendingPowerAction g_pendingPowerAction = PendingPowerAction::None;
volatile uint32_t g_pendingPowerExecuteAfterMs = 0;

bool labelUpsert(uint8_t channel, uint16_t id, const char *text)
{
    if (!g_labels)
        return false;

    portENTER_CRITICAL(&g_labelMux);
    const bool updated = g_labels->upsert(channel, id, text, false);
    portEXIT_CRITICAL(&g_labelMux);
    if (updated)
        g_labels->save();
    return updated;
}

bool labelRemove(uint8_t channel, uint16_t id)
{
    if (!g_labels)
        return false;

    portENTER_CRITICAL(&g_labelMux);
    const bool removed = g_labels->remove(channel, id, false);
    portEXIT_CRITICAL(&g_labelMux);
    if (removed)
        g_labels->save();
    return removed;
}

size_t copyLabels(LabelEntry *out, size_t capacity)
{
    if (!g_labels || !out || capacity == 0)
        return 0;

    portENTER_CRITICAL(&g_labelMux);
    const size_t count = g_labels->count() < capacity ? g_labels->count() : capacity;
    const LabelEntry *entries = g_labels->entries();
    for (size_t i = 0; i < count; ++i)
        out[i] = entries[i];
    portEXIT_CRITICAL(&g_labelMux);
    return count;
}

size_t copyCommonSignals(CommonSignalSpec *out, size_t capacity)
{
    if (!g_commonSignals || !out || capacity == 0)
        return 0;

    const size_t count = g_commonSignals->count() < capacity ? g_commonSignals->count() : capacity;
    const CommonSignalSpec *entries = g_commonSignals->entries();
    for (size_t i = 0; i < count; ++i)
        out[i] = entries[i];
    return count;
}

bool replaceCommonSignals(const CommonSignalSpec *entries, size_t count)
{
    if (!g_commonSignals)
        return false;
    return g_commonSignals->replaceAll(entries, count);
}

String wifiStatusJson(bool ok = true, bool connected = false, bool includeConnected = false)
{
    const AnalyzerWifiStatus st = analyzerWifiStatus();
    JsonDocument doc;
    doc["ok"] = ok;
    if (includeConnected)
        doc["connected"] = connected;
    doc["mode"] = st.sta ? "sta" : (st.ap ? "ap" : "off");
    doc["ip"] = st.ip;
    doc["ssid"] = st.ssid;
    doc["pass"] = st.pass;
    String out;
    serializeJson(doc, out);
    return out;
}

bool parseCommonSignalSpec(JsonVariantConst src, CommonSignalSpec &out)
{
    const char *ch = src["ch"] | nullptr;
    uint8_t channel = 0;
    if (!analyzerWebParseChannelToken(ch, channel))
        return false;

    const JsonVariantConst idVar = src["id"];
    const JsonVariantConst startBitVar = src["start_bit"];
    const JsonVariantConst bitLengthVar = src["bit_length"];
    const JsonVariantConst endianVar = src["endian"];
    const JsonVariantConst signedVar = src["signed"];
    const JsonVariantConst scaleVar = src["scale"];
    const JsonVariantConst offsetVar = src["offset"];
    if (!idVar.is<int>() || !startBitVar.is<int>() || !bitLengthVar.is<int>() || !endianVar.is<int>() ||
        !signedVar.is<int>() || !scaleVar.is<float>() || !offsetVar.is<float>())
        return false;

    const int id = idVar.as<int>();
    const int startBit = startBitVar.as<int>();
    const int bitLength = bitLengthVar.as<int>();
    const int endian = endianVar.as<int>();
    const int isSigned = signedVar.as<int>();
    if (id < 0 || id >= kStdIdCount || startBit < 0 || startBit > 63 || bitLength < 0 || bitLength > 64 ||
        endian < 0 || endian > 255 || isSigned < 0 || isSigned > 255)
        return false;

    const char *label = src["label"] | nullptr;
    if (!label || label[0] == '\0')
        return false;

    memset(&out, 0, sizeof(out));
    out.channel = channel;
    out.id = static_cast<uint16_t>(id);
    out.start_bit = static_cast<uint8_t>(startBit);
    out.bit_length = static_cast<uint8_t>(bitLength);
    out.endian = static_cast<uint8_t>(endian);
    out.is_signed = static_cast<uint8_t>(isSigned);
    out.scale = scaleVar.as<float>();
    out.offset = offsetVar.as<float>();
    strlcpy(out.label, label, sizeof(out.label));
    return true;
}

bool parseTxSendRequest(JsonDocument &doc, uint8_t &channel, uint32_t &id, uint8_t &dlc, uint8_t *data)
{
    JsonArrayConst arr = doc["data"].as<JsonArrayConst>();
    bool dataIsInt[8] = {};
    int dataValues[8] = {};
    const size_t count = arr.isNull() ? 0 : (arr.size() > 8 ? 8 : arr.size());
    for (size_t i = 0; i < count; ++i)
    {
        dataIsInt[i] = arr[i].is<int>();
        if (dataIsInt[i])
            dataValues[i] = arr[i].as<int>();
    }
    return analyzerWebParseTxSendJsonFields(doc["ch"] | nullptr,
                                            doc["id"].is<int>(), doc["id"].as<int>(),
                                            doc["dlc"].is<int>(), doc["dlc"].as<int>(),
                                            dataIsInt, dataValues, count,
                                            channel, id, dlc, data);
}

void jsonAddCommonSignal(JsonArray array, const CommonSignalSpec &spec)
{
    JsonObject signal = array.createNestedObject();
    signal["ch"] = spec.channel == 1 ? "B" : "A";
    signal["id"] = spec.id;
    signal["label"] = spec.label;
    signal["start_bit"] = spec.start_bit;
    signal["bit_length"] = spec.bit_length;
    signal["endian"] = spec.endian;
    signal["signed"] = spec.is_signed;
    signal["scale"] = spec.scale;
    signal["offset"] = spec.offset;
}

void fillSignalSampleRecord(WsSignalSampleRecord &out, uint8_t channel, uint16_t id,
                            const RawSamplePoint &sample, uint64_t nowUs)
{
    out.channel = channel;
    out.id = id;
    out.dlc = sample.dlc;
    for (uint8_t i = 0; i < 8; ++i)
        out.data[i] = sample.data[i];
    out.sample_age_ms = analyzerWebSampleAgeMs(nowUs, sample.ts_us);
    out.sequence_lo = sample.sequence;
}

void fillSignalHintRecord(WsSignalHintRecord &out, const SignalHint &hint)
{
    out.kind = static_cast<uint8_t>(hint.kind);
    out.start_bit = hint.bit_range.start_bit;
    out.bit_length = hint.bit_range.bit_length;
    out.confidence_x1000 = analyzerWebConfidenceX1000(hint.confidence);
    memset(out.evidence, 0, sizeof(out.evidence));
    strncpy(out.evidence, hint.evidence, sizeof(out.evidence) - 1);
}

void sendSignalSamples(uint8_t channel, uint16_t id)
{
    if (!g_signals || ws.count() == 0)
        return;

    RawSamplePoint samples[kMaxSignalSamples] = {};
    const size_t count = g_signals->copySamples(channel, id, samples, kMaxSignalSamples);
    static WsSignalSampleRecord wire[kSignalSampleBatchCapacity];
    static uint8_t buf[kPushBufBytes];
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    size_t offset = 0;

    while (offset < count)
    {
        const size_t batchCount = (count - offset) < kSignalSampleBatchCapacity ? (count - offset) : kSignalSampleBatchCapacity;
        for (size_t i = 0; i < batchCount; ++i)
            fillSignalSampleRecord(wire[i], channel, id, samples[offset + i], nowUs);
        const size_t bytes = wsBuildSignalSamples(buf, sizeof(buf), wire, static_cast<uint8_t>(batchCount));
        if (bytes > 0)
            ws.binaryAll(buf, bytes);
        offset += batchCount;
    }

    if (count == 0)
    {
        const size_t bytes = wsBuildSignalSamples(buf, sizeof(buf), nullptr, 0);
        if (bytes > 0)
            ws.binaryAll(buf, bytes);
    }
}

void sendSignalHints(uint8_t channel, uint16_t id)
{
    if (!g_signals || ws.count() == 0)
        return;

    RawSamplePoint samples[kMaxSignalSamples] = {};
    SignalHint hints[kMaxSignalHints] = {};
    WsSignalHintRecord wire[kSignalHintBatchCapacity] = {};
    static uint8_t buf[kPushBufBytes];
    const size_t sampleCount = g_signals->copySamples(channel, id, samples, kMaxSignalSamples);
    const size_t hintCount = signalFindHints(samples, sampleCount, hints, kMaxSignalHints);
    const size_t batchCount = hintCount < kSignalHintBatchCapacity ? hintCount : kSignalHintBatchCapacity;
    for (size_t i = 0; i < batchCount; ++i)
        fillSignalHintRecord(wire[i], hints[i]);
    const size_t bytes = wsBuildSignalHints(buf, sizeof(buf), wire, static_cast<uint8_t>(batchCount));
    if (bytes > 0)
        ws.binaryAll(buf, bytes);
}

void sendLabelUpdateNotice()
{
    if (ws.count() == 0)
        return;

    uint8_t buf[3] = {};
    buf[0] = WS_MSG_DIFF;
    buf[1] = WS_DIFF_LABELS;
    buf[2] = 0;
    ws.binaryAll(buf, sizeof(buf));
}

void sendSnapshotDiff()
{
    if (!g_snapshots || ws.count() == 0)
        return;

    static SnapshotDiffRecord diffs[kSnapshotDiffBatchCapacity];
    static WsDiffRecord wire[kSnapshotDiffBatchCapacity];
    static uint8_t buf[kPushBufBytes];
    size_t skip = 0;

    while (true)
    {
        const size_t n = g_snapshots->diff(diffs, kSnapshotDiffBatchCapacity, skip);
        for (size_t i = 0; i < n; ++i)
        {
            wire[i].channel = diffs[i].channel;
            wire[i].id = diffs[i].id;
            wire[i].kind = diffs[i].kind;
            wire[i].dlc_a = diffs[i].dlc_a;
            wire[i].dlc_b = diffs[i].dlc_b;
            for (uint8_t b = 0; b < 8; ++b)
            {
                wire[i].data_a[b] = diffs[i].data_a[b];
                wire[i].data_b[b] = diffs[i].data_b[b];
            }
        }

        const size_t bytes = wsBuildSnapshotDiff(buf, sizeof(buf), wire, static_cast<uint8_t>(n));
        if ((n > 0 || skip == 0) && bytes > 0)
            ws.binaryAll(buf, bytes);
        if (n < kSnapshotDiffBatchCapacity)
            break;
        skip += n;
    }
}

void sendPretrigger()
{
    if (!g_pretrigger || ws.count() == 0)
        return;

    static WsPretriggerRecord recs[kPretriggerBatchCapacity];
    static uint8_t buf[kPushBufBytes];
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    size_t skip = 0;

    while (true)
    {
        const size_t n = g_pretrigger->summarize(nowUs, 5000000UL, recs, kPretriggerBatchCapacity, skip);
        const size_t bytes = wsBuildPretrigger(buf, sizeof(buf), recs, static_cast<uint8_t>(n));
        if ((n > 0 || skip == 0) && bytes > 0)
            ws.binaryAll(buf, bytes);
        if (n < kPretriggerBatchCapacity)
            break;
        skip += n;
    }
}

void sendBaseline()
{
    if (!g_table || ws.count() == 0)
        return;

    static WsBaselineRecord recs[kBaselineBatchCapacity];
    static uint8_t buf[kPushBufBytes];
    size_t n = 0;

    auto flush = [&]() {
        if (n == 0)
            return;
        const size_t bytes = wsBuildBaseline(buf, sizeof(buf), recs, static_cast<uint8_t>(n));
        if (bytes > 0)
            ws.binaryAll(buf, bytes);
        n = 0;
    };

    for (uint8_t ch = 0; ch < kChannelCount; ++ch)
    {
        for (uint32_t id = 0; id < kStdIdCount; ++id)
        {
            if (!g_table->record(ch, id).present)
                continue;
            recs[n].channel = ch;
            recs[n].id = static_cast<uint16_t>(id);
            ++n;
            if (n >= kBaselineBatchCapacity)
                flush();
        }
    }
    flush();
}

void markDirty(uint8_t channel, uint32_t id)
{
    const uint32_t key = static_cast<uint32_t>(channel) * kStdIdCount + id;
    if (key < kDirtyKeys)
        g_dirty[key >> 3] |= static_cast<uint8_t>(1u << (key & 7));
}

bool takeTxBodyBuffer(AsyncWebServerRequest *request, uint32_t nowMs)
{
    portENTER_CRITICAL(&g_txBodyMux);
    const bool acquired = analyzerWebTryAcquireTxBody(g_txBodyState, request, nowMs);
    portEXIT_CRITICAL(&g_txBodyMux);
    return acquired;
}

bool txBodyBufferIsOwner(AsyncWebServerRequest *request)
{
    portENTER_CRITICAL(&g_txBodyMux);
    const bool isOwner = analyzerWebTxBodyIsOwner(g_txBodyState, request);
    portEXIT_CRITICAL(&g_txBodyMux);
    return isOwner;
}

void releaseTxBodyBuffer(AsyncWebServerRequest *request)
{
    portENTER_CRITICAL(&g_txBodyMux);
    analyzerWebReleaseTxBody(g_txBodyState, request);
    portEXIT_CRITICAL(&g_txBodyMux);
}

bool enqueuePendingCommand(const PendingCmd &cmd)
{
    bool queued = false;
    portENTER_CRITICAL(&g_pendingMux);
    const size_t head = g_pendingHead;
    const size_t next = (head + 1) % kPendingCmdCap;
    if (next != g_pendingTail)
    {
        g_pending[head] = cmd;
        g_pendingHead = next;
        queued = true;
    }
    else
    {
        ++g_pendingDropped;
    }
    portEXIT_CRITICAL(&g_pendingMux);
    return queued;
}

uint32_t pendingDroppedCount()
{
    portENTER_CRITICAL(&g_pendingMux);
    const uint32_t dropped = g_pendingDropped;
    portEXIT_CRITICAL(&g_pendingMux);
    return dropped;
}

bool dequeuePendingCommand(PendingCmd &cmd)
{
    bool found = false;
    portENTER_CRITICAL(&g_pendingMux);
    const size_t tail = g_pendingTail;
    if (tail != g_pendingHead)
    {
        cmd = g_pending[tail];
        g_pendingTail = (tail + 1) % kPendingCmdCap;
        found = true;
    }
    portEXIT_CRITICAL(&g_pendingMux);
    return found;
}

void enqueuePendingWifi(const AnalyzerWifiCredentials &credentials)
{
    portENTER_CRITICAL(&g_wifiMux);
    g_pendingWifi = credentials;
    g_pendingWifiValid = true;
    portEXIT_CRITICAL(&g_wifiMux);
}

bool takePendingWifi(AnalyzerWifiCredentials &credentials)
{
    bool found = false;
    portENTER_CRITICAL(&g_wifiMux);
    if (g_pendingWifiValid)
    {
        credentials = g_pendingWifi;
        g_pendingWifiValid = false;
        found = true;
    }
    portEXIT_CRITICAL(&g_wifiMux);
    return found;
}

void enqueuePendingPowerAction(PendingPowerAction action, uint32_t executeAfterMs)
{
    portENTER_CRITICAL(&g_powerMux);
    g_pendingPowerAction = action;
    g_pendingPowerExecuteAfterMs = executeAfterMs;
    portEXIT_CRITICAL(&g_powerMux);
}

PendingPowerAction peekPendingPowerAction(uint32_t &executeAfterMs)
{
    portENTER_CRITICAL(&g_powerMux);
    const PendingPowerAction action = g_pendingPowerAction;
    executeAfterMs = g_pendingPowerExecuteAfterMs;
    portEXIT_CRITICAL(&g_powerMux);
    return action;
}

void clearPendingPowerAction(PendingPowerAction action)
{
    portENTER_CRITICAL(&g_powerMux);
    if (g_pendingPowerAction == action)
    {
        g_pendingPowerAction = PendingPowerAction::None;
        g_pendingPowerExecuteAfterMs = 0;
    }
    portEXIT_CRITICAL(&g_powerMux);
}

void processPendingWifi()
{
    AnalyzerWifiCredentials credentials;
    if (takePendingWifi(credentials))
        analyzerWifiSaveAndConnect(credentials.ssid, credentials.pass);
}

void processPendingPowerAction()
{
    uint32_t executeAfterMs = 0;
    const PendingPowerAction action = peekPendingPowerAction(executeAfterMs);
    if (action == PendingPowerAction::None || static_cast<int32_t>(millis() - executeAfterMs) < 0)
        return;

    switch (action)
    {
    case PendingPowerAction::Restart:
        clearPendingPowerAction(action);
        ESP.restart();
        break;
    case PendingPowerAction::ShutdownPrepare:
        setCanTxEnabled(false);
        setAnalyzerChannelTxEnabled(0, false);
        setAnalyzerChannelTxEnabled(1, false);
        enqueuePendingPowerAction(PendingPowerAction::ShutdownSleep, millis() + kShutdownSleepDelayMs);
        break;
    case PendingPowerAction::ShutdownSleep:
        clearPendingPowerAction(action);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        esp_deep_sleep_start();
        break;
    case PendingPowerAction::None:
        break;
    }
}

void processPendingCommand(const PendingCmd &cmd)
{
    switch (cmd.type)
    {
    case PendingCmdType::TxMaster:
        setCanTxEnabled(cmd.on);
        break;
    case PendingCmdType::TxEnable:
        setAnalyzerChannelTxEnabled(cmd.channel, cmd.on);
        break;
    case PendingCmdType::Snapshot:
        if (g_snapshots && g_table)
            g_snapshots->capture(cmd.slot, *g_table);
        break;
    case PendingCmdType::Diff:
        sendSnapshotDiff();
        break;
    case PendingCmdType::Baseline:
        sendBaseline();
        break;
    case PendingCmdType::Mark:
        sendPretrigger();
        break;
    case PendingCmdType::LabelSet:
        if (labelUpsert(cmd.channel, cmd.id, cmd.text))
            sendLabelUpdateNotice();
        break;
    case PendingCmdType::LabelDelete:
        if (labelRemove(cmd.channel, cmd.id))
            sendLabelUpdateNotice();
        break;
    case PendingCmdType::P4Watch:
        if (!g_signals)
            break;
        if (cmd.on)
            g_signals->watch(cmd.channel, cmd.id);
        else
            g_signals->unwatch(cmd.channel, cmd.id);
        break;
    case PendingCmdType::P4Hints:
        sendSignalSamples(cmd.channel, cmd.id);
        sendSignalHints(cmd.channel, cmd.id);
        break;
    case PendingCmdType::RecordStart:
        if (g_recorder)
            g_recorder->start();
        break;
    case PendingCmdType::RecordStop:
        if (g_recorder)
            g_recorder->stop();
        break;
    case PendingCmdType::TxSend:
        if (!g_txService)
        {
            Serial.println("[tx] send failed: driver_unavailable");
            break;
        }
        {
            const TxSendResult result = g_txService->sendSingle(cmd.channel, cmd.id, cmd.dlc, cmd.data, millis());
            char msg[64];
            snprintf(msg, sizeof(msg), "[tx] send %s", result == TxSendResult::Ok ? "ok" : analyzerWebTxError(result));
            Serial.println(msg);
        }
        break;
    }
}

void processPendingCommands()
{
    PendingCmd cmd;
    while (dequeuePendingCommand(cmd))
        processPendingCommand(cmd);
}

void handleCommand(const char *text, size_t len)
{
    JsonDocument doc;
    if (deserializeJson(doc, text, len))
        return;

    const char *cmd = doc["cmd"] | "";
    PendingCmd pending;
    if (strcmp(cmd, "tx_master") == 0)
    {
        pending.type = PendingCmdType::TxMaster;
        pending.on = doc["on"] | false;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "tx_enable") == 0)
    {
        const char *ch = doc["ch"] | nullptr;
        if (!analyzerWebParseChannelToken(ch, pending.channel))
            return;
        pending.type = PendingCmdType::TxEnable;
        pending.on = doc["on"] | false;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "snapshot") == 0)
    {
        const char *slot = doc["slot"] | nullptr;
        if (!analyzerWebParseSlotToken(slot, pending.slot))
            return;
        pending.type = PendingCmdType::Snapshot;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "diff") == 0)
    {
        pending.type = PendingCmdType::Diff;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "baseline") == 0)
    {
        pending.type = PendingCmdType::Baseline;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "mark") == 0)
    {
        pending.type = PendingCmdType::Mark;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "label_set") == 0)
    {
        const char *ch = doc["ch"] | nullptr;
        const int id = doc["id"] | -1;
        if (!analyzerWebParseChannelToken(ch, pending.channel) || id < 0 || id >= kStdIdCount)
            return;
        pending.type = PendingCmdType::LabelSet;
        pending.id = static_cast<uint16_t>(id);
        strlcpy(pending.text, doc["text"] | "", sizeof(pending.text));
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "label_delete") == 0)
    {
        const char *ch = doc["ch"] | nullptr;
        const int id = doc["id"] | -1;
        if (!analyzerWebParseChannelToken(ch, pending.channel) || id < 0 || id >= kStdIdCount)
            return;
        pending.type = PendingCmdType::LabelDelete;
        pending.id = static_cast<uint16_t>(id);
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "p4_watch") == 0)
    {
        const char *ch = doc["ch"] | nullptr;
        const int id = doc["id"] | -1;
        if (!analyzerWebParseChannelToken(ch, pending.channel) || id < 0 || id >= kStdIdCount)
            return;
        pending.type = PendingCmdType::P4Watch;
        pending.id = static_cast<uint16_t>(id);
        pending.on = doc["on"] | false;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "p4_hints") == 0)
    {
        const char *ch = doc["ch"] | nullptr;
        const int id = doc["id"] | -1;
        if (!analyzerWebParseChannelToken(ch, pending.channel) || id < 0 || id >= kStdIdCount)
            return;
        pending.type = PendingCmdType::P4Hints;
        pending.id = static_cast<uint16_t>(id);
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "record_start") == 0)
    {
        pending.type = PendingCmdType::RecordStart;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "record_stop") == 0)
    {
        pending.type = PendingCmdType::RecordStop;
        enqueuePendingCommand(pending);
        return;
    }
}

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type != WS_EVT_DATA)
        return;

    const AwsFrameInfo *info = static_cast<const AwsFrameInfo *>(arg);
    if (!info || !info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT || len > kMaxWsCommandBytes)
        return;

    handleCommand(reinterpret_cast<const char *>(data), len);
}

void drainQueueIntoTable()
{
    if (!g_queue || !g_table)
        return;

    CapturedFrame cap;
    while (g_queue->pop(cap))
    {
        if (cap.id >= kStdIdCount)
            continue;
        if (g_stats)
            g_stats->noteRx(cap);
        if (g_pretrigger)
            g_pretrigger->push(cap);
        if (g_signals)
            g_signals->push(cap);
        if (g_recorder && g_recorder->active())
            g_recorder->push(cap);
        g_table->update(cap);
        markDirty(cap.channel, cap.id);
    }
}

WsFrameRecord toWire(uint8_t channel, uint32_t id, const IdRecord &record, uint64_t nowUs)
{
    WsFrameRecord wire = {};
    wire.channel = channel;
    wire.id = static_cast<uint16_t>(id);
    wire.dlc = record.dlc;
    for (uint8_t i = 0; i < 8; ++i)
        wire.data[i] = record.data[i];
    wire.last_rx_ms = static_cast<uint32_t>(record.last_rx_ts / 1000);
    for (uint8_t i = 0; i < 8; ++i)
    {
        const uint64_t ageUs = nowUs > record.byte_change_ts[i] ? nowUs - record.byte_change_ts[i] : 0;
        const uint64_t ageMs = ageUs / 1000;
        wire.byte_age_ms[i] = ageMs > 65535 ? 65535 : static_cast<uint16_t>(ageMs);
    }
    wire.rx_count = record.rx_count;
    wire.last_delta_ms = record.last_delta_us > 65535000 ? 65535 : static_cast<uint16_t>(record.last_delta_us / 1000);
    wire.period_ms = record.period_est_us > 65535000 ? 65535 : static_cast<uint16_t>(record.period_est_us / 1000);
    wire.jitter_ms = record.jitter_us > 65535000 ? 65535 : static_cast<uint16_t>(record.jitter_us / 1000);
    wire.change_score = record.change_score;
    wire.flags = record.flags;
    return wire;
}

void pushDelta()
{
    if (!g_table || ws.count() == 0)
        return;

    static uint8_t buf[kPushBufBytes];
    static WsFrameRecord batch[kFrameDeltaBatchCapacity];
    size_t batchN = 0;
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());

    auto flush = [&]() {
        if (batchN == 0)
            return;
        const size_t n = wsBuildFrameDelta(buf, sizeof(buf), batch, static_cast<uint8_t>(batchN));
        if (n > 0)
            ws.binaryAll(buf, n);
        batchN = 0;
    };

    for (uint32_t key = 0; key < kDirtyKeys; ++key)
    {
        if ((g_dirty[key >> 3] & (1u << (key & 7))) == 0)
            continue;
        g_dirty[key >> 3] &= static_cast<uint8_t>(~(1u << (key & 7)));

        const uint8_t channel = static_cast<uint8_t>(key / kStdIdCount);
        const uint32_t id = key % kStdIdCount;
        batch[batchN++] = toWire(channel, id, g_table->record(channel, id), nowUs);
        if (batchN >= kFrameDeltaBatchCapacity)
            flush();
    }
    flush();
}

void pushBusStats()
{
    if (!g_stats || ws.count() == 0)
        return;

    const BusStatsSnapshot snapshot = g_stats->snapshot();
    WsBusStats stats = {};
    stats.fps_a = snapshot.fps[0];
    stats.fps_b = snapshot.fps[1];
    stats.load_a_x10 = snapshot.load_x10[0];
    stats.load_b_x10 = snapshot.load_x10[1];
    stats.dropped = snapshot.dropped;

    uint8_t buf[1 + sizeof(WsBusStats)];
    const size_t n = wsBuildBusStats(buf, sizeof(buf), stats);
    if (n > 0)
        ws.binaryAll(buf, n);
}
}

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels,
                           WatchedSignalWindow *signals, CommonSignalStore *common_signals,
                           Recorder *recorder, TxService *tx_service)
{
    g_queue = queue;
    g_table = table;
    g_stats = stats;
    g_pretrigger = pretrigger;
    g_snapshots = snapshots;
    g_labels = labels;
    g_signals = signals;
    g_commonSignals = common_signals;
    g_recorder = recorder;
    g_txService = tx_service;
}

void analyzerWebBegin()
{
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String out = "{";
        out += "\"can_tx_enabled\":" + String(isCanTxEnabled() ? "true" : "false");
        out += ",\"tx_a_enabled\":" + String(isAnalyzerChannelTxEnabled(0) ? "true" : "false");
        out += ",\"tx_b_enabled\":" + String(isAnalyzerChannelTxEnabled(1) ? "true" : "false");
        out += ",\"can_a_online\":" + String(isAnalyzerChannelOnline(0) ? "true" : "false");
        out += ",\"can_b_online\":" + String(isAnalyzerChannelOnline(1) ? "true" : "false");
        out += ",\"pending_dropped\":" + String(pendingDroppedCount());
        out += ",\"recording\":" + String(g_recorder && g_recorder->active() ? "true" : "false");
        out += ",\"record_count\":" + String(g_recorder ? static_cast<uint32_t>(g_recorder->count()) : 0);
        out += ",\"record_capacity\":" + String(g_recorder ? static_cast<uint32_t>(g_recorder->capacity()) : 0);
        out += ",\"record_dropped\":" + String(g_recorder ? g_recorder->dropped() : 0);
        out += "}";
        request->send(200, "application/json", out);
    });

    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", wifiStatusJson());
    });

    server.on("/api/wifi", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                  if (!analyzerWebBodyChunkIsValid(index, len, total, kMaxWifiJsonBytes))
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }
                  if (index == 0)
                      memset(g_wifiBody, 0, sizeof(g_wifiBody));
                  if (len > 0)
                      memcpy(g_wifiBody + index, data, len);
                  if (!analyzerWebBodyChunkCompletes(index, len, total))
                      return;
                  g_wifiBody[total] = '\0';

                  JsonDocument doc;
                  if (deserializeJson(doc, g_wifiBody, total))
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }
                  AnalyzerWifiCredentials credentials;
                  if (!analyzerWifiSanitizeCredentials(doc["ssid"] | "", doc["pass"] | "", credentials))
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }
                  enqueuePendingWifi(credentials);
                  request->send(200, "application/json", "{\"ok\":true,\"pending\":true}");
              });

    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        enqueuePendingPowerAction(PendingPowerAction::Restart, millis() + kPowerActionDelayMs);
        request->send(200, "application/json", "{\"ok\":true,\"pending\":true}");
    });

    server.on("/api/shutdown", HTTP_POST, [](AsyncWebServerRequest *request) {
        enqueuePendingPowerAction(PendingPowerAction::ShutdownPrepare, millis() + kPowerActionDelayMs);
        request->send(200, "application/json", "{\"ok\":true,\"pending\":true}");
    });

    server.on("/api/can-tx", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
                  const String body(reinterpret_cast<const char *>(data), len);
                  setCanTxEnabled(body.indexOf("true") >= 0);
                  request->send(200, "application/json", "{\"ok\":true}");
              });

    server.on("/api/can-tx-a", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
                  const String body(reinterpret_cast<const char *>(data), len);
                  setAnalyzerChannelTxEnabled(0, body.indexOf("true") >= 0);
                  request->send(200, "application/json", "{\"ok\":true}");
              });

    server.on("/api/can-tx-b", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
                  const String body(reinterpret_cast<const char *>(data), len);
                  setAnalyzerChannelTxEnabled(1, body.indexOf("true") >= 0);
                  request->send(200, "application/json", "{\"ok\":true}");
              });

    server.on("/api/tx/send", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                  if (index == 0 && !takeTxBodyBuffer(request, millis()))
                  {
                      request->send(analyzerWebTxBodyBusyStatus(), "application/json", "{\"ok\":false,\"error\":\"busy\"}");
                      return;
                  }

                  if (!txBodyBufferIsOwner(request) || !analyzerWebBodyChunkIsValid(index, len, total, kMaxTxJsonBytes))
                  {
                      releaseTxBodyBuffer(request);
                      request->send(400, "application/json", analyzerWebBadRequestJson());
                      return;
                  }

                  memcpy(g_txBody + index, data, len);
                  if (!analyzerWebBodyChunkCompletes(index, len, total))
                      return;
                  g_txBody[total] = '\0';

                  JsonDocument doc;
                  if (deserializeJson(doc, g_txBody, total))
                  {
                      releaseTxBodyBuffer(request);
                      request->send(400, "application/json", analyzerWebBadRequestJson());
                      return;
                  }

                  PendingCmd pending;
                  pending.type = PendingCmdType::TxSend;
                  uint32_t id = 0;
                  if (!parseTxSendRequest(doc, pending.channel, id, pending.dlc, pending.data))
                  {
                      releaseTxBodyBuffer(request);
                      request->send(400, "application/json", analyzerWebBadRequestJson());
                      return;
                  }
                  pending.id = static_cast<uint16_t>(id);

                  if (!enqueuePendingCommand(pending))
                  {
                      releaseTxBodyBuffer(request);
                      request->send(503, "application/json", "{\"ok\":false,\"error\":\"queue_full\"}");
                      return;
                  }

                  releaseTxBodyBuffer(request);
                  request->send(200, "application/json", analyzerWebTxAcceptedJson());
              });

    server.on("/api/labels", HTTP_GET, [](AsyncWebServerRequest *request) {
        LabelEntry snapshot[kMaxLabels];
        const size_t labelCount = copyLabels(snapshot, kMaxLabels);

        JsonDocument doc;
        JsonArray labels = doc.to<JsonArray>();
        for (size_t i = 0; i < labelCount; ++i)
        {
            JsonObject label = labels.createNestedObject();
            label["ch"] = snapshot[i].channel == 1 ? "B" : "A";
            label["id"] = snapshot[i].id;
            label["text"] = snapshot[i].text;
        }

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/p4/common", HTTP_GET, [](AsyncWebServerRequest *request) {
        CommonSignalSpec snapshot[kMaxCommonSignals] = {};
        const size_t signalCount = copyCommonSignals(snapshot, kMaxCommonSignals);

        JsonDocument doc;
        JsonArray signals = doc["signals"].to<JsonArray>();
        for (size_t i = 0; i < signalCount; ++i)
            jsonAddCommonSignal(signals, snapshot[i]);

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/p4/common", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                  if (!analyzerWebBodyChunkIsValid(index, len, total, kMaxCommonSignalsJsonBytes))
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }

                  if (index == 0)
                      memset(g_commonSignalBody, 0, sizeof(g_commonSignalBody));
                  if (len > 0)
                      memcpy(g_commonSignalBody + index, data, len);
                  if (!analyzerWebBodyChunkCompletes(index, len, total))
                      return;

                  JsonDocument doc;
                  if (deserializeJson(doc, g_commonSignalBody, total))
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }

                  JsonArrayConst signals = doc["signals"].as<JsonArrayConst>();
                  if (doc["signals"].isNull() || signals.isNull())
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }

                  CommonSignalSpec entries[kMaxCommonSignals] = {};
                  size_t count = 0;
                  for (JsonVariantConst item : signals)
                  {
                      if (count >= kMaxCommonSignals || !parseCommonSignalSpec(item, entries[count]))
                      {
                          request->send(400, "application/json", "{\"ok\":false}");
                          return;
                      }
                      ++count;
                  }

                  if (!replaceCommonSignals(entries, count))
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }

                  request->send(200, "application/json", "{\"ok\":true}");
              });

    server.on("/api/record/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!g_recorder)
        {
            request->send(404, "text/plain", "no recording");
            return;
        }
        // 录制中拒绝下载：collect 在 AsyncTCP 任务执行，push 在 Core1 drain；
        // 二者无锁，仅靠「下载时已 stop」保证互斥，否则会读到撕裂帧/基准漂移。
        if (g_recorder->active())
        {
            request->send(409, "text/plain", "stop recording first");
            return;
        }
        if (g_recorder->count() == 0)
        {
            request->send(404, "text/plain", "no recording");
            return;
        }
        auto cursor = std::make_shared<RecordCsvCursor>();
        const size_t total = g_recorder->count();
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "text/csv",
            [cursor, total](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
                // 录制若在下载中途重新开始，立即截断，避免与 drain push 并发读撕裂帧。
                if (!g_recorder || g_recorder->active())
                    return 0;
                return recordCsvFill(reinterpret_cast<char *>(buffer), maxLen, *g_recorder, total, *cursor);
            });
        response->addHeader("Content-Disposition", "attachment; filename=\"can-record.csv\"");
        request->send(response);
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void analyzerWebLoop()
{
    drainQueueIntoTable();
    processPendingCommands();
    processPendingWifi();
    processPendingPowerAction();
    ws.cleanupClients();

    const uint32_t now = millis();
    if (g_stats)
        g_stats->update(now, g_queue ? g_queue->dropped() : 0);

    if (now - g_lastPushMs >= kPushIntervalMs)
    {
        g_lastPushMs = now;
        pushDelta();
    }

    if (now - g_lastStatsMs >= kStatsIntervalMs)
    {
        g_lastStatsMs = now;
        pushBusStats();
    }
}
