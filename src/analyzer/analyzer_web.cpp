#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_control.h"
#include "analyzer/ws_protocol.h"
#include "can_helpers.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_timer.h>

namespace
{
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

FrameQueue *g_queue = nullptr;
IdTable *g_table = nullptr;
BusStatsTracker *g_stats = nullptr;

constexpr uint32_t kDirtyKeys = static_cast<uint32_t>(kChannelCount) * kStdIdCount;
uint8_t g_dirty[kDirtyKeys / 8] = {};

constexpr size_t kPushBufBytes = 1400;
uint32_t g_lastPushMs = 0;
uint32_t g_lastStatsMs = 0;
constexpr uint32_t kPushIntervalMs = 66;
constexpr uint32_t kStatsIntervalMs = 1000;

void markDirty(uint8_t channel, uint32_t id)
{
    const uint32_t key = static_cast<uint32_t>(channel) * kStdIdCount + id;
    if (key < kDirtyKeys)
        g_dirty[key >> 3] |= static_cast<uint8_t>(1u << (key & 7));
}

void handleCommand(const char *text, size_t len)
{
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, text, len))
        return;

    const char *cmd = doc["cmd"] | "";
    if (strcmp(cmd, "tx_master") == 0)
    {
        setCanTxEnabled(doc["on"] | false);
        return;
    }
    if (strcmp(cmd, "tx_enable") == 0)
    {
        const char *ch = doc["ch"] | "A";
        setAnalyzerChannelTxEnabled((ch[0] == 'B' || ch[0] == 'b') ? 1 : 0, doc["on"] | false);
    }
}

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType type, void *, uint8_t *data, size_t len)
{
    if (type == WS_EVT_DATA)
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

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats)
{
    g_queue = queue;
    g_table = table;
    g_stats = stats;
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

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void analyzerWebLoop()
{
    drainQueueIntoTable();
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
