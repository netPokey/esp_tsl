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

// ============================================================================
// 最小 Web/WS 层：
// - HTTP：静态文件、WiFi 配置、设备重启/深睡、通道在线状态；
// - WS：只向浏览器推送帧增量和总线统计，不接收任何控制命令。
// 本文件运行在 Arduino 主循环任务中；高优先级 rx_task 负责采集并写 FrameQueue。
// AsyncWebServer 的回调可能来自网络任务，因此 WiFi/电源动作只入 pending 状态，
// 真正执行放到 analyzerWebLoop()，避免在回调里做重启/深睡等不可重入动作。
// ============================================================================

namespace
{
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// 这些对象由 can_analyzer.cpp 创建并通过 analyzerWebSetContext() 注入；Web 层不拥有其生命周期。
FrameQueue *g_queue = nullptr;
IdTable *g_table = nullptr;
BusStatsTracker *g_stats = nullptr;

// 每个 (channel,id) 对应一个 dirty bit。消费侧更新 IdTable 后置位，pushDelta() 发送后清位。
constexpr uint32_t kDirtyKeys = static_cast<uint32_t>(kChannelCount) * kStdIdCount;
uint8_t g_dirty[kDirtyKeys / 8] = {};

// 1400 字节以内可避开常见 MTU 分片；单包最多 255 条记录（协议 count 为 uint8_t）。
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
// 约 15Hz 刷新实时表，既足够顺滑，又避免浏览器 DOM 过载；统计每秒刷新一次。
constexpr uint32_t kPushIntervalMs = 66;
constexpr uint32_t kStatsIntervalMs = 1000;
// 单轮 pushDelta 最多发送的 WS 帧数。即便大量 ID 同时 dirty（如首个客户端刚连上，
// 此前积累的 dirty 位会一次性全部待发），也不会一口气塞爆 AsyncWebSocket 的发送队列
// （ESP32Async 默认 WS_MAX_QUEUED_MESSAGES=32）；超出的 dirty 位保留，下一轮继续发。
constexpr size_t kMaxFlushesPerPush = 16;

// WiFi POST 只接收 ssid/pass，256 字节足够；超限直接 400，防止 Async 回调里堆分配。
constexpr size_t kMaxWifiJsonBytes = 256;
constexpr uint32_t kPowerActionDelayMs = 200;
constexpr uint32_t kShutdownSleepDelayMs = 100;
char g_wifiBody[kMaxWifiJsonBytes + 1] = {};
portMUX_TYPE g_wifiMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_powerMux = portMUX_INITIALIZER_UNLOCKED;
AnalyzerWifiCredentials g_pendingWifi;
volatile bool g_pendingWifiValid = false;

// 电源动作分两阶段延迟执行：HTTP 回调只设置状态，主循环到点后执行。
// ShutdownPrepare 先关闭 TX，再延迟进入深睡，让 JSON 响应有机会发回浏览器。
enum class PendingPowerAction : uint8_t
{
    None,
    Restart,
    ShutdownPrepare,
    ShutdownSleep
};

volatile PendingPowerAction g_pendingPowerAction = PendingPowerAction::None;
volatile uint32_t g_pendingPowerExecuteAfterMs = 0;

// 设备网络状态 JSON。字段名与 data/analyzer/app.js 保持一一对应。
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

// Async HTTP 回调里只保存待切换的 WiFi 凭据；实际保存/重连在主循环执行。
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

// 电源动作的共享状态由 HTTP 回调写、主循环读，必须用临界区保护。
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

// 主循环中的电源状态机。所有真正会断网/重启/深睡的操作都集中在这里执行。
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
        setCanTxEnabled(false);  // 深睡前再次确保全局 TX 关闭，保持只监听设备的安全边界。
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

// 标记某个 ID 需要推送。dirty 位只在消费侧使用，不需要跨任务同步。
void markDirty(uint8_t channel, uint32_t id)
{
    const uint32_t key = static_cast<uint32_t>(channel) * kStdIdCount + id;
    if (key < kDirtyKeys)
        g_dirty[key >> 3] |= static_cast<uint8_t>(1u << (key & 7));
}

// 把 rx_task 入队的原始帧全部消费到 IdTable/BusStats。
// 这是 FrameQueue 的唯一消费者；队列被 drain 后，dirty bit 记录哪些行需要推给浏览器。
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

// 把内部 IdRecord 转成紧凑的 WS 二进制记录。
// 内部以微秒保存时间，线上协议用毫秒/16-bit 表示，超出范围时饱和到 65535。
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

// 扫描 dirty 位并批量推送帧增量。
// 只推有变化/新到达的 ID，浏览器端按 ID 覆盖更新，避免每次发送完整 4096 行表。
void pushDelta()
{
    if (!g_table || ws.count() == 0)
        return;
    // 背压保护：任一客户端的发送队列已满时整轮跳过，且不触碰 dirty 位。
    // delta 是"最新值覆盖"语义，丢掉中间一轮无害；dirty 位保留到客户端追上后再发，
    // 避免在拥塞客户端上无限堆积 WS 消息、耗尽堆内存。
    if (!ws.availableForWriteAll())
        return;

    static uint8_t buf[kPushBufBytes];
    static WsFrameRecord batch[kFrameDeltaBatchCapacity];
    size_t batchN = 0;
    size_t flushes = 0;
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());

    auto flush = [&]() {
        if (batchN == 0)
            return;
        // builder 会再次按 cap 裁剪；这里的 batchN 已按 kFrameDeltaBatchCapacity 控制。
        const size_t n = wsBuildFrameDelta(buf, sizeof(buf), batch, static_cast<uint8_t>(batchN));
        if (n > 0)
        {
            ws.binaryAll(buf, n);
            ++flushes;
        }
        batchN = 0;
    };

    for (uint32_t key = 0; key < kDirtyKeys; ++key)
    {
        if ((g_dirty[key >> 3] & (1u << (key & 7))) == 0)
            continue;
        // 到达单轮发送预算就停下：当前 key 及之后的 dirty 位保持置位（尚未清位），下一轮继续。
        if (flushes >= kMaxFlushesPerPush)
            break;
        // 先清位再取快照；若之后同一轮 drain 又更新同一 ID，会重新置位并在下一轮推送。
        g_dirty[key >> 3] &= static_cast<uint8_t>(~(1u << (key & 7)));

        const uint8_t channel = static_cast<uint8_t>(key / kStdIdCount);
        const uint32_t id = key % kStdIdCount;
        batch[batchN++] = toWire(channel, id, g_table->record(channel, id), nowUs);
        if (batchN >= kFrameDeltaBatchCapacity)
            flush();
    }
    flush();
}

// 每秒推送一次总线统计。rx_err_a/b 现承载驱动层的硬件级丢帧（CAN_A 溢出事件、CAN_B rx_missed），
// 让前端能看见 FrameQueue 之外、被芯片/驱动队列丢掉的帧；bus_off 仍保留为 0。
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
    stats.rx_err_a = snapshot.hw_drops[0];
    stats.rx_err_b = snapshot.hw_drops[1];
    stats.dropped = snapshot.dropped;

    uint8_t buf[1 + sizeof(WsBusStats)];
    const size_t n = wsBuildBusStats(buf, sizeof(buf), stats);
    if (n > 0)
        ws.binaryAll(buf, n);
}
}  // namespace

// 注入由组装点创建的核心对象；Web 层仅保存裸指针，不负责释放。
void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats)
{
    g_queue = queue;
    g_table = table;
    g_stats = stats;
}

// 注册所有 HTTP/WS 路由。当前没有任何入站 WS 命令，WebSocket 只做服务器推送。
void analyzerWebBegin()
{
    server.addHandler(&ws);

    // 轻量状态端点：前端仅用它显示 CAN_A/CAN_B 是否初始化成功。
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

    // AsyncWebServer 以分片方式交付 POST body；这里手动聚合到固定缓冲，避免动态分配。
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
                  // 返回 pending：真正的保存/重连在主循环执行，响应先完成，避免连接切换打断 HTTP。
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

// Arduino 主循环每轮调用。顺序很重要：先 drain 队列再推送，这样浏览器看到的是最新状态。
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