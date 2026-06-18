# CAN 分析仪最小化设计

日期：2026-06-18
范围：将 `src/analyzer` 与 `data/analyzer` 精简为「只监听 + Web 浏览 CAN 消息」，保留 WiFi/AP 与电源控制。

## 目标

把当前功能繁多的 CAN 分析仪缩减为核心链路：

1. 通过 WebSocket 把 CAN 消息转发到浏览器。
2. Web 端浏览 A/B 两通道实时 CAN 表（含基础统计、筛选、排序）。
3. 保留 WiFi/AP 网络配置与电源控制（重启、关机深度睡眠）。

删除 TX 发送、录制、回放、快照差异、触发前回看、标签、信号工作台、常用信号等全部高级功能。

## 非目标

- 不新增任何功能。
- 不改动 CAN 驱动层（`drivers/`）与采集任务的核心采集逻辑。
- 不改 WiFi/AP 凭据与默认 AP（CAN-Analyzer / 1234567890）。

## 架构边界

### `src/can_analyzer.cpp`（组装点）

仅初始化保留链路所需对象：

- LittleFS
- CAN_A（MCP2515）、CAN_B（TWAI）驱动
- `FrameQueue`、`IdTable`、`BusStatsTracker`
- `rx_task`（Core0 采集）
- `analyzer_wifi`（WiFi/AP）
- `analyzer_web`（Web + WS + 电源端点）

移除以下对象的分配与初始化：`PretriggerBuffer`、`SnapshotStore`、`LabelStore`、`WatchedSignalWindow`、`CommonSignalStore`、`Recorder`、`ReplayService`、`TxService`、`TxModeSync`、`RecordTriggerService` 及其 PSRAM 缓冲。

### `src/analyzer/analyzer_web.*`（精简）

保留：

- 静态文件服务：`GET /`、`GET /app.js`、`GET /style.css`
- WebSocket `/ws`：定时消费 `FrameQueue`，更新 `IdTable` / `BusStatsTracker`，推送 frame delta + bus stats
- WiFi 端点：`GET /api/wifi/status`、`POST /api/wifi/connect`
- 电源端点：`POST /api/power/restart`、`POST /api/power/shutdown`（沿用 `PendingPowerAction` 延迟执行机制）

删除全部 TX、录制、回放、快照、标签、信号、常用信号、触发相关端点与命令处理。

### `src/analyzer/ws_protocol.*`（精简）

只保留：

- `WS_MSG_FRAME_DELTA`（`WsFrameRecord`）+ `wsBuildFrameDelta`
- `WS_MSG_BUS_STATS`（`WsBusStats`）+ `wsBuildBusStats`

删除 `WS_MSG_DIFF`、`WS_MSG_SIGNAL` 及对应子类型、结构体、builder（snapshot/pretrigger/baseline/signal samples/signal hints）。

### `data/analyzer/`（前端）

- `index.html`：保留连接状态、基础统计、筛选/排序、A/B 实时表、网络与电源面板；删除 TX 发送、录制/回放、触发、快照差异、触发前回看、信号工作台等 DOM。同步清理 `intro-panel` 中描述已删功能的文案，以及 `tx-banner`（TX 模式横幅）——若其仅服务 TX 功能则整体删除，不留空壳。
- `app.js`：保留 WS 解析（仅 frame/stats）、实时表渲染、筛选/排序、网络与电源面板交互；删除被移除功能对应代码与 DOM 引用。
- `style.css`：删除仅服务于已删 UI 的样式（可选清理）。

## 数据流与组件

接收路径（不变）：

1. Core0 `rx_task` 采集 CAN_A/CAN_B 帧 → `FrameQueue.push()`。
2. Core1 `analyzerWebLoop()` 定时 `FrameQueue.pop()` → 更新 `IdTable`（每 ID 最新数据 / 周期 / 抖动 / 活跃度）与 `BusStatsTracker`（fps / load / err / dropped）。

推送路径（精简）：

- 每约 66ms：扫描 dirty ID，构建 `WS_MSG_FRAME_DELTA` 批量推送。
- 每约 1000ms：构建 `WS_MSG_BUS_STATS` 推送。

HTTP 端点：

- `GET /`、`GET /app.js`、`GET /style.css`
- `GET /api/wifi/status`、`POST /api/wifi/connect`
- `POST /api/power/restart`、`POST /api/power/shutdown`

## 错误处理与边界

- PSRAM：`IdTable` 为硬依赖，分配失败保留现有 `while(true)` 提示死循环。其余被删模块的 PSRAM 分配代码一并移除。
- CAN 初始化失败：保留 `markAnalyzerChannelOnline` 标记，前端 bus-health 显示离线。
- WS 缓冲：沿用 `kPushBufBytes = 1400` 与批量上限逻辑，仅消息类型减少。
- 电源动作：沿用 `PendingPowerAction` 延迟执行，避免在 HTTP 回调内直接深睡 / 重启。
- `analyzerWebSetContext` 参数同步缩减为保留对象，避免悬空指针。

## 测试策略

保留（必要时精简）：

- `test_frame_queue`
- `test_bus_stats`
- `test_id_table`
- `test_ws_protocol`（仅 frame delta / bus stats builder 用例）

删除（对应已删模块）：

- `test_record_trigger`
- `test_tx_mode_sync`
- `test_signal_codec`
- `test_common_signal_store`
- `test_ws_protocol` 中 TX / replay / signal 相关用例

验证：

- `pio test -e native` 通过保留测试。
- `pio run` 固件编译通过。

## 待删除文件清单（参考）

后端：`tx_service.*`、`tx_mode_sync.*`、`replay_service.*`、`recorder.*`、`record_format.*`、`record_asc_format.*`、`record_trigger.*`、`pretrigger_buffer.*`、`snapshot_store.*`、`label_store.*`、`signal_window.*`、`signal_codec.*`、`signal_hints.*`、`common_signal_store.*`。

测试：`test_record_trigger`、`test_tx_mode_sync`、`test_signal_codec`、`test_common_signal_store` 等对应目录。

> 注：实现阶段会逐文件确认引用关系后再删除，避免遗漏保留链路的间接依赖。
