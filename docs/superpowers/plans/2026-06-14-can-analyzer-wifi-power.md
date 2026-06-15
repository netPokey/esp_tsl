# CAN 分析仪 WiFi 配置与电源控制 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Web UI 中配置 WiFi SSID/密码并持久化，开机/点击连接时尝试 STA，失败回 AP，同时提供重启和深度睡眠关机按钮。

**Architecture:** 扩展 `analyzer_wifi` 为单一 WiFi 管理单元：NVS 保存凭据、开机连接、手动保存并连接、状态查询、AP 回退。`analyzer_web` 新增 `/api/wifi`、`/api/restart`、`/api/shutdown`；前端新增“网络与电源”面板并调用这些 API。

**Tech Stack:** Arduino WiFi、Preferences(NVS)、ESPAsyncWebServer、ArduinoJson、ESP32 deep sleep、原生 JS 前端、PlatformIO native/analyzer。

**Spec:** `docs/superpowers/specs/2026-06-14-can-analyzer-wifi-power-design.md`

---

### Task 1: WiFi 配置存储与状态 helper

**Files:**
- Modify: `src/analyzer/analyzer_wifi.h`
- Modify: `src/analyzer/analyzer_wifi.cpp`
- Modify: `platformio.ini`
- Test: `test/test_analyzer_wifi/test_analyzer_wifi.cpp`

- [ ] **Step 1: 写 native 可测的数据结构和 helper 声明**

在 `src/analyzer/analyzer_wifi.h` 改为：

```cpp
#pragma once
#include <Arduino.h>
#include <cstddef>

constexpr size_t kAnalyzerWifiMaxSsid = 32;
constexpr size_t kAnalyzerWifiMaxPass = 64;

struct AnalyzerWifiCredentials
{
    char ssid[kAnalyzerWifiMaxSsid + 1] = {};
    char pass[kAnalyzerWifiMaxPass + 1] = {};
};

struct AnalyzerWifiStatus
{
    bool sta = false;
    bool ap = false;
    String ip;
    String ssid;
    String pass;
};

bool analyzerWifiSanitizeCredentials(const char *ssid, const char *pass, AnalyzerWifiCredentials &out);
String analyzerWifiBegin();
AnalyzerWifiStatus analyzerWifiStatus();
bool analyzerWifiSaveAndConnect(const char *ssid, const char *pass);
void analyzerWifiStartAp();
```

- [ ] **Step 2: 写失败测试**

Create `test/test_analyzer_wifi/test_analyzer_wifi.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "analyzer/analyzer_wifi.h"

void test_sanitize_accepts_normal_credentials()
{
    AnalyzerWifiCredentials c;
    TEST_ASSERT_TRUE(analyzerWifiSanitizeCredentials("wcqrmyybgs", "1234567890", c));
    TEST_ASSERT_EQUAL_STRING("wcqrmyybgs", c.ssid);
    TEST_ASSERT_EQUAL_STRING("1234567890", c.pass);
}

void test_sanitize_rejects_empty_ssid()
{
    AnalyzerWifiCredentials c;
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials("", "123", c));
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials(nullptr, "123", c));
}

void test_sanitize_rejects_too_long_ssid_or_pass()
{
    char ssid[kAnalyzerWifiMaxSsid + 2];
    memset(ssid, 'a', sizeof(ssid));
    ssid[sizeof(ssid) - 1] = '\0';
    char pass[kAnalyzerWifiMaxPass + 2];
    memset(pass, 'b', sizeof(pass));
    pass[sizeof(pass) - 1] = '\0';
    AnalyzerWifiCredentials c;
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials(ssid, "123", c));
    TEST_ASSERT_FALSE(analyzerWifiSanitizeCredentials("ssid", pass, c));
}

void test_sanitize_allows_empty_password()
{
    AnalyzerWifiCredentials c;
    TEST_ASSERT_TRUE(analyzerWifiSanitizeCredentials("open-ap", "", c));
    TEST_ASSERT_EQUAL_STRING("open-ap", c.ssid);
    TEST_ASSERT_EQUAL_STRING("", c.pass);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_accepts_normal_credentials);
    RUN_TEST(test_sanitize_rejects_empty_ssid);
    RUN_TEST(test_sanitize_rejects_too_long_ssid_or_pass);
    RUN_TEST(test_sanitize_allows_empty_password);
    return UNITY_END();
}
```

- [ ] **Step 3: 注册 native 源并确认失败**

在 `platformio.ini` 的 `[env:native] build_src_filter` 追加：

```ini
    +<analyzer/analyzer_wifi.cpp>
```

Run: `find . -name '._*' -delete 2>/dev/null; COPYFILE_DISABLE=1 pio test -e native -f test_analyzer_wifi`

Expected: FAIL（`analyzerWifiSanitizeCredentials` 未实现）。

- [ ] **Step 4: 实现 helper 与 ARDUINO / native 分支**

Replace `src/analyzer/analyzer_wifi.cpp` with:

```cpp
#include "analyzer/analyzer_wifi.h"
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
#include <WiFi.h>
#endif

namespace
{
constexpr const char *kPrefsNs = "an_wifi";
constexpr const char *kPrefsSsid = "ssid";
constexpr const char *kPrefsPass = "pass";
constexpr const char *kApSsid = "CAN-Analyzer";
constexpr const char *kApPass = "analyzer1234";
constexpr unsigned long kStaTimeoutMs = 10000;

#if defined(ARDUINO)
bool trySta(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == 0)
        return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass ? pass : "");
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kStaTimeoutMs)
        delay(200);
    return WiFi.status() == WL_CONNECTED;
}

AnalyzerWifiCredentials loadCredentials()
{
    AnalyzerWifiCredentials c;
    Preferences prefs;
    if (!prefs.begin(kPrefsNs, true))
        return c;
    const String ssid = prefs.getString(kPrefsSsid, "");
    const String pass = prefs.getString(kPrefsPass, "");
    analyzerWifiSanitizeCredentials(ssid.c_str(), pass.c_str(), c);
    prefs.end();
    return c;
}

bool saveCredentials(const AnalyzerWifiCredentials &c)
{
    Preferences prefs;
    if (!prefs.begin(kPrefsNs, false))
        return false;
    const bool ok = prefs.putString(kPrefsSsid, c.ssid) > 0 && prefs.putString(kPrefsPass, c.pass) >= 0;
    prefs.end();
    return ok;
}
#endif
}

bool analyzerWifiSanitizeCredentials(const char *ssid, const char *pass, AnalyzerWifiCredentials &out)
{
    memset(&out, 0, sizeof(out));
    if (!ssid || ssid[0] == '\0')
        return false;
    const size_t ssidLen = strlen(ssid);
    const size_t passLen = pass ? strlen(pass) : 0;
    if (ssidLen > kAnalyzerWifiMaxSsid || passLen > kAnalyzerWifiMaxPass)
        return false;
    memcpy(out.ssid, ssid, ssidLen);
    if (passLen > 0)
        memcpy(out.pass, pass, passLen);
    return true;
}

void analyzerWifiStartAp()
{
#if defined(ARDUINO)
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPass);
    delay(120);
#endif
}

String analyzerWifiBegin()
{
#if defined(ARDUINO)
    const AnalyzerWifiCredentials c = loadCredentials();
    if (c.ssid[0] != '\0' && trySta(c.ssid, c.pass))
        return WiFi.localIP().toString();
    analyzerWifiStartAp();
    return WiFi.softAPIP().toString();
#else
    return String();
#endif
}

AnalyzerWifiStatus analyzerWifiStatus()
{
    AnalyzerWifiStatus s;
#if defined(ARDUINO)
    const AnalyzerWifiCredentials c = loadCredentials();
    s.ssid = c.ssid;
    s.pass = c.pass;
    const wifi_mode_t mode = WiFi.getMode();
    s.sta = (mode == WIFI_STA || mode == WIFI_AP_STA) && WiFi.status() == WL_CONNECTED;
    s.ap = (mode == WIFI_AP || mode == WIFI_AP_STA);
    if (s.sta)
        s.ip = WiFi.localIP().toString();
    else if (s.ap)
        s.ip = WiFi.softAPIP().toString();
#endif
    return s;
}

bool analyzerWifiSaveAndConnect(const char *ssid, const char *pass)
{
#if defined(ARDUINO)
    AnalyzerWifiCredentials c;
    if (!analyzerWifiSanitizeCredentials(ssid, pass, c))
        return false;
    saveCredentials(c);
    WiFi.disconnect(true);
    delay(100);
    if (trySta(c.ssid, c.pass))
        return true;
    analyzerWifiStartAp();
#endif
    return false;
}
```

- [ ] **Step 5: 测试并提交**

Run: `find . -name '._*' -delete 2>/dev/null; COPYFILE_DISABLE=1 pio test -e native -f test_analyzer_wifi`

Expected: PASS（4/4）。

Commit:

```bash
git add src/analyzer/analyzer_wifi.h src/analyzer/analyzer_wifi.cpp test/test_analyzer_wifi/test_analyzer_wifi.cpp platformio.ini
git commit -m "feat(analyzer): add persistent WiFi configuration helpers"
```

---

### Task 2: WiFi 与电源 Web API

**Files:**
- Modify: `src/analyzer/analyzer_web.cpp`
- Modify: `src/analyzer/analyzer_web.h`（仅在需要 include 时）

- [ ] **Step 1: 添加 include**

在 `src/analyzer/analyzer_web.cpp` include 区加入：

```cpp
#include "analyzer/analyzer_wifi.h"
#include <esp_sleep.h>
```

确认已有 `ArduinoJson.h`，用于解析 `/api/wifi` JSON body。

- [ ] **Step 2: 添加 WiFi JSON body 缓冲**

在现有全局 `g_commonSignalBody` 附近加入：

```cpp
constexpr size_t kMaxWifiJsonBytes = 256;
char g_wifiBody[kMaxWifiJsonBytes] = {};
```

- [ ] **Step 3: 添加 WiFi 状态 JSON helper**

在匿名 namespace 内加入：

```cpp
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
```

- [ ] **Step 4: 添加 GET /api/wifi**

在 `analyzerWebBegin()` 内、`/api/status` 附近加入：

```cpp
    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", wifiStatusJson());
    });
```

- [ ] **Step 5: 添加 POST /api/wifi**

在 `analyzerWebBegin()` 内加入：

```cpp
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

                  JsonDocument doc;
                  if (deserializeJson(doc, g_wifiBody, total))
                  {
                      request->send(400, "application/json", "{\"ok\":false}");
                      return;
                  }
                  const char *ssid = doc["ssid"] | "";
                  const char *pass = doc["pass"] | "";
                  const bool connected = analyzerWifiSaveAndConnect(ssid, pass);
                  request->send(200, "application/json", wifiStatusJson(true, connected, true));
              });
```

- [ ] **Step 6: 添加 restart/shutdown 路由**

在 `analyzerWebBegin()` 内加入：

```cpp
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        delay(200);
        ESP.restart();
    });

    server.on("/api/shutdown", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"ok\":true}");
        setCanTxEnabled(false);
        setAnalyzerChannelTxEnabled(0, false);
        setAnalyzerChannelTxEnabled(1, false);
        delay(200);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        esp_deep_sleep_start();
    });
```

If `WiFi` is not visible, add `#include <WiFi.h>`.

- [ ] **Step 7: 构建验证并提交**

Run: `find . -name '._*' -delete 2>/dev/null; COPYFILE_DISABLE=1 pio run -e analyzer`

Expected: SUCCESS.

Commit:

```bash
git add src/analyzer/analyzer_web.cpp src/analyzer/analyzer_web.h
git commit -m "feat(analyzer): add WiFi and power control web APIs"
```

---

### Task 3: 前端网络与电源面板

**Files:**
- Modify: `data/analyzer/index.html`
- Modify: `data/analyzer/app.js`
- Modify: `data/analyzer/style.css`

- [ ] **Step 1: 添加 HTML 面板**

在 `intro-panel` 后插入：

```html
  <section class="network-panel">
    <h2>网络与电源</h2>
    <div class="panel-summary">配置 WiFi 后设备会优先连接路由器；连接失败会自动回到 AP：CAN-Analyzer / analyzer1234。关机为深度睡眠，需要按复位或重新上电恢复。</div>
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
```

- [ ] **Step 2: app.js 添加元素引用**

顶部元素引用区加入：

```javascript
const wifiMode = document.getElementById('wifi-mode');
const wifiIp = document.getElementById('wifi-ip');
const wifiSsid = document.getElementById('wifi-ssid');
const wifiPass = document.getElementById('wifi-pass');
const wifiConnectBtn = document.getElementById('wifi-connect-btn');
const wifiRefreshBtn = document.getElementById('wifi-refresh-btn');
const deviceRestartBtn = document.getElementById('device-restart-btn');
const deviceShutdownBtn = document.getElementById('device-shutdown-btn');
const wifiStatus = document.getElementById('wifi-status');
```

- [ ] **Step 3: app.js 添加网络函数**

在 `refreshTxBanner()` 前加入：

```javascript
function wifiModeText(mode) {
  if (mode === 'sta') return '已连接路由器（STA）';
  if (mode === 'ap') return '热点模式（AP）';
  return '无线已关闭';
}

async function refreshWifiStatus() {
  try {
    const r = await fetch('/api/wifi');
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
    const s = await r.json();
    wifiMode.value = wifiModeText(s.mode);
    wifiIp.value = s.ip || '';
    wifiStatus.textContent = s.connected
      ? `连接成功，请打开新地址：http://${s.ip}`
      : `连接失败，已回到 AP 模式：http://${s.ip}`;
  } catch (e) {
    wifiStatus.textContent = `连接请求失败：${e.message || e}`;
  } finally {
    wifiConnectBtn.disabled = false;
  }
}

async function postDeviceAction(path, message) {
  try {
    await fetch(path, { method: 'POST' });
    wifiStatus.textContent = message;
  } catch (e) {
    wifiStatus.textContent = `操作请求失败：${e.message || e}`;
  }
}
```

- [ ] **Step 4: app.js 添加事件绑定和启动加载**

在按钮绑定区加入：

```javascript
wifiConnectBtn.onclick = connectWifi;
wifiRefreshBtn.onclick = refreshWifiStatus;
deviceRestartBtn.onclick = () => {
  if (confirm('确定要重启设备吗？网页会短暂断开。'))
    postDeviceAction('/api/restart', '设备正在重启…');
};
deviceShutdownBtn.onclick = () => {
  if (confirm('确定要关机（进入深度睡眠）吗？需要按复位或重新上电恢复。'))
    postDeviceAction('/api/shutdown', '设备正在进入深度睡眠…');
};
```

在文件末尾初始化区加入：

```javascript
refreshWifiStatus();
```

- [ ] **Step 5: style.css 加样式**

追加：

```css
.network-panel { margin: 8px; padding: 10px; border: 1px solid #333; background: #151515; }
.network-panel h2 { margin: 0 0 6px; }
.network-grid { display: grid; grid-template-columns: repeat(4, minmax(150px, 1fr)); gap: 8px; align-items: end; }
.network-grid label { display: flex; flex-direction: column; gap: 3px; color: #aaa; }
.network-grid input { width: 100%; }
#wifi-status { color: #aaa; grid-column: span 4; }
@media (max-width: 900px) {
  .network-grid { grid-template-columns: 1fr; }
  #wifi-status { grid-column: span 1; }
}
```

- [ ] **Step 6: 验证并提交**

Run:

```bash
node --check data/analyzer/app.js
find . -name '._*' -delete 2>/dev/null; COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Expected: JS syntax OK, buildfs SUCCESS.

Commit:

```bash
git add data/analyzer/index.html data/analyzer/app.js data/analyzer/style.css
git commit -m "feat(analyzer): add WiFi and power controls to web UI"
```

---

### Task 4: 设备验证与收尾

- [ ] **Step 1: 全量验证**

Run:

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Expected:
- native 全部通过。
- analyzer SUCCESS。
- buildfs SUCCESS，LittleFS 仅 `/app.js`、`/index.html`、`/style.css`。

- [ ] **Step 2: 烧录设备测试**

Run:

```bash
COPYFILE_DISABLE=1 pio run -e analyzer -t upload --upload-port /dev/cu.usbmodem21201
COPYFILE_DISABLE=1 pio run -e analyzer -t uploadfs --upload-port /dev/cu.usbmodem21201
```

Manual/API verification:
- 无配置或错误密码：设备回 AP `CAN-Analyzer / analyzer1234`。
- `GET /api/wifi` 返回 mode/ip/ssid/pass。
- `POST /api/wifi` 正确密码后返回 `connected:true` 与 STA IP。
- `POST /api/restart` 设备重启。
- `POST /api/shutdown` 设备进入 deep sleep；按复位或重新上电恢复。

- [ ] **Step 3: 最终审查**

派发 code-reviewer 审查全部 WiFi/电源实现：
- 密码回显是用户明确要求。
- 没有硬编码用户私密 WiFi。
- 连接失败 AP 回退完整。
- restart/shutdown 使用 POST 且前端 confirm。
- shutdown 前 TX 关闭。

- [ ] **Step 4: 提交/合并收尾**

使用 finishing-a-development-branch 流程；合并 main 前确认：
- 工作区无临时 WiFi 凭据。
- 所有测试/构建通过。
- 用户确认设备验收结果。
