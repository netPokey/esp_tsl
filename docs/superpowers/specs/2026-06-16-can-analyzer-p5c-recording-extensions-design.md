# CAN 分析仪 P5c：录制扩展设计规范

> 日期：2026-06-16  
> 状态：待用户审核  
> 关联前序：`docs/superpowers/specs/2026-06-16-can-analyzer-p5b3-replay-design.md`

## 1. 目标与范围

P5c 补齐 P5 录制侧剩余能力，不新增任何发送能力：

1. **ASC 导出**
   - 保留现有 CSV 下载入口。
   - 新增当前 recorder 缓冲的 `.asc` 下载。
   - 使用当前 stopped recorder buffer，按旧到新顺序流式输出。

2. **触发录制**
   - 用户可 arm 一个触发条件。
   - 条件命中后在 analyzer loop/pending 路径启动现有 `Recorder`。
   - 支持 disarm。
   - status 和 UI 展示 arm/triggered/error 状态。

明确不在 P5c 范围：

- ASC 上传或解析。
- 从文件回放。
- 自动停止录制。
- 录制落 SD/SPIFFS。
- 周期发送、扫描、发送脚本。
- 新增鉴权、token、密码或持久化触发配置。

## 2. 安全与并发原则

P5c 只读/采集，不向 CAN 总线发送帧，但仍必须保持现有并发边界：

1. 下载 recorder 内容时，录制必须已停止；录制中下载返回 `409 stop recording first`。
2. ASC 与 CSV 使用相同下载互斥规则，避免 AsyncTCP chunk 读取 recorder 时 Core1 同时 push。
3. HTTP callback 只解析请求并 enqueue pending command，不直接调用 `Recorder::start()` / `stop()`。
4. replay running 时拒绝 arm trigger，也拒绝 trigger 命中后启动录制。
5. 触发录制命中后只启动 recorder，不改变 TX 状态、不调用 `TxService`、不触碰 CAN driver。
6. 同一时间只允许一个 trigger armed；新的 arm 替换旧 trigger 前必须通过 pending command 串行处理。

## 3. ASC 导出设计

### 3.1 格式

新增 `src/analyzer/record_asc_format.h/.cpp`，与现有 `record_format` 分离。

输出采用 Vector ASC 风格的简化文本，示例：

```text
date Tue Jun 16 00:00:00.000 2026
base hex  timestamps absolute
internal events logged
Begin Triggerblock
   0.000000 1 123 Rx d 3 10 11 12
   0.010500 2 321 Rx d 8 01 02 03 04 05 06 07 08
End Triggerblock
```

字段约定：

- 时间为相对第一帧的秒，保留 6 位小数。
- CAN channel：`CapturedFrame.channel == 0` 输出 `1`，`1` 输出 `2`，其它输出 `0`。
- ID 使用大写十六进制，不带 `0x`，标准帧至少 3 位。
- 方向固定 `Rx`。
- DLC 使用十进制。
- data 输出最多 8 个字节，每个字节两位大写十六进制，以空格分隔。
- 当前系统只支持标准帧导出；不新增 extended frame 字段。

### 3.2 流式下载

新增：

```cpp
size_t recordAscHeader(char *out, size_t cap);
size_t recordAscFooter(char *out, size_t cap);
size_t recordAscLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us);

struct RecordAscCursor
{
    size_t frame_index = 0;
    bool header_sent = false;
    bool footer_sent = false;
    uint64_t base_ts_us = 0;
    bool base_set = false;
};

size_t recordAscFill(char *buf, size_t maxLen, const Recorder &rec, size_t total, RecordAscCursor &cursor);
```

`recordAscFill()` 行为与 `recordCsvFill()` 一致：

- 首次输出 header。
- 按 old-to-new 输出 frame lines。
- 全部 frame 输出后输出 footer。
- 单行或 footer 容不下时停在边界，下次继续。
- 返回 `0` 表示结束。

### 3.3 Web API

新增：

```http
GET /api/record/download.asc
```

响应：

| 场景 | HTTP | 内容 |
|---|---:|---|
| recorder missing | 404 | `no recording` |
| recorder active | 409 | `stop recording first` |
| empty recorder | 404 | `no recording` |
| ok | 200 | `text/plain` chunked response |

下载文件名：`can-record.asc`。

## 4. 触发录制设计

### 4.1 触发模式

新增 `RecordingTriggerService`，文件：

- `src/analyzer/record_trigger.h`
- `src/analyzer/record_trigger.cpp`
- `test/test_record_trigger/test_record_trigger.cpp`

类型：

```cpp
enum class RecordTriggerMode : uint8_t
{
    Disabled,
    NewId,
    IdChange,
    AnyChange,
};

enum class RecordTriggerState : uint8_t
{
    Idle,
    Armed,
    Triggered,
    Failed,
};

enum class RecordTriggerArmResult : uint8_t
{
    Ok,
    RecorderUnavailable,
    AlreadyRecording,
    ReplayRunning,
    InvalidTarget,
};
```

配置：

```cpp
struct RecordTriggerConfig
{
    RecordTriggerMode mode = RecordTriggerMode::Disabled;
    uint8_t channel = 0;
    uint16_t id = 0;
};
```

服务接口：

```cpp
class RecordTriggerService
{
public:
    void init(Recorder *recorder, ReplayService *replay, IdTable *table);
    RecordTriggerArmResult arm(const RecordTriggerConfig &config);
    void disarm();
    void observe(const CapturedFrame &frame);
    RecordTriggerState state() const;
    RecordTriggerMode mode() const;
    uint8_t channel() const;
    uint16_t id() const;
    const char *error() const;
};
```

### 4.2 触发判定

`observe()` 在 `drainQueueIntoTable()` 中调用，位置必须在 `g_table->update(cap)` 之前，这样可以对比旧表状态：

- `NewId`：`IdTable` 中目标 `(channel,id)` 尚未 present 时触发；此模式不需要指定 ID。
- `AnyChange`：该 `(channel,id)` 已 present 且 DLC 或 data 发生变化时触发。
- `IdChange`：指定 `(channel,id)` 已 present 且 DLC 或 data 发生变化时触发。

命中后：

1. 如果 recorder missing，状态变 `Failed`，error=`recorder_unavailable`。
2. 如果 replay running，状态变 `Failed`，error=`replay_running`。
3. 如果 recorder 已 active，状态变 `Triggered`，不重复 start。
4. 否则调用 `Recorder::start()`，状态变 `Triggered`。
5. 命中的当前 frame 不强制补写到 recorder；录制从 trigger 命中后的后续 drain 开始。P5c 不引入 pre-trigger 自动拼接。

### 4.3 Web API / pending command

新增 pending command：

- `RecordTriggerArm`
- `RecordTriggerDisarm`

新增 HTTP：

```http
POST /api/record/trigger/arm
Content-Type: application/json

{
  "mode": "new_id" | "id_change" | "any_change",
  "ch": "A" | "B",
  "id": 291
}
```

字段规则：

- `new_id` 不需要 `ch/id`；如果提供也忽略。
- `any_change` 不需要 `ch/id`；如果提供也忽略。
- `id_change` 必须提供合法 `ch` 和标准 ID。
- ID 支持现有 TX parser 同款十进制或 `0x` 十六进制字符串；JSON number 也接受。

响应：

| 场景 | HTTP | JSON |
|---|---:|---|
| 请求合法且入队 | 200 | `{ "ok": true, "pending": true }` |
| JSON/字段非法 | 400 | `{ "ok": false, "error": "bad_request" }` |
| queue full | 503 | `{ "ok": false, "error": "queue_full" }` |

新增：

```http
POST /api/record/trigger/disarm
```

只 enqueue disarm。

### 4.4 Status

`/api/status` 增加：

```json
{
  "record_trigger_state": "idle",
  "record_trigger_mode": "disabled",
  "record_trigger_channel": "A",
  "record_trigger_id": 0,
  "record_trigger_error": ""
}
```

字符串：

- state: `idle`, `armed`, `triggered`, `failed`
- mode: `disabled`, `new_id`, `id_change`, `any_change`
- error: empty, `recorder_unavailable`, `already_recording`, `replay_running`, `invalid_target`

## 5. UI 设计

录制控制区扩展：

1. 保留 CSV 下载。
2. 新增 ASC 下载链接，启用条件与 CSV 相同：非 recording 且 `record_count > 0`。
3. 新增“触发录制”区域：
   - mode select：新 ID、指定 ID 变化、任意变化。
   - channel select：A/B，仅 `id_change` 时启用。
   - ID input，仅 `id_change` 时启用。
   - arm / disarm 按钮。
   - 状态文本：state/mode/target/error。
4. replay running 时禁用 arm。
5. recording active 时禁用 arm。
6. disarm 仅在 armed 或 failed/triggered 状态可点击，用于清空状态回 idle。

## 6. 测试策略

Native tests：

1. `test_record_asc_format`
   - header/footer。
   - line channel/id/dlc/data formatting。
   - base timestamp relative time。
   - cap too small returns 0。
   - `recordAscFill()` chunk boundary and footer emission。

2. `test_record_trigger`
   - arm invalid config rejects。
   - arm rejects recorder unavailable。
   - arm rejects already recording。
   - arm rejects replay running。
   - `NewId` triggers before table update。
   - `AnyChange` triggers on DLC/data change only。
   - `IdChange` triggers only selected channel/id。
   - disarm prevents trigger。

3. `test_ws_protocol`
   - trigger mode parser。
   - trigger state/mode/error strings。
   - arm request parser for JSON number and string ID。
   - bad request cases。

Build verification：

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_record_asc_format
COPYFILE_DISABLE=1 pio test -e native -f test_record_trigger
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

## 7. 后续阶段边界

P5d 扫描与脚本、P5e 周期发送会继续复用 `TxService`，并要求：

- TX Master + channel TX + online + rate limit 仍为最终安全边界。
- start/stop 走 pending command。
- HTTP callback 不直接发送。
- 与 replay、recording trigger 的运行状态互斥。

P5c 不为后续阶段预先实现任何发送调度，只预留 status/UI 空间时保持最小改动。
