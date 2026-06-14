#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_control.h"
#include "analyzer/ws_protocol.h"
#include "can_helpers.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

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

constexpr uint32_t kDirtyKeys = static_cast<uint32_t>(kChannelCount) * kStdIdCount;
uint8_t g_dirty[kDirtyKeys / 8] = {};

constexpr size_t kPushBufBytes = 1400;
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
    LabelDelete
};

struct PendingCmd
{
    PendingCmdType type = PendingCmdType::Diff;
    uint8_t channel = 0;
    bool on = false;
    SnapshotSlot slot = SnapshotSlot::A;
    uint16_t id = 0;
    char text[kLabelTextLen] = {};
};

constexpr size_t kPendingCmdCap = 8;
constexpr size_t kMaxWsCommandBytes = 256;
PendingCmd g_pending[kPendingCmdCap];
volatile size_t g_pendingHead = 0;
volatile size_t g_pendingTail = 0;
portMUX_TYPE g_pendingMux = portMUX_INITIALIZER_UNLOCKED;

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

    constexpr size_t batchSize = 64;
    SnapshotDiffRecord diffs[batchSize];
    WsDiffRecord wire[batchSize];
    uint8_t buf[kPushBufBytes];
    size_t skip = 0;

    while (true)
    {
        const size_t n = g_snapshots->diff(diffs, batchSize, skip);
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
        if (n < batchSize)
            break;
        skip += n;
    }
}

void sendPretrigger()
{
    if (!g_pretrigger || ws.count() == 0)
        return;

    constexpr size_t batchSize = 64;
    WsPretriggerRecord recs[batchSize];
    uint8_t buf[kPushBufBytes];
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    size_t skip = 0;

    while (true)
    {
        const size_t n = g_pretrigger->summarize(nowUs, 5000000UL, recs, batchSize, skip);
        const size_t bytes = wsBuildPretrigger(buf, sizeof(buf), recs, static_cast<uint8_t>(n));
        if ((n > 0 || skip == 0) && bytes > 0)
            ws.binaryAll(buf, bytes);
        if (n < batchSize)
            break;
        skip += n;
    }
}

void sendBaseline()
{
    if (!g_table || ws.count() == 0)
        return;

    WsBaselineRecord recs[64];
    uint8_t buf[kPushBufBytes];
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
            if (n >= 64)
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
    portEXIT_CRITICAL(&g_pendingMux);
    return queued;
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
        if (g_labels && g_labels->upsert(cmd.channel, cmd.id, cmd.text))
            sendLabelUpdateNotice();
        break;
    case PendingCmdType::LabelDelete:
        if (g_labels && g_labels->remove(cmd.channel, cmd.id))
            sendLabelUpdateNotice();
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
    constexpr size_t batchCapacity = 32;
    WsFrameRecord batch[batchCapacity];
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
        if (batchN >= batchCapacity)
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
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels)
{
    g_queue = queue;
    g_table = table;
    g_stats = stats;
    g_pretrigger = pretrigger;
    g_snapshots = snapshots;
    g_labels = labels;
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
        out += "}";
        request->send(200, "application/json", out);
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

    server.on("/api/labels", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray labels = doc.to<JsonArray>();
        if (g_labels)
        {
            const LabelEntry *entries = g_labels->entries();
            for (size_t i = 0; i < g_labels->count(); ++i)
            {
                JsonObject label = labels.createNestedObject();
                label["ch"] = entries[i].channel == 1 ? "B" : "A";
                label["id"] = entries[i].id;
                label["text"] = entries[i].text;
            }
        }

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void analyzerWebLoop()
{
    drainQueueIntoTable();
    processPendingCommands();
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
