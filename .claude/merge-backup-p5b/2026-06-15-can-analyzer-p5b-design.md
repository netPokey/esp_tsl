# CAN 分析仪 P5b：安全发送入口与单帧发送设计规范

> 日期：2026-06-15  
> 状态：待用户审核  
> 关联前序：`docs/superpowers/specs/2026-06-14-can-analyzer-p5a-design.md`  
> 入口交接：`docs/superpowers/plans/2026-06-15-can-analyzer-handoff-p5b.md`

## 1. 目标与范围

P5b 进入 CAN 分析仪的写总线阶段。本阶段先建立所有发送功能共用的安全边界，再开放最小可验证的单帧发送能力。

本阶段只覆盖：

1. **P5b-1：后端统一发送服务**
   - 新增 `TxService`，作为所有写总线功能的唯一发送入口。
   - 统一检查 TX Master、分通道 TX enable、通道在线、参数合法性与全局限速。
   - 成功时调用对应 `CanDriver::send()`。

2. **P5b-2：单帧发送 API + Web UI**
   - 新增 `POST /api/tx/send`。
   - 前端新增单帧发送表单。
   - 发送前必须明确提示“将向通道 X 发送”并二次确认。
   - 顶部 TX 安全横幅继续常驻显示当前状态。

明确不在本阶段范围：

- 录制回放。
- CSV 上传回放。
- 周期 / 循环发送。
- 字节扫描 / bit 翻转扫描。
- 发送序列 / 脚本。
- token、密码、鉴权或物理确认流程。
- 可配置限速或持久化 TX 设置。
- ASC 导出或 P5a 录制格式扩展。

## 2. 安全原则

写总线功能默认必须失败关闭。任何发送请求只有在所有安全条件同时满足时才能真正调用底层 driver。

安全条件：

1. TX Master 必须 ON。
2. 目标通道的 TX enable 必须 ON。
3. 目标通道必须在线。
4. 目标通道必须有可用 driver。
5. channel 必须是 A 或 B。
6. 首批只接受标准帧 ID：`0x000..0x7FF`。
7. DLC 必须为 `0..8`。
8. data 长度必须能覆盖 DLC。
9. 任意两次成功发送之间必须间隔至少 10ms。

所有写总线功能必须集中走 `TxService`。Web API、后续回放、后续周期发送和后续扫描都不得直接调用 `CanDriver::send()`。

## 3. 架构

### 3.1 复用现有控制状态

现有 `src/analyzer/analyzer_control.h` 已提供：

- `isCanTxEnabled()`：TX Master。
- `isAnalyzerChannelTxEnabled(channel)`：分通道 TX enable。
- `isAnalyzerChannelOnline(channel)`：分通道在线状态。
- `shouldAllowAnalyzerChannelTx(channel)`：三者组合判断。

P5b 不新增另一套 TX 状态源，避免 UI 横幅、driver mode 与发送服务互相不一致。

### 3.2 TxService

建议新增：

- `src/analyzer/tx_service.h`
- `src/analyzer/tx_service.cpp`
- `test/test_tx_service/test_tx_service.cpp`

`TxService` 只负责“能不能发”和“发这一帧”。它不负责切换 bus mode、不解析 Web JSON、不管理 UI、不调度任务。

建议接口语义：

```cpp
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
};
```

`sendSingle()` 内部流程：

1. 校验 channel。
2. 选择 CAN_A / CAN_B driver。
3. 校验 driver 是否存在。
4. 调用 `shouldAllowAnalyzerChannelTx(channel)`。
5. 校验标准帧 ID、DLC 与 data 指针。
6. 检查全局 10ms 发送间隔。
7. 构造 `CanFrame`。
8. 调用 `driver->send(frame)`。
9. 只在发送成功路径更新最后发送时间。

### 3.3 与 bus mode 的关系

`src/can_analyzer.cpp` 现有 `syncTxMode()` 已按 `shouldAllowAnalyzerChannelTx(channel)` 将通道切换为 `Normal` 或 `ListenOnly`。P5b 不在 `TxService` 中调用 `setBusMode()`，避免发送请求绕过现有状态同步。

如果 TX 状态刚被 UI 打开，`syncTxMode()` 仍是设备主循环负责的模式同步点。`TxService` 只做最后一道安全检查。

## 4. HTTP API

新增：

```http
POST /api/tx/send
Content-Type: application/json
```

请求体：

```json
{
  "ch": "A",
  "id": 291,
  "dlc": 3,
  "data": [16, 17, 18]
}
```

字段约束：

- `ch`: `"A"` 或 `"B"`。
- `id`: 整数，范围 `0..2047`。前端可接受用户输入 `0x123`，但提交给 API 时转为整数。
- `dlc`: 整数，范围 `0..8`。
- `data`: 数组，元素为 `0..255`，长度至少为 `dlc`。后端只使用前 `dlc` 个字节。

响应策略：

| 场景 | HTTP | 示例 |
|---|---:|---|
| 成功发送 | 200 | `{ "ok": true }` |
| JSON 或字段非法 | 400 | `{ "ok": false, "error": "invalid_dlc" }` |
| TX Master / 分通道 TX / 通道在线条件不满足 | 409 | `{ "ok": false, "error": "tx_disabled" }` |
| 10ms 限速命中 | 429 | `{ "ok": false, "error": "rate_limited" }` |
| `TxService` 或 driver 不可用 | 503 | `{ "ok": false, "error": "driver_unavailable" }` |

`analyzer_web` 只解析请求、调用 `TxService`、映射错误码，不直接访问底层 driver。

## 5. 前端交互

在现有页面增加单帧发送控制区，建议放在 TX 控制区附近或录制控制区之前，保证用户能同时看到顶部 TX 横幅。

控件：

- 通道：A / B。
- ID：文本输入，支持十进制和 `0x` 十六进制。
- DLC：数字输入 `0..8`。
- Byte0..Byte7：十六进制字节输入。
- 发送按钮。
- 状态文本：显示最近一次发送成功或失败原因。

发送流程：

1. 前端读取表单并做基本校验。
2. 按 DLC 截取前 N 个 byte。
3. 弹出 `confirm()`，文案必须包含目标通道，例如：`将向通道 A 发送 ID 0x123，确认继续？`。
4. 用户确认后 POST `/api/tx/send`。
5. 根据 HTTP 结果显示状态文本。
6. 调用 `refreshTxBanner()`，让顶部 TX 状态保持同步。

按钮行为：

- 当 `txState.master` 为 false 时，按钮可以禁用或显示明确提示。
- 当目标通道 TX 未开启或离线时，按钮可以禁用或显示明确提示。
- 即使前端禁用逻辑漏掉，后端仍必须拒绝。

## 6. 数据与错误边界

### 6.1 ID 范围

P5b-1/P5b-2 首批只支持标准帧 ID。扩展帧支持后置，避免一次性扩大 UI、解析、CSV 回放和 driver 差异处理范围。

### 6.2 DLC 与 data

后端必须拒绝：

- DLC 小于 0 或大于 8。
- data 不是数组。
- data 数组长度小于 DLC。
- data 元素不是 `0..255` 整数。

DLC 为 0 时允许发送空数据帧。

### 6.3 限速

初始固定为全局最小间隔 10ms。限速只统计成功发送路径：被拒绝的请求不推进最后发送时间。

全局限速覆盖 A/B 两个通道，避免用户在两个通道之间交替快速点击绕过限制。

## 7. 测试策略

### 7.1 Native 单元测试

新增 `test/test_tx_service/test_tx_service.cpp`：

- TX Master OFF 时拒绝。
- 分通道 TX OFF 时拒绝。
- 通道 offline 时拒绝。
- channel 越界拒绝。
- driver 缺失拒绝。
- ID 超出 `0x7FF` 拒绝。
- DLC 超过 8 拒绝。
- DLC > 0 但 data 为空时拒绝。
- 安全条件满足时调用 fake driver。
- 10ms 内第二次成功发送请求被限速。
- 10ms 后允许再次发送。

Web 层 helper 测试可放入现有 `test/test_ws_protocol/test_ws_protocol.cpp` 或新增对应测试，覆盖：

- channel token 解析。
- ID 十进制 / 十六进制输入解析。
- byte 字段范围解析。
- `TxSendResult` 到 HTTP 状态与错误字符串映射。

### 7.2 构建验证

实现阶段必须运行：

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_tx_service
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

### 7.3 手动验收

1. 默认 TX 关闭时，单帧发送被拒绝，且设备不发帧。
2. Master ON 但目标通道 TX OFF 时，请求被拒绝。
3. 目标通道离线时，请求被拒绝。
4. 非法 ID、DLC、byte 输入被前端或后端拒绝。
5. 连续快速点击发送时，第二次命中 10ms 限速。
6. Master ON、目标通道 TX ON、通道在线时，单帧发送成功。
7. 顶部 TX 横幅与 `/api/status` 保持一致。

## 8. 实施拆分建议

后续 implementation plan 建议拆为：

1. `TxService` 测试与实现。
2. Web 解析 helper 与错误映射测试。
3. `/api/tx/send` 接线与 `TxService` 注入。
4. 前端单帧发送 UI。
5. 完整验证与 code review。

## 9. 风险与约束

- 写总线功能安全优先，宁可误拒绝，也不能在状态不明时发送。
- `TxService` 不改变 bus mode，避免与现有 `syncTxMode()` 形成第二套状态机。
- 前端禁用按钮只是用户体验，后端检查才是安全边界。
- 可信局域网假设不等于开放公网可用；如果未来要接入不可信网络，应单独设计 token、密码或物理确认流程。
- 本阶段固定 10ms 限速，后续若需要更快发送，应在有测试与 UI 风险提示后单独扩展。
