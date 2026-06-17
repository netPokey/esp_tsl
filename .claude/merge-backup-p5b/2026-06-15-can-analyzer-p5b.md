# CAN 分析仪 P5b-1/P5b-2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the P5b safety-first transmit foundation: a unified `TxService` and a single-frame Web API/UI, without adding replay, periodic transmit, scanning, scripting, auth, or configurable rate limits.

**Architecture:** Add `TxService` as the only backend path allowed to call `CanDriver::send()`. Wire `TxService` into `can_analyzer.cpp` and `analyzer_web`, expose `POST /api/tx/send`, and add a small frontend form that uses the existing TX banner/status state and confirms before sending.

**Tech Stack:** Arduino + PlatformIO, ESPAsyncWebServer, ArduinoJson, native Unity tests, existing `CanDriver` abstraction, vanilla JS/CSS frontend.

**Spec:** `docs/superpowers/specs/2026-06-15-can-analyzer-p5b-design.md`

---

## File Structure

- Create: `src/analyzer/tx_service.h` — result enum, constants, `TxService` interface.
- Create: `src/analyzer/tx_service.cpp` — safety checks, 10ms global rate limit, `CanFrame` construction, driver dispatch.
- Create: `test/test_tx_service/test_tx_service.cpp` — native tests with a fake `CanDriver`.
- Modify: `src/analyzer/analyzer_web.h` — include `tx_service`, extend context, add native-test helpers for TX send parsing/result mapping.
- Modify: `src/analyzer/analyzer_web.cpp` — hold `TxService*`, parse `POST /api/tx/send`, map errors to HTTP responses.
- Modify: `src/can_analyzer.cpp` — create `TxService`, initialize it with CAN_A/CAN_B drivers, inject into Web context.
- Modify: `platformio.ini` — add `+<analyzer/tx_service.cpp>` to `[env:native]`.
- Modify: `data/analyzer/index.html` — add single-frame TX form near the TX controls.
- Modify: `data/analyzer/app.js` — form parsing, confirm prompt, POST `/api/tx/send`, status rendering.
- Modify: `data/analyzer/style.css` — lightweight form/status styling.

---

## Task 1: TxService tests and implementation

**Files:**
- Create: `src/analyzer/tx_service.h`
- Create: `src/analyzer/tx_service.cpp`
- Create: `test/test_tx_service/test_tx_service.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Write the failing test**

Create `test/test_tx_service/test_tx_service.cpp`:

```cpp
#include <unity.h>
#include "analyzer/tx_service.h"
#include "analyzer/analyzer_control.h"
#include "can_helpers.h"

class FakeCanDriver : public CanDriver
{
public:
    bool init() override { return true; }
    void setFilters(const uint32_t *, uint8_t) override {}
    bool setBusMode(CanBusMode mode) override
    {
        last_mode = mode;
        return true;
    }
    bool enableInterrupt(void (*)()) override { return false; }
    bool read(CanFrame &) override { return false; }
    void send(const CanFrame &frame) override
    {
        ++send_count;
        last_frame = frame;
    }

    int send_count = 0;
    CanFrame last_frame{};
    CanBusMode last_mode = CanBusMode::ListenOnly;
};

static FakeCanDriver canA;
static FakeCanDriver canB;
static TxService tx;
static uint8_t payload[8] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};

static void allowChannel(uint8_t channel)
{
    setCanTxEnabled(true);
    setAnalyzerChannelTxEnabled(channel, true);
    markAnalyzerChannelOnline(channel, true);
}

void setUp()
{
    setCanTxEnabled(false);
    setAnalyzerChannelTxEnabled(0, false);
    setAnalyzerChannelTxEnabled(1, false);
    markAnalyzerChannelOnline(0, false);
    markAnalyzerChannelOnline(1, false);
    canA = FakeCanDriver{};
    canB = FakeCanDriver{};
    tx.init(&canA, &canB);
}

void tearDown() {}

void test_master_off_rejects()
{
    setAnalyzerChannelTxEnabled(0, true);
    markAnalyzerChannelOnline(0, true);
    TEST_ASSERT_EQUAL(TxSendResult::TxDisabled, tx.sendSingle(0, 0x123, 1, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
}

void test_channel_tx_off_rejects()
{
    setCanTxEnabled(true);
    markAnalyzerChannelOnline(0, true);
    TEST_ASSERT_EQUAL(TxSendResult::TxDisabled, tx.sendSingle(0, 0x123, 1, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
}

void test_channel_offline_rejects()
{
    setCanTxEnabled(true);
    setAnalyzerChannelTxEnabled(0, true);
    TEST_ASSERT_EQUAL(TxSendResult::TxDisabled, tx.sendSingle(0, 0x123, 1, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
}

void test_invalid_channel_rejects()
{
    TEST_ASSERT_EQUAL(TxSendResult::InvalidChannel, tx.sendSingle(2, 0x123, 1, payload, 100));
}

void test_missing_driver_rejects()
{
    tx.init(nullptr, &canB);
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::DriverUnavailable, tx.sendSingle(0, 0x123, 1, payload, 100));
}

void test_invalid_id_rejects()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidId, tx.sendSingle(0, 0x800, 1, payload, 100));
}

void test_invalid_dlc_rejects()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidDlc, tx.sendSingle(0, 0x123, 9, payload, 100));
}

void test_null_data_with_payload_rejects()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidDlc, tx.sendSingle(0, 0x123, 1, nullptr, 100));
}

void test_dlc_zero_allows_null_data()
{
    allowChannel(0);
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(0, 0x123, 0, nullptr, 100));
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    TEST_ASSERT_EQUAL_UINT8(0, canA.last_frame.dlc);
}

void test_success_sends_to_selected_driver()
{
    allowChannel(1);
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(1, 0x321, 3, payload, 100));
    TEST_ASSERT_EQUAL_INT(0, canA.send_count);
    TEST_ASSERT_EQUAL_INT(1, canB.send_count);
    TEST_ASSERT_EQUAL_UINT32(0x321, canB.last_frame.id);
    TEST_ASSERT_EQUAL_UINT8(3, canB.last_frame.dlc);
    TEST_ASSERT_EQUAL_UINT8(0x10, canB.last_frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x12, canB.last_frame.data[2]);
}

void test_rate_limit_is_global_and_only_after_success()
{
    allowChannel(0);
    allowChannel(1);
    TEST_ASSERT_EQUAL(TxSendResult::InvalidId, tx.sendSingle(0, 0x900, 1, payload, 100));
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(0, 0x100, 1, payload, 100));
    TEST_ASSERT_EQUAL(TxSendResult::RateLimited, tx.sendSingle(1, 0x101, 1, payload, 109));
    TEST_ASSERT_EQUAL(TxSendResult::Ok, tx.sendSingle(1, 0x101, 1, payload, 110));
    TEST_ASSERT_EQUAL_INT(1, canA.send_count);
    TEST_ASSERT_EQUAL_INT(1, canB.send_count);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_master_off_rejects);
    RUN_TEST(test_channel_tx_off_rejects);
    RUN_TEST(test_channel_offline_rejects);
    RUN_TEST(test_invalid_channel_rejects);
    RUN_TEST(test_missing_driver_rejects);
    RUN_TEST(test_invalid_id_rejects);
    RUN_TEST(test_invalid_dlc_rejects);
    RUN_TEST(test_null_data_with_payload_rejects);
    RUN_TEST(test_dlc_zero_allows_null_data);
    RUN_TEST(test_success_sends_to_selected_driver);
    RUN_TEST(test_rate_limit_is_global_and_only_after_success);
    return UNITY_END();
}
```

- [ ] **Step 2: Add native source filter and run test to verify failure**

In `platformio.ini`, add this line under `[env:native] build_src_filter`:

```ini
    +<analyzer/tx_service.cpp>
```

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_tx_service
```

Expected: FAIL because `analyzer/tx_service.h` and `tx_service.cpp` do not exist yet.

- [ ] **Step 3: Write the header**

Create `src/analyzer/tx_service.h`:

```cpp
#pragma once
#include <cstdint>
#include "drivers/can_driver.h"

constexpr uint32_t kTxServiceMinIntervalMs = 10;
constexpr uint32_t kTxServiceMaxStandardId = 0x7FF;

enum class TxSendResult : uint8_t
{
    Ok,
    InvalidChannel,
    DriverUnavailable,
    TxDisabled,
    InvalidId,
    InvalidDlc,
    RateLimited,
};

class TxService
{
public:
    void init(CanDriver *canA, CanDriver *canB);
    TxSendResult sendSingle(uint8_t channel, uint32_t id, uint8_t dlc, const uint8_t *data, uint32_t now_ms);

private:
    CanDriver *driverFor(uint8_t channel) const;

    CanDriver *canA_ = nullptr;
    CanDriver *canB_ = nullptr;
    bool has_last_send_ = false;
    uint32_t last_send_ms_ = 0;
};
```

- [ ] **Step 4: Write minimal implementation**

Create `src/analyzer/tx_service.cpp`:

```cpp
#include "analyzer/tx_service.h"
#include "analyzer/analyzer_control.h"

void TxService::init(CanDriver *canA, CanDriver *canB)
{
    canA_ = canA;
    canB_ = canB;
    has_last_send_ = false;
    last_send_ms_ = 0;
}

CanDriver *TxService::driverFor(uint8_t channel) const
{
    if (channel == 0)
        return canA_;
    if (channel == 1)
        return canB_;
    return nullptr;
}

TxSendResult TxService::sendSingle(uint8_t channel, uint32_t id, uint8_t dlc, const uint8_t *data, uint32_t now_ms)
{
    if (channel > 1)
        return TxSendResult::InvalidChannel;

    CanDriver *driver = driverFor(channel);
    if (!driver)
        return TxSendResult::DriverUnavailable;

    if (!shouldAllowAnalyzerChannelTx(channel))
        return TxSendResult::TxDisabled;

    if (id > kTxServiceMaxStandardId)
        return TxSendResult::InvalidId;

    if (dlc > 8 || (dlc > 0 && !data))
        return TxSendResult::InvalidDlc;

    if (has_last_send_ && static_cast<uint32_t>(now_ms - last_send_ms_) < kTxServiceMinIntervalMs)
        return TxSendResult::RateLimited;

    CanFrame frame{};
    frame.id = id;
    frame.dlc = dlc;
    for (uint8_t i = 0; i < dlc; ++i)
        frame.data[i] = data[i];

    driver->send(frame);
    has_last_send_ = true;
    last_send_ms_ = now_ms;
    return TxSendResult::Ok;
}
```

- [ ] **Step 5: Run test to verify pass**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_tx_service
```

Expected: PASS, 11 tests pass.

---

## Task 2: Web TX parsing helpers and result mapping

**Files:**
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp`

- [ ] **Step 1: Add failing helper tests**

Append to `test/test_ws_protocol/test_ws_protocol.cpp` before `main`:

```cpp
void test_parse_tx_id_decimal_and_hex()
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(analyzerWebParseTxIdForTest("291", id));
    TEST_ASSERT_EQUAL_UINT32(291, id);
    TEST_ASSERT_TRUE(analyzerWebParseTxIdForTest("0x123", id));
    TEST_ASSERT_EQUAL_UINT32(0x123, id);
    TEST_ASSERT_TRUE(analyzerWebParseTxIdForTest("0X7FF", id));
    TEST_ASSERT_EQUAL_UINT32(0x7FF, id);
}

void test_parse_tx_id_rejects_bad_values()
{
    uint32_t id = 0;
    TEST_ASSERT_FALSE(analyzerWebParseTxIdForTest(nullptr, id));
    TEST_ASSERT_FALSE(analyzerWebParseTxIdForTest("", id));
    TEST_ASSERT_FALSE(analyzerWebParseTxIdForTest("0x", id));
    TEST_ASSERT_FALSE(analyzerWebParseTxIdForTest("12x", id));
    TEST_ASSERT_FALSE(analyzerWebParseTxIdForTest("2048", id));
    TEST_ASSERT_FALSE(analyzerWebParseTxIdForTest("0x800", id));
}

void test_parse_tx_byte_hex_and_decimal()
{
    uint8_t byte = 0;
    TEST_ASSERT_TRUE(analyzerWebParseTxByteForTest("0xAB", byte));
    TEST_ASSERT_EQUAL_UINT8(0xAB, byte);
    TEST_ASSERT_TRUE(analyzerWebParseTxByteForTest("171", byte));
    TEST_ASSERT_EQUAL_UINT8(171, byte);
}

void test_parse_tx_byte_rejects_bad_values()
{
    uint8_t byte = 0;
    TEST_ASSERT_FALSE(analyzerWebParseTxByteForTest(nullptr, byte));
    TEST_ASSERT_FALSE(analyzerWebParseTxByteForTest("", byte));
    TEST_ASSERT_FALSE(analyzerWebParseTxByteForTest("0x100", byte));
    TEST_ASSERT_FALSE(analyzerWebParseTxByteForTest("256", byte));
    TEST_ASSERT_FALSE(analyzerWebParseTxByteForTest("gg", byte));
}

void test_tx_result_http_mapping()
{
    TEST_ASSERT_EQUAL_INT(200, analyzerWebTxStatusForTest(TxSendResult::Ok));
    TEST_ASSERT_EQUAL_INT(400, analyzerWebTxStatusForTest(TxSendResult::InvalidChannel));
    TEST_ASSERT_EQUAL_INT(400, analyzerWebTxStatusForTest(TxSendResult::InvalidId));
    TEST_ASSERT_EQUAL_INT(400, analyzerWebTxStatusForTest(TxSendResult::InvalidDlc));
    TEST_ASSERT_EQUAL_INT(409, analyzerWebTxStatusForTest(TxSendResult::TxDisabled));
    TEST_ASSERT_EQUAL_INT(429, analyzerWebTxStatusForTest(TxSendResult::RateLimited));
    TEST_ASSERT_EQUAL_INT(503, analyzerWebTxStatusForTest(TxSendResult::DriverUnavailable));
    TEST_ASSERT_EQUAL_STRING("rate_limited", analyzerWebTxErrorForTest(TxSendResult::RateLimited));
}
```

Add these lines to the existing `main` in `test/test_ws_protocol/test_ws_protocol.cpp`:

```cpp
    RUN_TEST(test_parse_tx_id_decimal_and_hex);
    RUN_TEST(test_parse_tx_id_rejects_bad_values);
    RUN_TEST(test_parse_tx_byte_hex_and_decimal);
    RUN_TEST(test_parse_tx_byte_rejects_bad_values);
    RUN_TEST(test_tx_result_http_mapping);
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol
```

Expected: FAIL because `analyzerWebParseTxIdForTest`, `analyzerWebParseTxByteForTest`, `analyzerWebTxStatusForTest`, and `analyzerWebTxErrorForTest` are not defined.

- [ ] **Step 3: Add helpers to analyzer_web.h**

Edit `src/analyzer/analyzer_web.h` and add `#include "analyzer/tx_service.h"`. Then add these helpers before the `#if !defined(ARDUINO)` block:

```cpp
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
```

Inside the `#if !defined(ARDUINO)` block, add:

```cpp
inline bool analyzerWebParseTxIdForTest(const char *text, uint32_t &id)
{
    return analyzerWebParseTxId(text, id);
}

inline bool analyzerWebParseTxByteForTest(const char *text, uint8_t &byte)
{
    return analyzerWebParseTxByte(text, byte);
}

inline int analyzerWebTxStatusForTest(TxSendResult result)
{
    return analyzerWebTxHttpStatus(result);
}

inline const char *analyzerWebTxErrorForTest(TxSendResult result)
{
    return analyzerWebTxError(result);
}
```

- [ ] **Step 4: Run test to verify pass**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol
```

Expected: PASS, existing protocol tests plus 5 new helper tests pass.

---

## Task 3: Backend `/api/tx/send` wiring

**Files:**
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `src/analyzer/analyzer_web.cpp`
- Modify: `src/can_analyzer.cpp`

- [ ] **Step 1: Extend Web context**

Change the declaration in `src/analyzer/analyzer_web.h` to add `TxService *tx_service` at the end:

```cpp
void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels,
                           WatchedSignalWindow *signals, CommonSignalStore *common_signals,
                           Recorder *recorder, TxService *tx_service);
```

- [ ] **Step 2: Add global service pointer and context assignment**

In `src/analyzer/analyzer_web.cpp`, near the existing globals, add:

```cpp
TxService *g_txService = nullptr;
```

Update `analyzerWebSetContext(...)` signature to match the header and add:

```cpp
    g_txService = tx_service;
```

- [ ] **Step 3: Add JSON parser helpers in analyzer_web.cpp**

Inside the anonymous namespace in `src/analyzer/analyzer_web.cpp`, add:

```cpp
constexpr size_t kMaxTxJsonBytes = 256;
char g_txBody[kMaxTxJsonBytes + 1] = {};

bool parseTxByteValue(JsonVariantConst value, uint8_t &out)
{
    if (value.is<int>())
    {
        const int v = value.as<int>();
        if (v < 0 || v > 255)
            return false;
        out = static_cast<uint8_t>(v);
        return true;
    }
    if (value.is<const char *>())
        return analyzerWebParseTxByte(value.as<const char *>(), out);
    return false;
}

bool parseTxSendRequest(JsonDocument &doc, uint8_t &channel, uint32_t &id, uint8_t &dlc, uint8_t *data)
{
    const char *ch = doc["ch"] | nullptr;
    if (!analyzerWebParseChannelToken(ch, channel))
        return false;

    if (doc["id"].is<int>())
    {
        const int parsed = doc["id"].as<int>();
        if (parsed < 0 || parsed > static_cast<int>(kTxServiceMaxStandardId))
            return false;
        id = static_cast<uint32_t>(parsed);
    }
    else if (doc["id"].is<const char *>())
    {
        if (!analyzerWebParseTxId(doc["id"].as<const char *>(), id))
            return false;
    }
    else
    {
        return false;
    }

    if (!doc["dlc"].is<int>())
        return false;
    const int parsedDlc = doc["dlc"].as<int>();
    if (parsedDlc < 0 || parsedDlc > 8)
        return false;
    dlc = static_cast<uint8_t>(parsedDlc);

    JsonArrayConst arr = doc["data"].as<JsonArrayConst>();
    if (arr.isNull() || arr.size() < dlc)
        return false;
    for (uint8_t i = 0; i < dlc; ++i)
        if (!parseTxByteValue(arr[i], data[i]))
            return false;
    return true;
}

void sendTxResult(AsyncWebServerRequest *request, TxSendResult result)
{
    if (result == TxSendResult::Ok)
    {
        request->send(200, "application/json", "{\"ok\":true}");
        return;
    }
    String out = "{\"ok\":false,\"error\":\"";
    out += analyzerWebTxError(result);
    out += "\"}";
    request->send(analyzerWebTxHttpStatus(result), "application/json", out);
}
```

- [ ] **Step 4: Add HTTP route**

In `analyzerWebBegin()`, near the existing `/api/can-tx*` routes, add:

```cpp
    server.on("/api/tx/send", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                  if (!analyzerWebBodyChunkIsValid(index, len, total, kMaxTxJsonBytes))
                  {
                      request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_body\"}");
                      return;
                  }
                  memcpy(g_txBody + index, data, len);
                  if (!analyzerWebBodyChunkCompletes(index, len, total))
                      return;
                  g_txBody[total] = '\0';

                  if (!g_txService)
                  {
                      sendTxResult(request, TxSendResult::DriverUnavailable);
                      return;
                  }

                  JsonDocument doc;
                  if (deserializeJson(doc, g_txBody, total))
                  {
                      request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_json\"}");
                      return;
                  }

                  uint8_t channel = 0;
                  uint32_t id = 0;
                  uint8_t dlc = 0;
                  uint8_t frameData[8] = {};
                  if (!parseTxSendRequest(doc, channel, id, dlc, frameData))
                  {
                      request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_request\"}");
                      return;
                  }

                  sendTxResult(request, g_txService->sendSingle(channel, id, dlc, frameData, millis()));
              });
```

- [ ] **Step 5: Wire TxService in can_analyzer.cpp**

In `src/can_analyzer.cpp`, include:

```cpp
#include "analyzer/tx_service.h"
```

Near existing globals, add:

```cpp
TxService g_txService;
```

After `g_canA` and `g_canB` are created, add:

```cpp
    g_txService.init(g_canA.get(), g_canB.get());
```

Update `analyzerWebSetContext(...)` call to pass `&g_txService` as the final argument:

```cpp
                          &g_commonSignals,
                          g_recordStorage ? &g_recorder : nullptr,
                          &g_txService);
```

- [ ] **Step 6: Build firmware**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio run -e analyzer
```

Expected: SUCCESS.

---

## Task 4: Frontend single-frame send UI

**Files:**
- Modify: `data/analyzer/index.html`
- Modify: `data/analyzer/app.js`
- Modify: `data/analyzer/style.css`

- [ ] **Step 1: Add markup**

In `data/analyzer/index.html`, after the top TX controls block and before `<section class="intro-panel">`, add:

```html
  <div class="controls tx-send-controls">
    <label>发送通道
      <select id="tx-send-channel">
        <option value="A">CAN_A</option>
        <option value="B">CAN_B</option>
      </select>
    </label>
    <label>ID <input id="tx-send-id" type="text" value="0x000" inputmode="text"/></label>
    <label>DLC <input id="tx-send-dlc" type="number" min="0" max="8" value="8"/></label>
    <label>B0 <input class="tx-byte" id="tx-byte-0" type="text" value="00"/></label>
    <label>B1 <input class="tx-byte" id="tx-byte-1" type="text" value="00"/></label>
    <label>B2 <input class="tx-byte" id="tx-byte-2" type="text" value="00"/></label>
    <label>B3 <input class="tx-byte" id="tx-byte-3" type="text" value="00"/></label>
    <label>B4 <input class="tx-byte" id="tx-byte-4" type="text" value="00"/></label>
    <label>B5 <input class="tx-byte" id="tx-byte-5" type="text" value="00"/></label>
    <label>B6 <input class="tx-byte" id="tx-byte-6" type="text" value="00"/></label>
    <label>B7 <input class="tx-byte" id="tx-byte-7" type="text" value="00"/></label>
    <button id="tx-send-btn">发送单帧</button>
    <span id="tx-send-status">单帧发送：待命</span>
  </div>
```

- [ ] **Step 2: Add JS element references and parsing helpers**

Near the top of `data/analyzer/app.js`, add:

```js
const txSendChannel = document.getElementById('tx-send-channel');
const txSendId = document.getElementById('tx-send-id');
const txSendDlc = document.getElementById('tx-send-dlc');
const txSendBtn = document.getElementById('tx-send-btn');
const txSendStatus = document.getElementById('tx-send-status');
const txByteInputs = Array.from({ length: 8 }, (_, i) => document.getElementById(`tx-byte-${i}`));
```

Append helper functions near other UI helpers:

```js
function parseBoundedIntText(text, max) {
  const raw = String(text || '').trim();
  if (!raw) throw new Error('请输入数值');
  const base = raw.startsWith('0x') || raw.startsWith('0X') ? 16 : 10;
  const body = base === 16 ? raw.slice(2) : raw;
  if (!body || !/^[0-9a-fA-F]+$/.test(body)) throw new Error(`非法数值：${raw}`);
  const value = parseInt(body, base);
  if (!Number.isInteger(value) || value < 0 || value > max) throw new Error(`数值超出范围：${raw}`);
  return value;
}

function parseTxSendForm() {
  const ch = txSendChannel.value;
  const id = parseBoundedIntText(txSendId.value, 0x7FF);
  const dlc = Number(txSendDlc.value);
  if (!Number.isInteger(dlc) || dlc < 0 || dlc > 8) throw new Error('DLC 必须是 0..8');
  const data = [];
  for (let i = 0; i < dlc; i++) data.push(parseBoundedIntText(txByteInputs[i].value, 0xFF));
  return { ch, id, dlc, data };
}

function updateTxSendButton() {
  const ch = txSendChannel.value;
  const channelOn = ch === 'A' ? txState.a && txState.onlineA : txState.b && txState.onlineB;
  txSendBtn.disabled = !(txState.master && channelOn);
}
```

- [ ] **Step 3: Update TX state rendering and send action**

In `paintTxState()`, after the existing button state updates, add:

```js
  updateTxSendButton();
```

Append the send handler near other button handlers:

```js
txSendChannel.onchange = updateTxSendButton;
txSendBtn.onclick = async () => {
  let payload;
  try {
    payload = parseTxSendForm();
  } catch (err) {
    txSendStatus.textContent = `单帧发送：${err.message || err}`;
    return;
  }

  const idLabel = `0x${payload.id.toString(16).toUpperCase().padStart(3, '0')}`;
  if (!confirm(`将向通道 ${payload.ch} 发送 ID ${idLabel}，确认继续？`)) return;

  try {
    const r = await fetch('/api/tx/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const body = await r.json().catch(() => ({}));
    if (r.ok && body.ok) txSendStatus.textContent = `单帧发送：${payload.ch} ${idLabel} 已发送`;
    else txSendStatus.textContent = `单帧发送失败：${body.error || r.status}`;
  } catch (err) {
    txSendStatus.textContent = `单帧发送失败：${err.message || err}`;
  }
  refreshTxBanner();
};
```

- [ ] **Step 4: Add styles**

Append to `data/analyzer/style.css`:

```css
.tx-send-controls { align-items: center; }
.tx-send-controls input { width: 72px; }
.tx-send-controls .tx-byte { width: 42px; text-transform: uppercase; }
#tx-send-status { min-width: 180px; color: #ddd; }
```

- [ ] **Step 5: Check JS syntax and build LittleFS**

Run:

```bash
node --check data/analyzer/app.js
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Expected: `node --check` has no output, and buildfs succeeds with only `/app.js`, `/index.html`, `/style.css`.

---

## Task 5: Full verification and review

**Files:**
- Verify all files changed above.

- [ ] **Step 1: Run targeted native tests**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_tx_service
COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol
```

Expected: all tests pass.

- [ ] **Step 2: Run full native suite**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native
```

Expected: all native tests pass.

- [ ] **Step 3: Build firmware**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio run -e analyzer
```

Expected: SUCCESS.

- [ ] **Step 4: Build filesystem image**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Expected: SUCCESS and LittleFS contains only `/app.js`, `/index.html`, `/style.css`.

- [ ] **Step 5: Manual browser verification**

After flashing firmware and filesystem, verify in browser:

1. Default TX OFF: sending a single frame fails and no frame is sent.
2. Master ON but target channel TX OFF: request fails.
3. Target channel offline: request fails.
4. Bad ID, DLC, or byte value is rejected before or by POST.
5. Two rapid successful clicks hit `rate_limited` on the second request.
6. Master ON + target channel TX ON + channel online sends one frame.
7. Top TX banner remains consistent with `/api/status`.

- [ ] **Step 6: Code review**

Use `superpowers:requesting-code-review` or the `superpowers:code-reviewer` agent with this prompt:

```text
Review CAN analyzer P5b-1/P5b-2 against docs/superpowers/specs/2026-06-15-can-analyzer-p5b-design.md and docs/superpowers/plans/2026-06-15-can-analyzer-p5b.md. Focus on TxService safety gates, rate limiting semantics, Web API validation/status mapping, frontend confirmation/disable behavior, and ensuring no replay/periodic/scan/scripting scope crept in. Report Critical/Important/Minor. Do not edit files.
```

---

## Self-Review

- **Spec coverage:** `TxService` safety gates are in Task 1; HTTP API is in Task 3; frontend form and confirm are in Task 4; fixed 10ms global rate limit is in Task 1; out-of-scope items are not implemented by any task.
- **Placeholder scan:** no TBD/TODO/fill-in-later steps; each code-changing step includes concrete code.
- **Type consistency:** `TxSendResult`, `TxService::sendSingle`, `kTxServiceMaxStandardId`, `analyzerWebParseTxId`, `analyzerWebTxHttpStatus`, and `analyzerWebTxError` are defined before later tasks use them.
- **Git note:** This plan omits commit steps because this session’s safety rules require explicit user instruction before creating commits.
