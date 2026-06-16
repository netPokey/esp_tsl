# CAN 分析仪 P5b-3：当前录制缓冲回放设计规范

> 日期：2026-06-16  
> 状态：待用户审核  
> 关联前序：`docs/superpowers/specs/2026-06-14-can-analyzer-p5a-design.md`、`docs/superpowers/specs/2026-06-15-can-analyzer-p5b-design.md`

## 1. 目标与范围

P5b-3 在已完成的 P5a 录制缓冲与 P5b-1/P5b-2 安全发送边界之上，实现“当前 recorder 缓冲按原始时间间隔回放”。

本阶段只覆盖：

1. **后端回放调度服务**
   - 从已停止的 `Recorder` 中复制当前缓冲帧。
   - 按 `CapturedFrame.ts_us` 的相邻时间差调度发送。
   - 每一帧发送都调用 `TxService::sendSingle()`。
   - 支持启动、停止、完成状态。

2. **Web API / 命令接线**
   - 新增回放启动与停止入口。
   - 启动请求只入队 pending command，不在 HTTP/WS callback 中直接触碰 CAN driver。
   - `/api/status` 暴露回放状态、进度和最近错误。

3. **Web UI**
   - 在录制控制区附近增加“回放当前录制”控制。
   - 支持目标通道选择：原通道、强制 A、强制 B。
   - 启动前二次确认，文案必须包含回放目标和风险提示。
   - 支持停止正在进行的回放。

明确不在本阶段范围：

- CSV 上传回放。
- ASC 解析或导入。
- 从下载文件回放。
- 周期 / 循环发送任务。
- 字节扫描 / bit 翻转扫描。
- 发送序列 / 脚本。
- 可配置回放倍速、暂停、单步、循环回放。
- 持久化回放配置。
- 新增 token、密码、鉴权或物理确认流程。

## 2. 安全原则

回放是批量写总线功能，风险高于单帧发送，因此必须保持失败关闭和可停止。

安全条件：

1. 回放只能在 `Recorder` 存在、已停止、且缓冲非空时启动。
2. 回放启动前必须二次确认。
3. 任意正在进行的回放必须能被 stop 命令停止。
4. 每一帧都必须通过 `TxService::sendSingle()`，不得直接调用 `CanDriver::send()`。
5. `TxService` 的 TX Master、分通道 TX enable、通道 online、driver 可用、ID/DLC/data、10ms 全局限速检查仍是最终安全边界。
6. 目标通道为“原通道”时，逐帧使用录制时的 `CapturedFrame.channel`；强制 A/B 时，所有帧重定向到指定通道。
7. 回放期间如果某一帧被 `TxService` 拒绝，回放应停止并记录最近错误，避免继续刷后续帧。

## 3. 架构

### 3.1 复用现有数据

P5a 已有：

- `Recorder::active()`：判断录制是否进行中。
- `Recorder::count()`：当前缓冲帧数。
- `Recorder::collect(out, cap, skip)`：按旧到新顺序复制缓冲帧。
- `CapturedFrame.channel`：原始通道，`0 = A`，`1 = B`。
- `CapturedFrame.ts_us`：采集微秒时间戳。

P5b-3 不改变 recorder 的写入模型。启动回放时一次性复制当前缓冲快照，之后调度服务只读自己的快照，避免回放过程依赖 recorder 内部环形位置。

### 3.2 ReplayService

建议新增：

- `src/analyzer/replay_service.h`
- `src/analyzer/replay_service.cpp`
- `test/test_replay_service/test_replay_service.cpp`

`ReplayService` 负责回放状态机和时序调度，不解析 Web JSON，不直接接触 CAN driver。

建议接口语义：

```cpp
enum class ReplayTarget : uint8_t
{
    Original,
    ForceA,
    ForceB,
};

enum class ReplayStartResult : uint8_t
{
    Ok,
    Busy,
    RecorderUnavailable,
    RecordingActive,
    Empty,
    TooManyFrames,
};

enum class ReplayStopResult : uint8_t
{
    Ok,
    NotRunning,
};

enum class ReplayState : uint8_t
{
    Idle,
    Running,
    Completed,
    Stopped,
    Failed,
};

class ReplayService
{
public:
    void init(Recorder *recorder, TxService *tx_service, CapturedFrame *storage, size_t capacity);
    ReplayStartResult start(ReplayTarget target, uint32_t now_ms);
    ReplayStopResult stop();
    void tick(uint32_t now_ms);
    ReplayState state() const;
    size_t total() const;
    size_t sent() const;
    TxSendResult lastTxResult() const;
};
```

`start()` 行为：

1. 如果已有回放运行，返回 `Busy`。
2. 如果 recorder 不存在，返回 `RecorderUnavailable`。
3. 如果 recorder 正在录制，返回 `RecordingActive`。
4. 如果 recorder 缓冲为空，返回 `Empty`。
5. 如果服务快照 storage 不足，返回 `TooManyFrames`。
6. 调用 `recorder.collect()` 复制旧到新帧。
7. 初始化目标通道、索引、开始时间和状态。

`tick()` 行为：

1. 非 `Running` 状态直接返回。
2. 第一帧在启动后立即尝试发送。
3. 后续帧按录制时相邻 `ts_us` 差值调度。
4. 到期帧调用 `TxService::sendSingle(target_channel, id, dlc, data, now_ms)`。
5. 如果结果为 `Ok`，推进 `sent`。
6. 如果结果不是 `Ok`，状态置为 `Failed`，保存最近 `TxSendResult`，停止继续发送。
7. 所有帧发送成功后，状态置为 `Completed`。

调度时间使用 `uint32_t now_ms`，录制时间差从 `ts_us` 转为毫秒。小于 1ms 的正间隔按 1ms 处理，避免紧邻帧在同一个 tick 中无节制连续发送。最终发送频率仍受 `TxService` 的 10ms 全局限速约束。

### 3.3 与 analyzer_web 的关系

`analyzer_web` 只负责：

- 解析启动 / 停止请求。
- 把请求转为 pending command。
- 在 `processPendingCommand()` 中调用 `ReplayService::start()` / `stop()`。
- 在 `analyzerWebLoop()` 中调用 `ReplayService::tick(millis())`。
- 在 `/api/status` 输出回放状态。

HTTP/WS callback 不直接调用 `ReplayService::tick()`，也不直接调用 `TxService` 或底层 driver。

## 4. Web API 与状态

### 4.1 启动回放

新增：

```http
POST /api/replay/start
Content-Type: application/json
```

请求体：

```json
{
  "target": "original"
}
```

字段约束：

- `target`: `"original"`、`"A"` 或 `"B"`。

响应策略：

| 场景 | HTTP | 示例 |
|---|---:|---|
| 请求合法且已入队 | 200 | `{ "ok": true, "pending": true }` |
| JSON 或字段非法 | 400 | `{ "ok": false, "error": "bad_request" }` |
| pending command 队列已满 | 503 | `{ "ok": false, "error": "queue_full" }` |

启动后的真实结果由 `ReplayService::start()` 在 pending command 路径中产生，并反映到 `/api/status`。HTTP 200 只表示启动请求已提交，不表示回放已经开始或已经发帧。

### 4.2 停止回放

新增：

```http
POST /api/replay/stop
```

响应策略：

| 场景 | HTTP | 示例 |
|---|---:|---|
| 停止请求已入队 | 200 | `{ "ok": true, "pending": true }` |
| pending command 队列已满 | 503 | `{ "ok": false, "error": "queue_full" }` |

如果当前没有回放运行，pending 路径中的 stop 操作保持幂等，不作为前端错误展示。

### 4.3 `/api/status` 扩展

增加字段：

```json
{
  "replay_state": "idle",
  "replay_total": 0,
  "replay_sent": 0,
  "replay_error": ""
}
```

字段语义：

- `replay_state`: `"idle"`、`"running"`、`"completed"`、`"stopped"`、`"failed"`。
- `replay_total`: 当前回放快照总帧数。
- `replay_sent`: 已成功发送帧数。
- `replay_error`: 最近失败原因；无错误时为空字符串。

`replay_error` 可以使用 `TxService` 既有错误字符串，例如 `tx_disabled`、`rate_limited`、`driver_unavailable`。启动失败使用 replay 自己的错误字符串，例如 `recording_active`、`empty_recording`、`too_many_frames`。

## 5. 前端交互

在现有录制控制区附近新增“回放当前录制”区域。

控件：

- 目标通道：原通道 / CAN_A / CAN_B。
- 开始回放按钮。
- 停止回放按钮。
- 状态文本：显示 `idle/running/completed/stopped/failed`、进度和最近错误。

启动流程：

1. 前端读取目标通道。
2. 检查当前 status：录制中时禁用或提示先停止录制；`record_count == 0` 时禁用或提示无录制内容。
3. 弹出 `confirm()`，文案必须包含目标和风险，例如：`将按原始间隔回放当前录制缓冲到原通道。该操作会向 CAN 总线发送多帧，确认继续？`
4. 用户确认后 POST `/api/replay/start`。
5. 成功响应只显示“回放请求已提交”。
6. 刷新 `/api/status`，由状态字段显示真实启动、进度、完成或失败。

停止流程：

1. 运行中显示停止按钮。
2. 点击后 POST `/api/replay/stop`。
3. 成功响应只显示“停止请求已提交”。
4. 刷新状态并继续通过轮询显示最终 `stopped`。

按钮行为：

- `recording == true` 时禁用开始回放。
- `record_count == 0` 时禁用开始回放。
- `replay_state == "running"` 时禁用开始回放，启用停止。
- `replay_state != "running"` 时禁用停止。
- TX 横幅仍是发送安全状态的主要提示；回放 UI 不新增密码或 token。

## 6. 数据与错误边界

### 6.1 快照容量

`ReplayService` 使用启动时注入的 `CapturedFrame` storage 保存回放快照。容量建议与 recorder 容量一致，由 `src/can_analyzer.cpp` 在 PSRAM 分配。

如果 `recorder.count() > replay_capacity`，启动返回 `TooManyFrames` 并不发送任何帧。

### 6.2 时间间隔

回放使用相邻帧的采集时间差：

```text
due_ms[0] = start_ms
due_ms[i] = start_ms + max(1, (ts_us[i] - ts_us[i - 1]) / 1000) 的累计值
```

如果时间戳倒退或相等，则该间隔按 1ms 处理。这样能保证顺序推进，同时避免一个 tick 中无限发送。若原始间隔小于 `TxService` 的 10ms，实际发送可能因限速失败并停止，这是本阶段刻意保守的安全行为。

### 6.3 失败停止

以下情况会使运行中的回放进入 `Failed`：

- `TxService::sendSingle()` 返回非 `Ok`。
- 快照中出现非法 channel、ID 或 DLC，并被 `TxService` 拒绝。

失败后不自动重试，不跳过失败帧，不继续发送后续帧。

### 6.4 录制与回放互斥

启动回放时 recorder 必须处于 stopped 状态。回放运行期间如果用户启动录制，后端拒绝 `record_start`，避免录制与回放同时使用设备造成混淆。

## 7. 测试策略

实现阶段必须运行：

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_replay_service
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```
