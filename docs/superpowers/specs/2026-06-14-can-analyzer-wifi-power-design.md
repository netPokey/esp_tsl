# CAN 分析仪 WiFi 配置与电源控制设计规范

> 状态：设计已确认，待写实现计划
> 日期：2026-06-14
> 关联阶段：P5a 收尾增强（网络配置、重启、深度睡眠关机）

## 1. 目标与范围

在 CAN 分析仪 Web UI 中增加“网络与电源”面板，让用户无需改固件即可配置 WiFi，并提供重启与关机（深度睡眠）操作。

交付内容：
- 页面可配置 WiFi SSID/密码，密码允许回显。
- 开机读取已保存 WiFi 配置并尝试 STA 连接；失败自动回退 AP 模式。
- 点击“连接 WiFi”时保存配置并立即尝试连接；失败自动回退 AP。
- 页面显示当前网络模式、IP、SSID、密码。
- 提供“重启设备”和“关机（深度睡眠）”按钮。

不在本范围：
- 多 WiFi 配置列表。
- mDNS/固定域名。
- 远程唤醒；深度睡眠后需要按复位或重新上电恢复。

## 2. 当前状态

`src/analyzer/analyzer_wifi.cpp` 当前硬编码两个 STA 凭据：

```cpp
if (trySta("jhwctcm", "12345678")) return WiFi.localIP().toString();
if (trySta("Cc", "452509526..")) return WiFi.localIP().toString();
```

失败后进入 AP：`CAN-Analyzer / analyzer1234`，IP `192.168.4.1`。

新设计移除硬编码私密 WiFi，统一使用 NVS/Preferences 持久化配置。

## 3. 固件设计

### 3.1 WiFi 配置存储

新增或扩展 `analyzer_wifi`，使用 `Preferences` 命名空间 `analyzer_wifi` 保存：

- `ssid`：字符串
- `pass`：字符串

密码按用户要求允许通过 API 回显，因此不做隐藏处理。该设备默认用于可信局域网诊断；若未来部署到不可信网络，应再加鉴权。

### 3.2 开机连接流程

`analyzerWifiBegin()`：

1. 读取 NVS 中的 `ssid/pass`。
2. 若 `ssid` 非空，调用 `trySta(ssid, pass)`，最多等待 10 秒。
3. 成功则保持 `WIFI_STA`，返回 `WiFi.localIP().toString()`。
4. 失败或无配置则调用 `analyzerWifiStartAp()`，启动 AP：`CAN-Analyzer / analyzer1234`，返回 AP IP。

### 3.3 手动连接流程

新增函数：

```cpp
struct AnalyzerWifiStatus {
    bool sta = false;
    bool ap = false;
    String ip;
    String ssid;
    String pass;
};

AnalyzerWifiStatus analyzerWifiStatus();
bool analyzerWifiSaveAndConnect(const char *ssid, const char *pass);
void analyzerWifiStartAp();
```

`analyzerWifiSaveAndConnect()`：
- 保存 `ssid/pass` 到 NVS。
- 断开当前 WiFi。
- 尝试 STA 连接。
- 成功返回 true。
- 失败时启动 AP 并返回 false。

### 3.4 电源控制

新增 Web API：

- `POST /api/restart`：响应 `{ok:true}` 后延迟约 200ms 调用 `ESP.restart()`。
- `POST /api/shutdown`：响应 `{ok:true}` 后：
  - `setCanTxEnabled(false)`
  - `setAnalyzerChannelTxEnabled(0,false)` / `(1,false)`
  - `WiFi.disconnect(true)`、`WiFi.mode(WIFI_OFF)`
  - 延迟短暂时间后 `esp_deep_sleep_start()`

深度睡眠后无法通过网页唤醒，需要按复位或重新上电。

## 4. Web API

### 4.1 `GET /api/wifi`

返回：

```json
{
  "mode": "sta",
  "ip": "192.168.110.13",
  "ssid": "wcqrmyybgs",
  "pass": "1234567890"
}
```

`mode`：`"sta"`、`"ap"` 或 `"off"`。

### 4.2 `POST /api/wifi`

请求 JSON：

```json
{"ssid":"wcqrmyybgs","pass":"1234567890"}
```

行为：保存配置并立即尝试连接。返回：

```json
{"ok":true,"connected":true,"mode":"sta","ip":"192.168.110.13"}
```

失败时返回 HTTP 200，但 `connected:false`，并已回 AP：

```json
{"ok":true,"connected":false,"mode":"ap","ip":"192.168.4.1"}
```

选择 HTTP 200 是为了让前端能正常解析失败状态并显示“已回到 AP 模式”。

### 4.3 `/api/restart` 与 `/api/shutdown`

两者都使用 POST，前端需二次确认。

## 5. 前端设计

在顶部介绍区后添加“网络与电源”面板：

- 当前模式：STA / AP / 未知
- 当前 IP
- SSID 输入框
- 密码输入框（回显当前保存密码）
- “连接 WiFi”按钮
- “刷新状态”按钮
- “重启设备”按钮
- “关机（深度睡眠）”按钮
- 说明文本：
  - 连接失败会自动回 AP：`CAN-Analyzer / analyzer1234`。
  - 关机后需要按复位或重新上电恢复。

前端行为：
- 页面加载时 `GET /api/wifi` 填充状态和输入框。
- 点击“连接 WiFi”后禁用按钮并显示“正在连接…”。
- 成功连接 STA 后提示用户打开新 IP。
- 失败后提示已回 AP，并显示 AP IP。
- 重启/关机用 `confirm()` 二次确认。

## 6. 测试与验收

### native 测试

新增可 native 测试的 helper：
- WiFi JSON 请求体上限与字段解析（若抽成 helper）。
- `/api/wifi` body chunk 边界可复用 `analyzerWebBodyChunkIsValid/Completes`。

### 固件验证

- `COPYFILE_DISABLE=1 pio test -e native`
- `COPYFILE_DISABLE=1 pio run -e analyzer`
- `COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs`

### 设备验收

1. 首次启动无配置：进入 AP `CAN-Analyzer / analyzer1234`，网页可打开 `192.168.4.1`。
2. 页面输入 WiFi，点击“连接 WiFi”：成功后显示新 STA IP。
3. 输入错误密码，点击连接：设备回 AP，页面/串口显示 AP IP。
4. 断电重启：自动读取保存配置并尝试 STA；失败回 AP。
5. “重启设备”：设备重启后重新进入 STA 或 AP。
6. “关机（深度睡眠）”：设备停止 Web 服务；按复位或重新上电后恢复。

## 7. 风险与约束

- 密码回显会泄露给同网段访问页面的人；这是用户明确选择。
- 切换 WiFi 后当前网页连接可能断开；前端只能提示新 IP 或 AP IP。
- 深度睡眠不等于断电，USB 串口/板载电源仍可能有指示灯；恢复需要复位/重新上电。
