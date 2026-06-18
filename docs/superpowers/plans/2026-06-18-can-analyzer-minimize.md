# CAN 分析仪最小化 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 CAN 分析仪精简为「只监听 + WebSocket 转发 + Web 浏览 A/B 实时表」，保留 WiFi/AP 与电源控制，删除 TX 发送、录制、回放、快照差异、触发前回看、标签、信号工作台、常用信号、触发录制。

**Architecture:** 接收链路不变（Core0 `rx_task` 采集 → `FrameQueue` → Core1 `analyzerWebLoop` 消费 → `IdTable`/`BusStatsTracker` → WS 推送 frame delta + bus stats）。删除所有写/分析模块及其 PSRAM 缓冲、HTTP/WS 命令处理。`analyzer_web` 只保留静态文件、`/ws`、WiFi 端点、电源端点、精简版 `/api/status`（仅在线状态）。前端 `index.html`/`app.js` 整体重写为最小可用版本，保留高性能差分渲染核心。

**Tech Stack:** ESP32 (Arduino + PlatformIO)、MCP2515(SPI)/TWAI 双 CAN、ESPAsyncWebServer + AsyncWebSocket、LittleFS、PSRAM、Unity(native 单测)。

**实测约束（与 spec 的实现细节修正）：**
- 端点保持现有路径不改名：`GET/POST /api/wifi`、`POST /api/restart`、`POST /api/shutdown`、`GET /api/status`（精简）。spec 中 `/api/wifi/status`、`/api/power/*` 为示意，以现有路径为准。
- `kStdIdCount=2048`、`kChannelCount=2` 定义在 `src/analyzer/id_table.h`，保留。
- `CapturedFrame` 定义在 `src/analyzer/analyzer_types.h`，保留。
- TX 开关函数 `isCanTxEnabled/setCanTxEnabled` 在 `include/can_helpers.h`，**保留不动**（被其他 env 共用）。仅删除 analyzer 内 TX 通道开关的使用。
- `analyzer_control.h` 同时含 `markAnalyzerChannelOnline/isAnalyzerChannelOnline`（电源/状态需要）与 TX 通道开关（不再需要）。保留在线状态相关，删除 TX 通道开关相关。
- 验证环境：`pio test -e native`（单测）、`pio run -e analyzer`（固件编译）。

---

## 文件结构

**后端保留并修改：**
- `src/can_analyzer.cpp` — 组装点，删除所有被删模块的分配/初始化，精简 `analyzerWebSetContext` 调用与 `loop()`。
- `src/analyzer/ws_protocol.h` / `.cpp` — 只保留 `WS_MSG_FRAME_DELTA`/`WS_MSG_BUS_STATS` 及其 builder。
- `src/analyzer/analyzer_web.h` / `.cpp` — 精简 context 签名、删除 TX/录制/回放/快照/标签/信号/触发的端点、命令、helper。
- `platformio.ini` — 精简 `[env:native]` 的 `build_src_filter`。

**后端保留不动：**
- `src/analyzer/frame_queue.*`、`id_table.*`、`bus_stats.*`、`rx_task.*`、`analyzer_wifi.*`、`analyzer_types.h`
- `src/analyzer/analyzer_control.h`（删 TX 通道开关部分，保留在线状态）
- `include/can_helpers.h`、`drivers/`、`pin_config.h`

**后端删除（源文件）：**
`tx_service.*`、`tx_mode_sync.*`、`replay_service.*`、`recorder.*`、`record_format.*`、`record_asc_format.*`、`record_trigger.*`、`pretrigger_buffer.*`、`snapshot_store.*`、`label_store.*`、`signal_window.*`、`signal_codec.*`、`signal_hints.*`、`common_signal_store.*`

**测试删除：**
`test_tx_service`、`test_tx_mode_sync`、`test_replay_service`、`test_recorder`、`test_record_format`、`test_record_stream`、`test_record_asc_format`、`test_record_trigger`、`test_pretrigger_buffer`、`test_snapshot_store`、`test_label_store`、`test_signal_window`、`test_signal_codec`、`test_signal_hints`、`test_common_signal_store`

**测试保留：**
`test_frame_queue`、`test_bus_stats`、`test_id_table`、`test_analyzer_wifi`、`test_ws_protocol`（删除 TX/replay/signal/diff/pretrigger/baseline/record_trigger 相关用例）。`test_can_analyzer_sim`（JS 模拟器测试，与固件无关，保留不动）。

**前端整体重写：**
- `data/analyzer/index.html`、`data/analyzer/app.js`、`data/analyzer/style.css`

## Tasks

### Task 1: 精简 native 构建过滤器并删除废弃测试目录

只让 native 编译/运行保留链路的模块与测试。先做这步，后续删除源文件时 native 不会再尝试编译它们。

**Files:**
- Modify: `platformio.ini`（`[env:native]` 的 `build_src_filter`）
- Delete: `test/test_tx_service`、`test/test_tx_mode_sync`、`test/test_replay_service`、`test/test_recorder`、`test/test_record_format`、`test/test_record_stream`、`test/test_record_asc_format`、`test/test_record_trigger`、`test/test_pretrigger_buffer`、`test/test_snapshot_store`、`test/test_label_store`、`test/test_signal_window`、`test/test_signal_codec`、`test/test_signal_hints`、`test/test_common_signal_store`

- [ ] **Step 1: 把 `[env:native]` 的 `build_src_filter` 改为只保留链路模块**

将 `platformio.ini` 中 `[env:native]` 的整段 `build_src_filter`（当前从 `-<*>` 到 `+<analyzer/replay_service.cpp>`）替换为：

```ini
build_src_filter =
    -<*>
    +<analyzer/frame_queue.cpp>
    +<analyzer/id_table.cpp>
    +<analyzer/ws_protocol.cpp>
    +<analyzer/bus_stats.cpp>
    +<analyzer/analyzer_wifi.cpp>
```

- [ ] **Step 2: 删除废弃测试目录**

```bash
git rm -r test/test_tx_service test/test_tx_mode_sync test/test_replay_service \
  test/test_recorder test/test_record_format test/test_record_stream \
  test/test_record_asc_format test/test_record_trigger test/test_pretrigger_buffer \
  test/test_snapshot_store test/test_label_store test/test_signal_window \
  test/test_signal_codec test/test_signal_hints test/test_common_signal_store
```

- [ ] **Step 3: 运行 native 测试确认仍可编译通过**

Run: `pio test -e native`
Expected: PASS。此时 `test_ws_protocol` 仍含将删用例（仍引用 `ws_protocol.cpp` 里现存的 builder 与 `analyzer_web.h` 的 helper），应全部通过。剩余测试目录 `test_frame_queue`/`test_bus_stats`/`test_id_table`/`test_analyzer_wifi`/`test_ws_protocol`/`test_can_analyzer_sim`。

- [ ] **Step 4: 提交**

```bash
git add platformio.ini
git commit -m "build(analyzer): trim native filter and remove obsolete tests"
```

---

### Task 2: 精简 ws_protocol 与 test_ws_protocol、analyzer_web.h（三者耦合，同任务）

`ws_protocol.h` 的 diff/signal 结构体与 builder、`analyzer_web.h` 里引用它们的 helper/ForTest 函数、`test_ws_protocol.cpp` 里对应的用例互相引用，必须一起改，否则 native 编译断裂。

**Files:**
- Modify: `src/analyzer/ws_protocol.h`
- Modify: `src/analyzer/ws_protocol.cpp`
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp`

- [ ] **Step 1: 用最小内容覆盖 `src/analyzer/ws_protocol.h`**

完整替换为：

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

enum WsMsgType : uint8_t
{
    WS_MSG_FRAME_DELTA = 0x01,
    WS_MSG_BUS_STATS = 0x02,
};

#pragma pack(push, 1)
struct WsFrameRecord
{
    uint8_t channel;
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t last_rx_ms;
    uint16_t byte_age_ms[8];
    uint32_t rx_count;
    uint16_t last_delta_ms;
    uint16_t period_ms;
    uint16_t jitter_ms;
    uint16_t change_score;
    uint8_t flags;
};

struct WsBusStats
{
    uint16_t fps_a;
    uint16_t fps_b;
    uint16_t load_a_x10;
    uint16_t load_b_x10;
    uint32_t rx_err_a;
    uint32_t rx_err_b;
    uint8_t bus_off_a;
    uint8_t bus_off_b;
    uint32_t dropped;
};
#pragma pack(pop)

size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count);
size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats);
```

- [ ] **Step 2: 用最小内容覆盖 `src/analyzer/ws_protocol.cpp`**

完整替换为：

```cpp
#include "analyzer/ws_protocol.h"
#include <cstring>

size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count)
{
    if (cap < 2)
        return 0;

    const size_t recSize = sizeof(WsFrameRecord);
    size_t maxByCap = (cap - 2) / recSize;
    if (maxByCap > count)
        maxByCap = count;

    buf[0] = WS_MSG_FRAME_DELTA;
    buf[1] = static_cast<uint8_t>(maxByCap);
    memcpy(buf + 2, recs, maxByCap * recSize);
    return 2 + maxByCap * recSize;
}

size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats)
{
    if (cap < 1 + sizeof(WsBusStats))
        return 0;

    buf[0] = WS_MSG_BUS_STATS;
    memcpy(buf + 1, &stats, sizeof(WsBusStats));
    return 1 + sizeof(WsBusStats);
}
```

- [ ] **Step 3: 用最小内容覆盖 `src/analyzer/analyzer_web.h`**

完整替换为（只保留 body-chunk 校验 helper 及其 ForTest 变体，以及精简后的三个函数声明；删除全部 TX/replay/signal/snapshot/record_trigger helper）：

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"

inline bool analyzerWebBodyChunkIsValid(size_t index, size_t len, size_t total, size_t max_total)
{
    return total <= max_total && index <= total && len <= (total - index);
}

inline bool analyzerWebBodyChunkCompletes(size_t index, size_t len, size_t total)
{
    return index <= total && len <= (total - index) && index + len == total;
}

#if defined(NATIVE_BUILD)
inline bool analyzerWebBodyChunkIsValidForTest(size_t index, size_t len, size_t total, size_t max_total)
{
    return analyzerWebBodyChunkIsValid(index, len, total, max_total);
}

inline bool analyzerWebBodyChunkCompletesForTest(size_t index, size_t len, size_t total)
{
    return analyzerWebBodyChunkCompletes(index, len, total);
}
#endif

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats);
void analyzerWebBegin();
void analyzerWebLoop();
```

- [ ] **Step 4: 精简 `test/test_ws_protocol/test_ws_protocol.cpp`，只保留仍存在符号的用例**

只保留这 4 个测试函数及其 `RUN_TEST`，删除其余所有测试函数与对应 `RUN_TEST`：
- `test_frame_delta_header_and_one_record`
- `test_frame_delta_respects_buffer_cap`
- `test_bus_stats_layout`
- `test_analyzer_web_body_chunk_helpers_validate_bounds_and_completion`

删除这些用例引用的、已不存在的符号相关测试（diff/pretrigger/baseline/signal samples/hints/tx/replay/record_trigger/channel/slot 全部）。`#include` 保留 `"analyzer/ws_protocol.h"` 与 `"analyzer/analyzer_web.h"`。`main()` 框架与保留的 4 个 `RUN_TEST` 保持。

参考保留后的 `main()` 形态：

```cpp
int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_delta_header_and_one_record);
    RUN_TEST(test_frame_delta_respects_buffer_cap);
    RUN_TEST(test_bus_stats_layout);
    RUN_TEST(test_analyzer_web_body_chunk_helpers_validate_bounds_and_completion);
    return UNITY_END();
}
```

> 注：`test_bus_stats_layout` 验证 `WsBusStats` 二进制布局，结构体未变所以保留有效。若该用例内部引用了已删结构体（不应该，仅用 `WsBusStats`），按编译错误提示删掉相应断言。

- [ ] **Step 5: 运行 native 测试**

Run: `pio test -e native`
Expected: PASS（含精简后的 `test_ws_protocol`）。若报未定义符号，说明 test 仍引用已删 builder/helper，回到 Step 4 删干净。

- [ ] **Step 6: 提交**

```bash
git add src/analyzer/ws_protocol.h src/analyzer/ws_protocol.cpp src/analyzer/analyzer_web.h test/test_ws_protocol/test_ws_protocol.cpp
git commit -m "refactor(analyzer): reduce ws protocol and web header to frame+stats"
```

---

### Task 3: 精简 analyzer_web.cpp

删除所有 TX/录制/回放/快照/标签/信号/触发的 pending 命令、HTTP 端点、helper，只保留：drain→table、frame delta 推送、bus stats 推送、`/ws`、WiFi 端点、电源端点、精简 `/api/status`、静态文件。这是纯固件文件（不参与 native），用 `pio run -e analyzer` 验证。

**Files:**
- Modify: `src/analyzer/analyzer_web.cpp`（整体替换为下方内容）

- [ ] **Step 1: 用最小内容覆盖 `src/analyzer/analyzer_web.cpp`（上半：includes、状态、电源/WiFi 机制）**

完整替换文件。先写出文件的前半部分：

```cpp
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
```

- [ ] **Step 2: 续写文件中段（drain、toWire、推送）**

接着上一段，继续写入同一文件的中段：

```cpp
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
```

- [ ] **Step 3: 续写文件下段（context、begin 端点注册、loop）**

接着上一段，写入文件结尾部分：

```cpp
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
```

> 注：不再注册 `onWsEvent`（无入站命令），所以 `ws` 不需要 `onEvent` 回调。客户端只接收二进制推送。

- [ ] **Step 4: 编译固件（此步会因 can_analyzer.cpp 仍引用旧符号而失败，属预期）**

Run: `pio run -e analyzer`
Expected: 可能 FAIL，因为 `src/can_analyzer.cpp` 仍 include 已删头并调用旧版 `analyzerWebSetContext`。这是预期——Task 5 修好组装点后再整体编译通过。本步只确认 `analyzer_web.cpp` 自身语法无误（错误应来自 `can_analyzer.cpp` 或缺失头，而非 `analyzer_web.cpp` 内部）。

- [ ] **Step 5: 提交**

```bash
git add src/analyzer/analyzer_web.cpp
git commit -m "refactor(analyzer): strip web layer to listen+ws+wifi+power"
```

---

### Task 4: 精简组装点 can_analyzer.cpp

删除所有被删模块的全局对象、PSRAM 分配、初始化，精简 `analyzerWebSetContext` 调用与 `loop()`（移除 `syncTxMode`）。

**Files:**
- Modify: `src/can_analyzer.cpp`（整体替换）

- [ ] **Step 1: 用最小内容覆盖 `src/can_analyzer.cpp`**

完整替换为：

```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <memory>

#include "analyzer/analyzer_control.h"
#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_wifi.h"
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/rx_task.h"
#include "can_helpers.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"
#include "pin_config.h"

namespace
{
constexpr uint16_t kQueueCapacity = 1024;
CapturedFrame g_queueStorage[kQueueCapacity];
FrameQueue g_queue;

IdTable g_table;
BusStatsTracker g_stats;

std::unique_ptr<MCP2515Driver> g_canA;
std::unique_ptr<TWAIDriver> g_canB;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    setCanTxEnabled(false);

    if (!LittleFS.begin(true))
        Serial.println("LittleFS mount failed");

    void *tableMem = heap_caps_malloc(IdTable::kStorageBytes, MALLOC_CAP_SPIRAM);
    if (!tableMem)
    {
        Serial.println("PSRAM allocation failed for ID table");
        while (true)
            delay(1000);
    }
    g_table.init(static_cast<IdRecord *>(tableMem));

    g_queue.init(g_queueStorage, kQueueCapacity);
    g_stats.begin(millis());

    g_canA.reset(new MCP2515Driver(MCP2515_CS, MCP2515_RST,
                                   MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI,
                                   &SPI, 10000000));
    g_canB.reset(new TWAIDriver(static_cast<gpio_num_t>(CAN_TX),
                                static_cast<gpio_num_t>(CAN_RX)));

    const bool canAOk = g_canA->init();
    const bool canBOk = g_canB->init();
    markAnalyzerChannelOnline(0, canAOk);
    markAnalyzerChannelOnline(1, canBOk);

    if (!canAOk)
        Serial.println("CAN_A init failed");
    if (!canBOk)
        Serial.println("CAN_B init failed");

    if (canAOk)
        g_canA->setFilters(nullptr, 0);
    if (canBOk)
        g_canB->setFilters(nullptr, 0);

    rxTaskStart(canAOk ? g_canA.get() : nullptr,
                canBOk ? g_canB.get() : nullptr,
                &g_queue);

    const String ip = analyzerWifiBegin();
    analyzerWebSetContext(&g_queue, &g_table, &g_stats);
    analyzerWebBegin();

    Serial.print("CAN analyzer ready (listen-only): http://");
    Serial.println(ip);
}

void loop()
{
    analyzerWebLoop();
    delay(1);
}
```

- [ ] **Step 2: 提交（编译验证延后到 Task 6，因仍有未删源文件参与 analyzer env 构建）**

```bash
git add src/can_analyzer.cpp
git commit -m "refactor(analyzer): minimize assembly point to listen+web link"
```

---

### Task 5: 删除后端源文件并精简 analyzer_control.h

`[env:analyzer]` 的 `build_src_filter` 用 `+<analyzer/>` 通配整个目录，被删模块仍会被编译。删除它们的源文件，并清理 `analyzer_control.h` 中不再需要的 TX 通道开关（保留在线状态相关）。

**Files:**
- Delete: `src/analyzer/tx_service.*`、`tx_mode_sync.*`、`replay_service.*`、`recorder.*`、`record_format.*`、`record_asc_format.*`、`record_trigger.*`、`pretrigger_buffer.*`、`snapshot_store.*`、`label_store.*`、`signal_window.*`、`signal_codec.*`、`signal_hints.*`、`common_signal_store.*`
- Modify: `src/analyzer/analyzer_control.h`

- [ ] **Step 1: 删除被删模块的源文件（.h 与 .cpp）**

```bash
git rm src/analyzer/tx_service.h src/analyzer/tx_service.cpp \
  src/analyzer/tx_mode_sync.h src/analyzer/tx_mode_sync.cpp \
  src/analyzer/replay_service.h src/analyzer/replay_service.cpp \
  src/analyzer/recorder.h src/analyzer/recorder.cpp \
  src/analyzer/record_format.h src/analyzer/record_format.cpp \
  src/analyzer/record_asc_format.h src/analyzer/record_asc_format.cpp \
  src/analyzer/record_trigger.h src/analyzer/record_trigger.cpp \
  src/analyzer/pretrigger_buffer.h src/analyzer/pretrigger_buffer.cpp \
  src/analyzer/snapshot_store.h src/analyzer/snapshot_store.cpp \
  src/analyzer/label_store.h src/analyzer/label_store.cpp \
  src/analyzer/signal_window.h src/analyzer/signal_window.cpp \
  src/analyzer/signal_codec.h src/analyzer/signal_codec.cpp \
  src/analyzer/signal_hints.h src/analyzer/signal_hints.cpp \
  src/analyzer/common_signal_store.h src/analyzer/common_signal_store.cpp
```

- [ ] **Step 2: 精简 `src/analyzer/analyzer_control.h`，删除 TX 通道开关，保留在线状态**

完整替换为：

```cpp
#pragma once
#include <cstdint>
#include "can_helpers.h"

inline Shared<bool> &analyzerChannelOnlineStorage(uint8_t channel)
{
    static Shared<bool> onlineA(false);
    static Shared<bool> onlineB(false);
    return channel == 0 ? onlineA : onlineB;
}

inline void markAnalyzerChannelOnline(uint8_t channel, bool online)
{
    storeSharedBool(analyzerChannelOnlineStorage(channel), online);
}

inline bool isAnalyzerChannelOnline(uint8_t channel)
{
    return loadSharedBool(analyzerChannelOnlineStorage(channel));
}
```

- [ ] **Step 3: 确认无残留引用**

Run: `grep -rn "tx_service\|tx_mode_sync\|replay_service\|recorder\|record_format\|record_asc\|record_trigger\|pretrigger_buffer\|snapshot_store\|label_store\|signal_window\|signal_codec\|signal_hints\|common_signal_store\|setAnalyzerChannelTxEnabled\|isAnalyzerChannelTxEnabled\|shouldAllowAnalyzerChannelTx" src/`
Expected: 无输出（空）。若有命中，说明还有源文件引用被删模块，逐一处理。

- [ ] **Step 4: 提交**

```bash
git add -A src/analyzer
git commit -m "refactor(analyzer): delete tx/record/replay/snapshot/signal modules"
```

---

### Task 6: 验证固件编译

此时后端所有源文件应自洽。

- [ ] **Step 1: 编译 analyzer 固件**

Run: `pio run -e analyzer`
Expected: SUCCESS（编译链接通过）。若报缺符号/缺头，按提示定位是否漏删 include 或漏改引用，修正后重编。

- [ ] **Step 2: 运行 native 测试确认未回归**

Run: `pio test -e native`
Expected: PASS（`test_frame_queue`/`test_bus_stats`/`test_id_table`/`test_analyzer_wifi`/`test_ws_protocol`/`test_can_analyzer_sim`）。

- [ ] **Step 3: 无改动则跳过提交（验证性任务）**

无源码变更则不提交。若修了编译错误，按改动内容提交。

---

### Task 7: 重写前端 index.html

整体重写为最小可用版本：连接状态、bus-health、bus-stats、进制/静态抑制/冻结/排序、通道/ID/范围/搜索筛选、A/B 实时表、网络与电源面板。删除 TX、录制、回放、触发、快照差异、回看、信号工作台、intro-panel。

**Files:**
- Modify: `data/analyzer/index.html`（整体替换）

- [ ] **Step 1: 用最小内容覆盖 `data/analyzer/index.html`**

完整替换为：

```html
<!DOCTYPE html>
<html lang="zh">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>CAN 分析仪</title>
  <link rel="stylesheet" href="style.css"/>
</head>
<body>
  <section class="network-panel">
    <h2>网络与电源</h2>
    <div class="panel-summary">配置 WiFi 后设备会优先连接路由器；连接失败会自动回到 AP：CAN-Analyzer / 1234567890。关机为深度睡眠，需要按复位或重新上电恢复。</div>
    <div class="network-grid">
      <label>当前模式 <input id="wifi-mode" type="text" readonly/></label>
      <label>当前 IP <input id="wifi-ip" type="text" readonly/></label>
      <label>WiFi 名称 <input id="wifi-ssid" type="text" placeholder="输入 SSID"/></label>
      <label>WiFi 密码 <input id="wifi-pass" type="text" placeholder="输入密码"/></label>
      <button id="wifi-connect-btn">连接 WiFi</button>
      <button id="wifi-refresh-btn">刷新状态</button>
      <button id="device-restart-btn">重启设备</button>
      <button id="device-shutdown-btn">关机（深度睡眠）</button>
      <span id="wifi-status">网络状态：未知</span>
    </div>
  </section>
  <div class="controls">
    <span id="bus-health">CAN_A: ? · CAN_B: ?</span>
    <label>通道 <select id="channel-filter"><option value="all">A+B</option><option value="A">A</option><option value="B">B</option></select></label>
    <label>ID <input id="id-filter" type="text" placeholder="0x123"/></label>
    <label>起始 <input id="range-from" type="text" placeholder="0x000"/></label>
    <label>结束 <input id="range-to" type="text" placeholder="0x7FF"/></label>
    <label>搜索 <input id="search-box" type="text" placeholder="ID"/></label>
  </div>
  <div class="controls">
    <label>进制 <select id="base-select"><option value="hex">HEX</option><option value="dec">DEC</option><option value="bin">BIN</option><option value="ascii">ASCII</option></select></label>
    <label><input id="suppress-static" type="checkbox"/> 静态抑制</label>
    <label>阈值 <input id="static-seconds" type="number" min="1" max="60" value="5"/> 秒</label>
    <label><input id="freeze-view" type="checkbox"/> 冻结</label>
    <label>排序 <select id="sort-select"><option value="id">ID</option><option value="activity">活跃度</option></select></label>
    <span id="bus-stats">A: 0 帧/秒 0.0% · B: 0 帧/秒 0.0% · 丢弃=0</span>
  </div>
  <div id="status">WS：连接中…</div>
  <div class="grid">
    <section><h2>CAN_A 实时表</h2><div class="panel-summary">显示 A 通道每个 ID 的最新数据、接收次数、周期估计和变化活跃度。</div><table id="tbl-a"><thead><tr><th>ID</th><th>DLC</th><th>数据</th><th>计数</th><th>Δt</th><th>周期</th><th>抖动</th><th>活跃</th></tr></thead><tbody></tbody></table></section>
    <section><h2>CAN_B 实时表</h2><div class="panel-summary">显示 B 通道每个 ID 的最新数据、接收次数、周期估计和变化活跃度。</div><table id="tbl-b"><thead><tr><th>ID</th><th>DLC</th><th>数据</th><th>计数</th><th>Δt</th><th>周期</th><th>抖动</th><th>活跃</th></tr></thead><tbody></tbody></table></section>
  </div>
  <script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 2: 提交**

```bash
git add data/analyzer/index.html
git commit -m "refactor(analyzer-ui): minimize index.html to realtime tables + network/power"
```

---

### Task 8: 重写前端 app.js

整体重写为最小版：WS 连接（只解析 frame delta=0x01 / bus stats=0x02）、高性能差分渲染、筛选/排序/静态抑制/冻结、网络与电源面板交互（`/api/wifi`、`/api/restart`、`/api/shutdown`、`/api/status` 只取在线状态）。删除 TX、录制、回放、触发、快照、回看、信号、标签全部逻辑与 DOM 引用。

**Files:**
- Modify: `data/analyzer/app.js`（整体替换）

- [ ] **Step 1: 用最小内容覆盖 `data/analyzer/app.js`（上半：DOM 引用、工具函数、渲染核心）**

完整替换文件。先写上半部分：

```js
const busHealth = document.getElementById('bus-health');
const busStats = document.getElementById('bus-stats');
const statusEl = document.getElementById('status');
const baseSelect = document.getElementById('base-select');
const suppressStatic = document.getElementById('suppress-static');
const staticSeconds = document.getElementById('static-seconds');
const freezeView = document.getElementById('freeze-view');
const sortSelect = document.getElementById('sort-select');
const channelFilter = document.getElementById('channel-filter');
const idFilter = document.getElementById('id-filter');
const rangeFrom = document.getElementById('range-from');
const rangeTo = document.getElementById('range-to');
const searchBox = document.getElementById('search-box');
const wifiMode = document.getElementById('wifi-mode');
const wifiIp = document.getElementById('wifi-ip');
const wifiSsid = document.getElementById('wifi-ssid');
const wifiPass = document.getElementById('wifi-pass');
const wifiConnectBtn = document.getElementById('wifi-connect-btn');
const wifiRefreshBtn = document.getElementById('wifi-refresh-btn');
const deviceRestartBtn = document.getElementById('device-restart-btn');
const deviceShutdownBtn = document.getElementById('device-shutdown-btn');
const wifiStatus = document.getElementById('wifi-status');
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};
const records = {};
let ws = null;
let needSort = false;
let sortQueued = false;
let recordRenderQueued = false;
let fullRecordRenderQueued = false;
const dirtyRecordKeys = new Set();
const lastOrder = { 0: [], 1: [] };

function setText(node, value) {
  const v = String(value);
  if (node.textContent !== v) node.textContent = v;
}
function hex(n, w) { return n.toString(16).toUpperCase().padStart(w, '0'); }
function printable(b) { return b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'; }
function recordKey(ch, id) { return ch * 4096 + id; }
function idText(id) { return '0x' + hex(id, 3); }

function parseBoundedIntText(text, max) {
  const raw = String(text || '').trim();
  if (!raw) throw new Error('请输入数值');
  const base = /^0x/i.test(raw) ? 16 : 10;
  const body = base === 16 ? raw.slice(2) : raw;
  const pattern = base === 16 ? /^[0-9a-fA-F]+$/ : /^[0-9]+$/;
  if (!body || !pattern.test(body)) throw new Error(`非法数值：${raw}`);
  const value = Number.parseInt(body, base);
  if (!Number.isInteger(value) || value < 0 || value > max) throw new Error(`数值超出范围：${raw}`);
  return value;
}
function parseId(text) {
  try { return parseBoundedIntText(text, 0x7ff); } catch (e) { return null; }
}

function formatByte(b) {
  switch (baseSelect.value) {
    case 'dec': return String(b);
    case 'bin': return b.toString(2).padStart(8, '0');
    case 'ascii': return printable(b);
    default: return hex(b, 2);
  }
}
function byteClass(ageMs) {
  if (ageMs < 500) return 'hot';
  if (ageMs < 2500) return 'warm';
  return '';
}
function bitHtml(rec) {
  let out = '';
  for (let byte = 0; byte < rec.dlc; byte++) {
    out += `B${byte}: `;
    for (let bit = 7; bit >= 0; bit--) {
      const one = (rec.data[byte] >> bit) & 1;
      out += `<span class="bit ${one ? 'one' : ''}">${one}</span>`;
    }
    out += ' ';
  }
  return out;
}
function isStatic(rec) {
  if (!suppressStatic.checked) return false;
  const thresholdMs = Math.max(1, Number(staticSeconds.value) || 5) * 1000;
  for (let i = 0; i < rec.dlc; i++) {
    if (rec.byteAge[i] < thresholdMs) return false;
  }
  return true;
}
function rowClass(score) {
  if (score >= 20) return 'activity-high';
  if (score >= 5) return 'activity-med';
  return 'activity-low';
}
function passesLocalFilters(rec) {
  if (channelFilter.value === 'A' && rec.ch !== 0) return false;
  if (channelFilter.value === 'B' && rec.ch !== 1) return false;
  const exact = parseId(idFilter.value);
  if (idFilter.value.trim() && exact === null) return false;
  if (exact !== null && rec.id !== exact) return false;
  const from = parseId(rangeFrom.value);
  const to = parseId(rangeTo.value);
  if (rangeFrom.value.trim() && from === null) return false;
  if (rangeTo.value.trim() && to === null) return false;
  if (from !== null && rec.id < from) return false;
  if (to !== null && rec.id > to) return false;
  const q = searchBox.value.trim().toLowerCase();
  if (q) {
    const idHex = idText(rec.id).toLowerCase();
    const idBare = hex(rec.id, 3).toLowerCase();
    if (!idHex.includes(q) && !idBare.includes(q)) return false;
  }
  return true;
}
function rowHidden(rec) {
  return isStatic(rec) || !passesLocalFilters(rec);
}
```

- [ ] **Step 2: 续写 app.js 中段（ensureRows、paintRecord、sortTables、调度）**

接上一段，继续写入同一文件：

```js
function ensureRows(rec) {
  const key = rec.key;
  if (rows[key]) return rows[key];

  const tr = document.createElement('tr');
  const bit = document.createElement('tr');
  bit.className = 'bit-view hidden';
  bit.dataset.manualHidden = '1';
  const bitTd = document.createElement('td');
  bitTd.colSpan = 8;
  bit.appendChild(bitTd);

  const idTd = document.createElement('td');
  const idSpan = document.createElement('span');
  idSpan.className = 'id-text';
  idSpan.textContent = idText(rec.id);
  idTd.appendChild(idSpan);

  const dataTd = document.createElement('td');
  const byteSpans = [];
  for (let i = 0; i < 8; i++) {
    const s = document.createElement('span');
    s.className = 'byte';
    s.style.display = 'none';
    dataTd.appendChild(s);
    byteSpans.push(s);
  }

  const dlcTd = document.createElement('td');
  const countTd = document.createElement('td');
  const deltaTd = document.createElement('td');
  const periodTd = document.createElement('td');
  const jitterTd = document.createElement('td');
  const scoreTd = document.createElement('td');

  tr.appendChild(idTd);
  tr.appendChild(dlcTd);
  tr.appendChild(dataTd);
  tr.appendChild(countTd);
  tr.appendChild(deltaTd);
  tr.appendChild(periodTd);
  tr.appendChild(jitterTd);
  tr.appendChild(scoreTd);

  tr.onclick = () => {
    const hiddenNow = bit.classList.toggle('hidden');
    bit.dataset.manualHidden = hiddenNow ? '1' : '0';
    if (!hiddenNow) {
      const r = records[key];
      if (r) bitTd.innerHTML = bitHtml(r);
    }
  };

  rows[key] = {
    tr, bit, bitTd, byteSpans,
    dlcTd, countTd, deltaTd, periodTd, jitterTd, scoreTd,
    lastClass: '',
  };
  tbody[rec.ch].appendChild(tr);
  tbody[rec.ch].appendChild(bit);
  needSort = true;
  return rows[key];
}

function paintRecord(rec) {
  const pair = ensureRows(rec);
  const tr = pair.tr;
  const hiddenRow = rowHidden(rec);

  const cls = rowClass(rec.changeScore);
  if (cls !== pair.lastClass) { tr.className = cls; pair.lastClass = cls; }
  tr.classList.toggle('hidden', hiddenRow);

  if (hiddenRow || pair.bit.dataset.manualHidden !== '0') pair.bit.classList.add('hidden');
  else pair.bit.classList.remove('hidden');

  if (hiddenRow) return;

  for (let i = 0; i < 8; i++) {
    const s = pair.byteSpans[i];
    if (i < rec.dlc) {
      const txt = formatByte(rec.data[i]);
      if (s.textContent !== txt) s.textContent = txt;
      const c = ('byte ' + byteClass(rec.byteAge[i])).trim();
      if (s.className !== c) s.className = c;
      if (s.style.display !== '') s.style.display = '';
    } else if (s.style.display !== 'none') {
      s.style.display = 'none';
    }
  }

  setText(pair.dlcTd, rec.dlc);
  setText(pair.countTd, rec.count);
  setText(pair.deltaTd, rec.deltaMs);
  setText(pair.periodTd, rec.periodMs);
  setText(pair.jitterTd, rec.jitterMs);
  setText(pair.scoreTd, rec.changeScore);

  if (!pair.bit.classList.contains('hidden')) pair.bitTd.innerHTML = bitHtml(rec);
}

function sortTables() {
  for (const ch of [0, 1]) {
    const list = Object.values(records)
      .filter(r => r.ch === ch && !rowHidden(r))
      .sort((a, b) => sortSelect.value === 'activity'
        ? (b.changeScore - a.changeScore || a.id - b.id)
        : (a.id - b.id));

    const order = list.map(r => r.key);
    const prev = lastOrder[ch];
    let same = order.length === prev.length;
    if (same) {
      for (let i = 0; i < order.length; i++) {
        if (order[i] !== prev[i]) { same = false; break; }
      }
    }
    if (same) continue;

    lastOrder[ch] = order;
    const frag = document.createDocumentFragment();
    for (const rec of list) {
      const pair = rows[rec.key];
      if (!pair) continue;
      frag.appendChild(pair.tr);
      frag.appendChild(pair.bit);
    }
    tbody[ch].appendChild(frag);
  }
  needSort = false;
}

function scheduleSort() {
  if (sortQueued) return;
  sortQueued = true;
  setTimeout(() => { sortQueued = false; sortTables(); }, 300);
}

function scheduleRecordRender(full = false) {
  if (full) fullRecordRenderQueued = true;
  if (recordRenderQueued) return;
  recordRenderQueued = true;
  requestAnimationFrame(() => {
    recordRenderQueued = false;
    if (fullRecordRenderQueued) {
      fullRecordRenderQueued = false;
      for (const rec of Object.values(records)) {
        if (rows[rec.key] || !rowHidden(rec)) paintRecord(rec);
      }
    } else {
      for (const key of dirtyRecordKeys) {
        const rec = records[key];
        if (rec && (rows[key] || !rowHidden(rec))) paintRecord(rec);
      }
    }
    dirtyRecordKeys.clear();
    if (needSort) scheduleSort();
  });
}

function repaintAll() {
  needSort = true;
  scheduleRecordRender(true);
  scheduleSort();
}
```

- [ ] **Step 3: 续写 app.js 下段（WS 解析、网络/电源、bus-health、绑定与初始化）**

接上一段，写入文件结尾：

```js
function parseDelta(buf) {
  if (freezeView.checked || buf.byteLength < 2) return;
  const dv = new DataView(buf);
  let o = 0;
  if (dv.getUint8(o++) !== 0x01) return;
  const count = dv.getUint8(o++);
  for (let i = 0; i < count; i++) {
    if (o + 45 > buf.byteLength) return;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const dlc = dv.getUint8(o); o += 1;
    const data = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
    const lastRx = dv.getUint32(o, true); o += 4;
    const byteAge = [];
    for (let b = 0; b < 8; b++) { byteAge.push(dv.getUint16(o, true)); o += 2; }
    const countRx = dv.getUint32(o, true); o += 4;
    const deltaMs = dv.getUint16(o, true); o += 2;
    const periodMs = dv.getUint16(o, true); o += 2;
    const jitterMs = dv.getUint16(o, true); o += 2;
    const changeScore = dv.getUint16(o, true); o += 2;
    const flags = dv.getUint8(o); o += 1;
    const key = recordKey(ch, id);
    records[key] = { key, ch, id, dlc, data, byteAge, count: countRx, lastRx, deltaMs, periodMs, jitterMs, changeScore, flags };
    dirtyRecordKeys.add(key);
  }
  scheduleRecordRender();
}

function parseStats(buf) {
  if (buf.byteLength < 23) return;
  const dv = new DataView(buf);
  let o = 1;
  const fpsA = dv.getUint16(o, true); o += 2;
  const fpsB = dv.getUint16(o, true); o += 2;
  const loadA = dv.getUint16(o, true) / 10; o += 2;
  const loadB = dv.getUint16(o, true) / 10; o += 2;
  o += 4 + 4 + 1 + 1;
  const dropped = dv.getUint32(o, true);
  busStats.textContent = `A: ${fpsA} 帧/秒 ${loadA.toFixed(1)}% · B: ${fpsB} 帧/秒 ${loadB.toFixed(1)}% · 丢弃=${dropped}`;
}

function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { statusEl.textContent = 'WS：已连接'; };
  ws.onclose = () => { statusEl.textContent = 'WS：断开，重连中…'; setTimeout(connect, 1000); };
  ws.onmessage = (ev) => {
    if (!(ev.data instanceof ArrayBuffer) || ev.data.byteLength === 0) return;
    const type = new DataView(ev.data).getUint8(0);
    if (type === 0x01) parseDelta(ev.data);
    if (type === 0x02) parseStats(ev.data);
  };
}

function wifiModeText(mode) {
  if (mode === 'sta') return '已连接路由器（STA）';
  if (mode === 'ap') return '热点模式（AP）';
  return '无线已关闭';
}

async function refreshWifiStatus() {
  try {
    const r = await fetch('/api/wifi');
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const s = await r.json();
    wifiMode.value = wifiModeText(s.mode);
    wifiIp.value = s.ip || '';
    wifiSsid.value = s.ssid || '';
    wifiPass.value = s.pass || '';
    wifiStatus.textContent = `网络状态：${wifiModeText(s.mode)} ${s.ip || ''}`;
  } catch (e) {
    wifiStatus.textContent = `网络状态获取失败：${e.message || e}`;
  }
}

async function connectWifi() {
  wifiConnectBtn.disabled = true;
  wifiStatus.textContent = '正在连接 WiFi…';
  try {
    const r = await fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: wifiSsid.value.trim(), pass: wifiPass.value })
    });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const s = await r.json();
    wifiStatus.textContent = s.pending
      ? 'WiFi 配置已保存，设备正在切换网络；请稍后刷新状态，或打开新 IP / AP 地址。'
      : `连接请求已返回：${JSON.stringify(s)}`;
  } catch (e) {
    wifiStatus.textContent = `连接请求失败：${e.message || e}`;
  } finally {
    wifiConnectBtn.disabled = false;
  }
}

async function postDeviceAction(path, message, button) {
  if (button) button.disabled = true;
  try {
    const r = await fetch(path, { method: 'POST' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    wifiStatus.textContent = message;
  } catch (e) {
    wifiStatus.textContent = `操作请求失败：${e.message || e}`;
    if (button) button.disabled = false;
  }
}

async function refreshBusHealth() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    busHealth.textContent = `CAN_A: ${s.can_a_online ? '在线' : '离线'} · CAN_B: ${s.can_b_online ? '在线' : '离线'}`;
  } catch (e) {}
}

wifiConnectBtn.onclick = connectWifi;
wifiRefreshBtn.onclick = refreshWifiStatus;
deviceRestartBtn.onclick = () => {
  if (confirm('确定要重启设备吗？网页会短暂断开。'))
    postDeviceAction('/api/restart', '设备正在重启…', deviceRestartBtn);
};
deviceShutdownBtn.onclick = () => {
  if (confirm('确定要关机（进入深度睡眠）吗？需要按复位或重新上电恢复。'))
    postDeviceAction('/api/shutdown', '设备正在进入深度睡眠…', deviceShutdownBtn);
};
baseSelect.onchange = repaintAll;
suppressStatic.onchange = repaintAll;
staticSeconds.onchange = repaintAll;
sortSelect.onchange = repaintAll;
for (const el of [channelFilter, idFilter, rangeFrom, rangeTo, searchBox]) {
  el.oninput = repaintAll;
  el.onchange = repaintAll;
}

function refreshVisible() {
  if (freezeView.checked) return;
  for (const key in rows) {
    const rec = records[key];
    if (rec && !rowHidden(rec)) paintRecord(rec);
  }
  scheduleSort();
}
setInterval(refreshVisible, 500);

connect();
refreshWifiStatus();
refreshBusHealth();
setInterval(refreshBusHealth, 2000);
```

- [ ] **Step 4: 提交**

```bash
git add data/analyzer/app.js
git commit -m "refactor(analyzer-ui): minimize app.js to ws browse + network/power"
```

---

### Task 9: 清理 style.css（可选，低风险）

删除仅服务于已删 UI 的样式（tx-banner、controls 中 TX 按钮态、record/trigger/replay、snapshot/pretrigger 表、signal-workbench、intro-panel、label-badge、row-actions、selectable-id、whitelisted/baselined 等）。保留：network-panel、controls、grid、table、byte/bit、activity-* 行态、hot/warm、hidden、panel-summary。

**Files:**
- Modify: `data/analyzer/style.css`

- [ ] **Step 1: 找出当前 HTML/JS 仍引用的 class/id，删除其余样式**

Run: `grep -oE 'class="[^"]+"|id="[^"]+"' data/analyzer/index.html | sed -E 's/(class|id)="//;s/"//' | tr ' ' '\n' | sort -u`
然后对照 `app.js` 中出现的 class（`hidden`、`bit-view`、`bit`、`one`、`byte`、`hot`、`warm`、`id-text`、`activity-high/med/low`），把 `style.css` 中未被引用的规则删除。无法确定的规则可保留（样式冗余不影响功能）。

- [ ] **Step 2: 确认文件可读、无语法破坏**

Run: `node -e "require('fs').readFileSync('data/analyzer/style.css','utf8')"`
Expected: 无报错。CSS 无构建步骤，布局靠人工确认未塌。

- [ ] **Step 3: 提交**

```bash
git add data/analyzer/style.css
git commit -m "style(analyzer-ui): drop css for removed features"
```

---

### Task 10: 最终验证

- [ ] **Step 1: 全量 native 测试**

Run: `pio test -e native`
Expected: PASS。保留测试：`test_frame_queue`、`test_bus_stats`、`test_id_table`、`test_analyzer_wifi`、`test_ws_protocol`、`test_can_analyzer_sim`。

- [ ] **Step 2: 固件编译**

Run: `pio run -e analyzer`
Expected: SUCCESS。

- [ ] **Step 3: 确认无残留引用**

Run: `grep -rn "WS_MSG_DIFF\|WS_MSG_SIGNAL\|TxService\|Recorder\|ReplayService\|SnapshotStore\|LabelStore\|PretriggerBuffer\|WatchedSignalWindow\|CommonSignalStore\|RecordTrigger" src/ test/ | grep -v ".pio"`
Expected: 无输出。

- [ ] **Step 4: 上传文件系统镜像（需要设备时执行；无设备可跳过并记录）**

前端改动需重新烧写 LittleFS 才能在设备生效：
Run: `pio run -e analyzer -t uploadfs`（连接设备时）
Expected: 上传成功。无设备时跳过，并在交付说明里注明「前端生效需 `uploadfs`」。

- [ ] **Step 5: 收尾提交（如有验证期间的修正）**

```bash
git add -A
git commit -m "chore(analyzer): finalize minimize verification"
```
