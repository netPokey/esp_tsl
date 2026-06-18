#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_control.h"
#include "analyzer/analyzer_wifi.h"
#include "analyzer/ws_protocol.h"
#include "can_helpers.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <cstring>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

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
constexpr size_t kMaxWsBatchRecords = 255;
constexpr size_t wsBatchCapacity(size_t byteCapacity)
{
    return byteCapacity < kMaxWsBatchRecords ? byteCapacity : kMaxWsBatchRecords;
}
constexpr size_t kFrameDeltaBatchCapacity = wsBatchCapacity((kPushBufBytes - 2) / sizeof(WsFrameRecord));
static_assert(kFrameDeltaBatchCapacity >= 1, "frame delta batch capacity must be non-zero");
uint32_t g_lastPushMs = 0;
uint32_t g_lastStatsMs = 0;
constexpr uint32_t kPushIntervalMs = 66;
constexpr uint32_t kStatsIntervalMs = 1000;

constexpr size_t kMaxWifiJsonBytes = 256;
constexpr uint32_t kPowerActionDelayMs = 200;
constexpr uint32_t kShutdownSleepDelayMs = 100;
char g_wifiBody[kMaxWifiJsonBytes + 1] = {};
portMUX_TYPE g_wifiMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_powerMux = portMUX_INITIALIZER_UNLOCKED;
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

String wifiStatusJson()
{
    const AnalyzerWifiStatus st = analyzerWifiStatus();
    JsonDocument doc;
    doc["ok"] = true;
    doc["mode"] = st.sta ? "sta" : (st.ap ? "ap" : "off");
    doc["ip"] = st.ip;
    doc["ssid"] = st.ssid;
    doc["pass"] = st.pass;
    String out;
    serializeJson(doc, out);
    return out;
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

void markDirty(uint8_t channel, uint32_t id)
{
    const uint32_t key = static_cast<uint32_t>(channel) * kStdIdCount + id;
    if (key < kDirtyKeys)
        g_dirty[key >> 3] |= static_cast<uint8_t>(1u << (key & 7));
}

void drainQueueIntoTable()
{
    if (!g_queue)
        return;

    CapturedFrame cap;
    while (g_queue->pop(cap))
    {
        if (cap.id >= kStdIdCount)
            continue;
        if (g_stats)
            g_stats->noteRx(cap);
        if (g_table)
        {
            g_table->update(cap);
            markDirty(cap.channel, cap.id);
        }
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
}  // namespace

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats)
{
    g_queue = queue;
    g_table = table;
    g_stats = stats;
}

void analyzerWebBegin()
{
    server.addHandler(&ws);

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String out = "{";
        out += "\"can_a_online\":" + String(isAnalyzerChannelOnline(0) ? "true" : "false");
        out += ",\"can_b_online\":" + String(isAnalyzerChannelOnline(1) ? "true" : "false");
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

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void analyzerWebLoop()
{
    drainQueueIntoTable();
    processPendingWifi();
    processPendingPowerAction();
    const uint32_t now = millis();
    ws.cleanupClients();

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
