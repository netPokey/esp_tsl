# CAN 分析仪 P5a：录制 / 导出设计规范

> 状态：设计已确认，待写实现计划
> 日期：2026-06-14
> 关联文档：`docs/superpowers/specs/2026-06-12-can-analyzer-design.md`（总设计，P5 路线图第 5 节）
> 阶段拆分：P5 拆为 **P5a 录制/导出（只读，本文）** + P5b 发送（写总线，另文）

## 1. 目标与范围

在已合并 main 的 P1–P4 之上，交付 P5 的**只读录制/导出**子系统：

- 手动 `start` / `stop` 录制 **A+B 全部原始帧**（不受前端过滤器约束）。
- 停止后以 **CSV** 格式**流式下载**到浏览器：边遍历边格式化边发，**不落 flash、不整体驻留内存**。
- 录制缓冲驻留 **PSRAM 环形缓冲**，满则丢最旧帧并计数（`dropped`），前端可见。

**明确不在 P5a 范围**（留待 P5b 或后续）：
- 任何发送 / 回放 / 扫描 / 序列（写总线）功能。
- ASC（Vector）格式导出 —— 首批仅 CSV，格式化器留扩展位。
- 条件触发录制 —— 首批仅手动 start/stop。

## 2. 架构与数据流

### 2.1 录制 tap 点

`drainQueueIntoTable()`（`src/analyzer/analyzer_web.cpp:600`）是 Core1 唯一的逐帧入口：每帧从 `frame_queue` pop 后，依次 push 给 `bus_stats` / `pretrigger` / `signal_window`，再 `id_table.update`。录制 tap 与之并列：

```cpp
while (g_queue->pop(cap))
{
    if (cap.id >= kStdIdCount) continue;
    if (g_stats) g_stats->noteRx(cap);
    if (g_pretrigger) g_pretrigger->push(cap);
    if (g_signals) g_signals->push(cap);
    if (g_recorder && g_recorder->active()) g_recorder->push(cap);  // P5a 新增
    g_table->update(cap);
    markDirty(cap.channel, cap.id);
}
```

- 录制全部帧（A+B），在 tap 点之前已过 `cap.id >= kStdIdCount` 的基本边界检查，与 id_table 一致。
- 录制是否激活由 `g_recorder->active()` 决定，未激活时零开销（一次原子读 + 短路）。

### 2.2 单元职责

| 单元 | 职责 | 可 native 测试 |
|---|---|---|
| `recorder.{h,cpp}` | 会话式录制器：PSRAM 环形缓冲存 `CapturedFrame`，start/stop/active/push/collect/计数 | 是（注入裸缓冲，不依赖 Arduino） |
| `record_format.{h,cpp}` | 纯函数 CSV 格式化器：表头 + 单帧行 + time 相对化 | 是（纯函数） |
| `analyzer_web.{cpp,h}` | record 命令、`GET /api/record/download` chunked、context 注入、drain tap、status 字段 | helper 经 `ForTest` 包装 |
| `can_analyzer.cpp` | PSRAM 分配 + init + 注入 | 否（设备入口） |

## 3. 数据模型

### 3.1 录制缓冲（PSRAM 环形）

复用既有 `CapturedFrame`（`analyzer_types.h`：`id/dlc/data[8]/channel/ts_us`），不新增帧结构。

- 容量：`kRecordCapacity = 100000` 帧。`sizeof(CapturedFrame)` ~24B（对齐后），约 **2.4MB PSRAM**。
- PSRAM 预算（8MB）：id_table 256KB + pretrigger 16384 帧 (~400KB) + 双 snapshot + signal window，余量充足容纳 2.4MB 录制缓冲。
- 满策略：环形覆盖最旧帧，`dropped_` 累加。宁可丢开头也不阻塞 drain（与队列背压一致）。

### 3.2 Recorder 接口

```cpp
class Recorder
{
public:
    void init(CapturedFrame *storage, size_t capacity);
    void start();                 // 清空 head/count/dropped，置 active
    void stop();                  // 仅清 active；缓冲内容保留供下载
    bool active() const;
    size_t count() const;         // 当前已存帧数（<= capacity）
    size_t capacity() const;
    uint32_t dropped() const;     // 本次会话被环形覆盖丢弃的帧数
    // 旧->新顺序遍历：返回写入 out 的帧数，支持分页（skip 跳过前 skip 帧）
    size_t collect(CapturedFrame *out, size_t cap, size_t skip) const;
private:
    CapturedFrame *storage_ = nullptr;
    size_t capacity_ = 0;
    size_t head_ = 0;             // 下一个写入位置
    size_t count_ = 0;
    uint32_t dropped_ = 0;
    bool active_ = false;
};
```

- `active_` 在 Core1（drain）写、HTTP 回调（Core1，Async 任务亦在 Core1）读；本架构 Web 与 drain 同核串行，按现有 P3/P4 模式无需额外锁，沿用 `analyzerWebLoop()` 串行处理。
- `collect(out, cap, skip)` 按旧→新返回帧，供 download 分页遍历；越界 skip 返回 0。

### 3.3 CSV 格式

`record_format.{h,cpp}` 纯函数：

- 表头：`time_s,channel,id,dlc,data`
- 单帧行：`time_s` = `(frame.ts_us - base_ts_us) / 1e6`，保留 6 位小数；`channel` = `A`/`B`；`id` = `0x` + 大写 3 位 hex；`dlc` = 十进制；`data` = `dlc` 个字节连续大写 hex（无分隔）。
- `base_ts_us` 取本次会话首帧 `ts_us`（由 download 端读 `collect` 首帧确定）。
- 接口示例：
  ```cpp
  size_t recordCsvHeader(char *out, size_t cap);
  size_t recordCsvLine(char *out, size_t cap, const CapturedFrame &f, uint64_t base_ts_us);
  ```
- 格式化器不感知 PSRAM/网络，单帧→单行，便于扩展 ASC（新增 `recordAscLine` 即可）。

## 4. 接口

### 4.1 WS 上行命令（JSON，低频）

```jsonc
{ "cmd": "record_start" }
{ "cmd": "record_stop" }
```

- 命令仅校验并切换 `recorder` 状态，沿用 P3/P4 的「WS callback 只校验并入队/置位，串行处理」模式。
- 缺失 recorder（PSRAM 分配失败）时命令 no-op + Serial log（与 pretrigger/snapshot 既有降级一致）。

### 4.2 HTTP 下载

`GET /api/record/download`：

- 用 `AsyncWebServerResponse *beginChunkedResponse("text/csv", cb)`，回调签名 `size_t cb(uint8_t *buf, size_t maxLen, size_t index)`。
- 回调内部维护「已发帧游标」：首次发表头；之后每次调用 `recorder->collect(tmp, n, cursor)` 取下一批帧，逐帧 `recordCsvLine` 写入 `buf`（受 `maxLen` 约束，不溢出），返回写入字节数；遍历完返回 0 结束。
- `base_ts_us` 在首批确定并缓存于回调状态。
- 响应头 `Content-Disposition: attachment; filename="can-record.csv"`。
- 下载期间录制应已 `stop`（前端约束）；若仍 active，按调用瞬间快照的 `count()` 为界，避免并发写读越界。

### 4.3 状态

`/api/status` JSON 新增字段：

```jsonc
{ "recording": false, "record_count": 0, "record_capacity": 100000, "record_dropped": 0 }
```

## 5. 前端（最小改动）

`data/analyzer/index.html` / `app.js` / `style.css`：

- 控制区新增一组：`录制开始` / `录制停止` 按钮、状态文本（`录制中 · N 帧 · dropped=M` / `空闲`）、`下载 CSV` 链接（`<a href="/api/record/download" download>`，非录制中且 count>0 时可用）。
- 按钮经 WS 发 `record_start` / `record_stop`；状态由 `/api/status` 轮询或现有 status 推送渲染。
- 不引入新 WS 二进制类型；下载走原生浏览器 GET（流式由浏览器落盘）。

## 6. 安全

P5a 纯只读：不切换 `setBusMode`、不调用任何 TX 路径、不触碰 `shouldAllowAnalyzerChannelTx`。无需二次确认。TX 横幅状态不受影响。

## 7. 测试

- `test/test_recorder/`：init/start 清零、push 累积、环形覆盖最旧 + dropped 计数、count 上限、collect 旧→新顺序、collect 分页 skip、stop 保留内容、未 init 安全。
- `test/test_record_format/`：表头、单帧行（time 相对化 6 位小数、id hex 3 位大写、data 按 dlc 截断 hex、channel A/B）、缓冲 cap 不溢出（小 cap 截断返回）。
- `test/test_ws_protocol/`（或 analyzer_web helper 测试）：record 命令解析、download 回调游标遍历分批正确、base_ts 取首帧。
- 回归：现有 89 native 测试保持通过。

## 8. 文件结构

- Create：`src/analyzer/recorder.h`、`src/analyzer/recorder.cpp`
- Create：`src/analyzer/record_format.h`、`src/analyzer/record_format.cpp`
- Modify：`src/analyzer/analyzer_web.cpp`、`src/analyzer/analyzer_web.h`（命令、路由、context 注入含 `Recorder*`、drain tap、status）
- Modify：`src/can_analyzer.cpp`（`ps_malloc` 录制缓冲、`g_recorder.init`、注入 `analyzerWebSetContext`）
- Modify：`data/analyzer/index.html`、`data/analyzer/app.js`、`data/analyzer/style.css`
- Create：`test/test_recorder/test_recorder.cpp`、`test/test_record_format/test_record_format.cpp`
- Modify：`platformio.ini`（在 `[env:native]` 的 `build_src_filter` 追加 `+<analyzer/recorder.cpp>` 与 `+<analyzer/record_format.cpp>`；`test/test_*` 目录由 PlatformIO 自动发现，无需额外注册）

## 9. 验收

- native：现有 89 + 新增 recorder/record_format 测试全通过。
- `pio run -e analyzer`：SUCCESS。
- `pio run -e analyzer -t buildfs`：SUCCESS，LittleFS 仅 `/app.js`、`/index.html`、`/style.css`。
- 台架（用户手动）：浏览器 record_start → 制造流量 → record_stop → 下载 CSV，校验 time/channel/id/data 正确、dropped 计数合理。
