# ESP32-S3 双通道 CAN 报文分析仪 —— 分析层设计规范

> 状态：设计已确认，待写实现计划
> 日期：2026-06-12
> 关联文档：`Can分析功能开发计划.md`（功能需求来源）

## 1. 目标与范围

在已测好的双通道（A/B）CAN 收发驱动之上，构建一个**独立的通用 CAN 报文分析仪固件**，覆盖开发计划的 P1–P5 全部分析层 / Web 控制 / 安全机制功能。

- **整体架构一次设计到位，实现按 P1→P5 阶段交付**，每阶段可独立编译 + 台架测试。
- 新固件与现有特斯拉业务 `main.cpp` **完全解耦**：各自独立 PlatformIO env，互不编译。
- 只复用底层**驱动层**（`CanDriver` 抽象 / `MCP2515Driver` / `TWAIDriver`）、`CanFrame`、`pin_config.h`、TX 安全闸；分析层、Web 层、前端 UI 全部新建。

### 已锁定的架构决策

| 决策点 | 选择 | 理由 |
|---|---|---|
| 框架 | Arduino + PlatformIO（ESP32-S3，16MB flash + PSRAM） | 与现有项目一致 |
| Web 通信 | ESPAsyncWebServer + WebSocket 二进制帧 | 高负载不丢帧、实时高亮/波形依赖它 |
| 并发模型 | 双核：Core0 采集、Core1 分析+Web | 不丢帧、UI 不卡 |
| 帧类型 | 仅标准帧（11-bit），定长数组表 | 实测 348 个唯一 ID 全 ≤0x7FF，零扩展帧 |
| 录制存储 | 流式下载到浏览器（无 SD/不落 flash） | 设备无 SD 卡 |
| 代码复用 | 干净独立，仅复用驱动层 | 通用分析仪不掺业务逻辑 |
| 前端交付 | LittleFS 文件系统托管 | UI 体量大、改前端不重编固件 |

### 数据依据（来自实测抓包）

- `data/can_batches.ndjson`：258 批，348 个唯一 ID，**最大 ID = 2047（0x7FF），扩展帧数量 = 0**。
- `TESLA_CAN_BATTERY_REFERENCE.md` / `TESLA_CAN_STEERING_REFERENCE.md`：全部为 11-bit 标准 ID。
- 存在**多路复用（mux）报文**（如 `0x401` 用 byte0 当 mux 索引、`0x332` 按 mux=0/1 切字段），P4 信号解码需支持按 mux 分组。

## 2. 总体架构

### 2.1 固件定位

独立的通用双通道 CAN 报文分析仪固件，只复用已测试通过的驱动层（`CanDriver` / `MCP2515Driver` / `TWAIDriver`）、`CanFrame`、`pin_config.h` 与全局 TX 安全闸（`setCanTxEnabled` / `isCanTxEnabled` / `shouldAllowCanTx`）。

- 新增 PlatformIO 环境 `[env:analyzer]`，入口 `src/can_analyzer.cpp`。
- 与现有 `main.cpp`（特斯拉业务固件）互不编译，通过 `build_src_filter` 隔离。
- 分析层、Web 层、前端 UI 全部新写，**不复用** `web/web_server.h`（同步 WebServer + 绑死特斯拉 handler，与新架构冲突）。

### 2.2 双核分工

不丢帧、UI 不卡的关键是把"实时采集"和"分析 + Web"分到两个核心：

```
Core 0 (采集)                      Core 1 (分析 + Web)
─────────────                     ────────────────────
轮询 MCP2515(A) / TWAI(B)           从队列排空取帧
  ↓                                 ↓
esp_timer 打微秒时间戳              更新唯一 ID 表 (PSRAM)
  ↓                                 ↓ 过滤 / 静态抑制 / 周期估算 / 活跃度
压入无锁 SPSC 环形队列  ─────────→  ↓
(绝不做字符串 / Web 处理)            ESPAsyncWebServer
                                    ├─ WS 二进制增量推送 @10–20Hz
                                    └─ JSON 控制指令(发送/黑名单/配置)
```

- **Core 0**：一个 pin 到核心 0 的 FreeRTOS 任务，职责只有「读帧 → 打时间戳 → 入队」。绝不在此做字符串或 Web 处理。
- **Core 1**：Arduino `loop()`（`ARDUINO_RUNNING_CORE=1`）+ Async server 任务，负责排空队列、更新表、跑统计、WS 推送。
- **背压策略**：队列满时丢最旧帧并累加 `dropped` 计数，前端可见。宁可丢帧也绝不阻塞 Core 0 的采集。

### 2.3 文件结构

每个单元单一职责、可独立理解与测试：

```
src/can_analyzer.cpp            入口：初始化 + 接线，无业务逻辑
src/analyzer/
  frame_queue.{h,cpp}           无锁 SPSC 环形队列
  rx_task.{h,cpp}               Core0 采集任务
  id_table.{h,cpp}              唯一 ID 表(PSRAM) + 统计
  analysis_engine.{h,cpp}       过滤 / 静态抑制 / 周期 / 活跃度
  pretrigger_buffer.{h,cpp}     P3 触发前环形缓冲
  recorder.{h,cpp}              P5 csv/asc 流式生成
  sender.{h,cpp}                P5 发送 / 周期 / 扫描 / 序列(受 TX 闸约束)
  ws_protocol.h                 WS 二进制帧布局(前后端共用定义)
  analyzer_web.{h,cpp}          ESPAsyncWebServer + WS + 路由
  analyzer_wifi.{h,cpp}         STA/AP 回退(沿用现有模式)
data/                           前端(LittleFS)：index.html / app.js / style.css
```

### 2.4 新增依赖

- `ESPAsyncWebServer`（自带 WebSocket）
- `AsyncTCP`（ESPAsyncWebServer 在 ESP32 上的依赖）

P1 即引入。前端经 LittleFS 托管（`board_build.filesystem = littlefs`，单独 `Upload Filesystem Image`）。

## 3. 数据模型

### 3.1 捕获帧（入队载体）

底层 `CanFrame` 仅有 `{id, dlc, data[8]}`，无时间戳、无通道。Core0 入队时包一层元数据，**不改动底层驱动**：

```cpp
struct CapturedFrame {
    uint32_t id;          // 标准 11-bit ID
    uint8_t  dlc;
    uint8_t  data[8];
    uint8_t  channel;     // 0 = A, 1 = B
    uint64_t ts_us;       // esp_timer_get_time() 微秒时间戳
};
```

### 3.2 唯一 ID 表（常驻 PSRAM）

`IdRecord table[2][2048]`（A/B 两通道 × 标准 ID 全集），按 `(channel, id)` 直接索引，O(1) 访问。

| 字段 | 类型 | 用途 |
|---|---|---|
| `present` | bool | 该 (通道,ID) 是否出现过 |
| `dlc` / `data[8]` | u8 / u8[8] | 当前帧 |
| `byte_change_ts[8]` | u32[8] | 每字节最后变化时刻（字节高亮淡出） |
| `last_rx_ts` / `prev_rx_ts` | u32 ×2 | Delta Time |
| `rx_count` | u32 | 累计帧数 / 活跃度 |
| `period_est` | u32 | 滑动平均周期（μs） |
| `change_score` | u16 | 近 N 秒变化次数（活跃度热力） |
| `flags` | u8 位域 | static / blacklist / whitelist / pinned |

每条 ~64 字节 × 4096 = **~256KB**，放 PSRAM 无压力。

**关键简化**：
- 不存 `prev[8]`：新帧直接与 `data[8]` 比对来更新 `byte_change_ts`，再覆盖。
- 不存 `bit_change_ts[64]`：**位级高亮由前端算**——后端只推 `byte_change_ts`，前端按当前时间渲染颜色衰减。省内存、省带宽，正合计划第 2 节。
- `label` 别名**单独存**（少量被标注，约 64 条），持久化到 NVS，不撑大主表。

## 4. WebSocket 协议

`ws_protocol.h` 由前后端共用同一套结构体 / 常量定义，避免对齐错位。所有多字节字段统一小端。

### 4.1 下行（设备 → 浏览器，二进制定长）

| Type | 名称 | 内容 | 频率 |
|---|---|---|---|
| `0x01` | 帧增量 | 本周期变化的 IdRecord 列表（定长结构体数组） | 10–20Hz |
| `0x02` | 总线统计 | 每通道 fps / 负载% / TWAI 错误计数 / bus-off 状态 | 1–2Hz |
| `0x03` | Diff/触发 | 快照 Diff 结果、Pre-trigger 回看数据 | 事件驱动 |

每个二进制帧首字节为 Type，后接对应定长 payload。

### 4.2 上行（浏览器 → 设备，小 JSON，低频）

控制指令一律走 JSON，便于扩展、可读性好：

```jsonc
{ "cmd": "blacklist",   "ch": "A", "id": 306 }
{ "cmd": "tx_master",   "on": true }          // 发送总开关
{ "cmd": "tx_enable",   "ch": "A", "on": true }
{ "cmd": "send_frame",  "ch": "B", "id": 306, "data": "..." }
{ "cmd": "scan",        "ch": "B", "id": 306, "byte": 3, "from": 0, "to": 255 }
{ "cmd": "snapshot",    "slot": "A" }          // 拍快照供 Diff
{ "cmd": "record_start" } / { "cmd": "record_stop" }
```

## 5. 分阶段路线图

整体架构一次定好，**实现按阶段交付**。每阶段可独立编译 + 台架测试。

> **台架技巧（无车）**：把 CAN-A 的 TX 接到 CAN-B 总线（或自收/loopback），一路发一路收，造出双通道流量，验完每个阶段。

| 阶段 | 交付内容 | 台架验收 |
|---|---|---|
| **P1 骨架** | 双核 + 无锁队列 + RX 任务；唯一 ID 表去重；ESPAsyncWebServer + WS 二进制帧增量推送；前端 A/B 分区实时表；TX 总开关（默认 OFF/监听-only）+ 红/绿状态横幅；STA/AP 回退 | 浏览器看到 A/B 实时帧、去重正确、横幅显示监听-only |
| **P2 高亮+抑制+时间** | 字节级高亮 + 前端淡出；位级展开视图；多进制（HEX/DEC/BIN/ASCII）；静态抑制（可设阈值）；Delta Time；周期 + 抖动；总线负载/fps/错误计数；冻结/暂停；活跃度热力 + 排序 | 制造变化能高亮、静止帧隐藏、间隔/负载正确 |
| **P3 对比找包** | 基线采集 → 一键拉黑；快照 Diff（新增/消失/变化）；Pre-trigger 环形缓冲（最近 N 秒回看）；黑/白名单；ID 标注 + NVS 持久化；按 ID/范围/通道过滤 + 搜索 | 模拟操作前后，diff 定位到变化 ID；回看触发前几秒 |
| **P4 波形+信号** | 字节/16 位曲线（大小端、有无符号）；多路复用（mux）字段解码（数据有 0x401/0x332）；滚动计数器 + 校验和自动识别 | 喂递增数据画出斜线、标出计数器 |
| **P5 录制+发送** | csv/asc 流式下载到浏览器；触发录制；按原始时序回放（可重定向 A/B）；单帧发送；周期/循环发送；字节/位扫描；发送序列脚本 | 录一段 → 回放，环回（A 发 B 收）验证收发一致 |

## 6. 安全设计（贯穿全程）

这台设备能往车上发报文，安全前置（计划第 4 节）：

1. **默认监听-only**：上电不发任何东西（含不发 ACK），`setBusMode(ListenOnly)`。
2. **发送总开关默认 OFF**，且**分通道独立使能**（A 能发不代表 B 能发）。
3. **回放 / 扫描 / 周期发送前二次确认**，并显示"即将向 通道X 发送"。
4. **发送限速**，防手滑刷爆总线。
5. UI 顶部常驻**红/绿发送状态横幅**（红=可发送 / 绿=只监听），杜绝"以为没在发其实在发"。

复用现有全局 TX 闸 `setCanTxEnabled` / `isCanTxEnabled` / `shouldAllowCanTx`，运行时按总开关把驱动在 `Normal` / `ListenOnly` 间切换。

## 7. 双通道 A/B 贯穿原则

A/B 在**显示、过滤、统计、导出、发送、回放**每一处都可区分、可独立操作：

- 显示：分区 + 通道色标/标签；可单独只看 A / 只看 B / 合并看。
- 统计：负载、fps、错误计数各自独立。
- 导出：每条记录带 channel 字段。
- 发送/回放：明确指定目标通道，受该通道独立 TX 使能约束。

## 8. PlatformIO 环境配置

```ini
[env:analyzer]
board_build.filesystem = littlefs
build_src_filter =
    +<can_analyzer.cpp>
    +<analyzer/>
    -<main.cpp>
    -<can.ino>
    -<can.ino.cpp>
    -<can_b_replay.cpp>
lib_deps =
    ${env.lib_deps}
    ESP32Async/ESPAsyncWebServer
    ESP32Async/AsyncTCP
```
