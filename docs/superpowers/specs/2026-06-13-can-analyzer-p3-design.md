# CAN 分析仪 P3 对比找包设计规范

> 日期：2026-06-13  
> 状态：待用户审核  
> 关联总设计：`docs/superpowers/specs/2026-06-12-can-analyzer-design.md`

## 1. 目标与范围

P3 在 P2 双通道实时表、时间统计、活跃度和前端高亮基础上，实现逆向找包的主工作流：

1. 基线采集与一键拉黑：静止状态采集当前出现过的 ID，前端一键加入黑名单并隐藏。
2. 快照 Diff：拍摄快照 A/B，对比新增、消失、数据变化的 ID。
3. Pre-trigger 回看：常驻保存最近 5 秒原始帧，用户点击「标记」后回看触发前窗口内的 ID 活动。
4. 黑/白名单、ID/范围/通道过滤、搜索：全部在前端即时生效。
5. ID 标注：最多 64 条 `(channel, id) -> label`，持久化到 NVS。

P3 不做 P4 的信号曲线/mux 解码，也不做 P5 的录制、回放、扫描或主动发送。

## 2. 架构原则

P3 继续沿用 P1/P2 的双核分工：Core0 只负责采集、打时间戳、入 SPSC 队列；Core1 在 `analyzerWebLoop()` 中排空队列、更新 `IdTable`、维护 P3 状态并推送 WebSocket。P3 不向 Core0 增加任何分支、过滤、字符串或额外队列。

所有新增分析状态由 Core1 单线程拥有，因此不需要互斥锁：

- `IdTable`：继续作为当前实时表真相源。
- `PretriggerBuffer`：Core1 写入每个从队列弹出的 `CapturedFrame`。
- `SnapshotStore`：Core1 从 `IdTable` 复制 A/B 快照并计算 Diff。
- `LabelStore`：Core1 响应前端 JSON 命令，维护 64 条标注并写入 NVS。

前端负责视图过滤和交互状态：黑名单、白名单、范围过滤、搜索、通道过滤、pinned 均不阻断后端推送；它们只影响 DOM 显示和 Diff/Pre-trigger 结果区的呈现。

## 3. 后端组件

### 3.1 `PretriggerBuffer`

新增 `src/analyzer/pretrigger_buffer.h/.cpp`，保存最近 5 秒原始 `CapturedFrame`。

推荐容量：`kPretriggerCapacity = 16384`。按 `CapturedFrame` 约 24 字节估算，占用约 384KB，放 PSRAM。该容量覆盖双通道约 3000 fps 的 5 秒窗口，超出时覆盖最旧帧。

接口：

```cpp
class PretriggerBuffer {
public:
    void init(CapturedFrame *storage, size_t capacity);
    void push(const CapturedFrame &frame);
    size_t collect(uint64_t now_us, uint32_t window_us, CapturedFrame *out, size_t cap) const;
};
```

`collect()` 返回时间戳在 `[now_us - window_us, now_us]` 内的帧，按时间升序写入输出。P3 初版不把全部原始帧推给前端，而是在后端汇总成每个 `(channel,id)` 的活动摘要：首次时间、末次时间、帧数、变化次数、最后数据。

### 3.2 `SnapshotStore`

新增 `src/analyzer/snapshot_store.h/.cpp`。两个槽 `A/B`，每槽保存 `SnapshotRecord[2][2048]`：

```cpp
struct SnapshotRecord {
    bool present;
    uint8_t dlc;
    uint8_t data[8];
};
```

接口：

```cpp
enum class SnapshotSlot : uint8_t { A = 0, B = 1 };

class SnapshotStore {
public:
    void init(SnapshotRecord *slotA, SnapshotRecord *slotB);
    void capture(SnapshotSlot slot, const IdTable &table);
    size_t diff(SnapshotDiffRecord *out, size_t cap) const;
};
```

Diff 语义固定为当前值对比：

- `added`：B present、A not present。
- `removed`：A present、B not present。
- `changed`：A/B 都 present，但 `dlc` 或 `data[0..7]` 不同。

Diff 不使用 rx_count/change_score，因此适合「操作前 vs 操作后」的稳定状态对比。瞬态按键由 Pre-trigger 解决。

### 3.3 `LabelStore`

新增 `src/analyzer/label_store.h/.cpp`，最多 64 条标注，存 NVS 命名空间 `analyzer`、key `labels`。

```cpp
struct LabelEntry {
    uint8_t channel;
    uint16_t id;
    char text[24];
};
```

接口：

```cpp
class LabelStore {
public:
    void begin();
    bool upsert(uint8_t channel, uint16_t id, const char *text);
    bool remove(uint8_t channel, uint16_t id);
    const LabelEntry *entries() const;
    size_t count() const;
};
```

`upsert()` 对空字符串等同删除。超出 64 条时拒绝新增并通过 WebSocket/JSON 状态返回错误。

## 4. WebSocket 与 JSON 控制

### 4.1 下行二进制消息

沿用 `WS_MSG_DIFF = 0x03`，作为 P3 事件消息类型。首字节 type，第二字节 subtype：

```cpp
enum WsDiffSubtype : uint8_t {
    WS_DIFF_SNAPSHOT = 0x01,
    WS_DIFF_PRETRIGGER = 0x02,
    WS_DIFF_BASELINE = 0x03,
    WS_DIFF_LABELS = 0x04,
};
```

#### Snapshot Diff payload

Header：`type(1), subtype(1), count(1)`，后接 `WsDiffRecord[count]`：

```cpp
struct WsDiffRecord {
    uint8_t channel;
    uint16_t id;
    uint8_t kind;       // 1 added, 2 removed, 3 changed
    uint8_t dlc_a;
    uint8_t data_a[8];
    uint8_t dlc_b;
    uint8_t data_b[8];
};
```

#### Pre-trigger payload

Header：`type(1), subtype(1), count(1)`，后接 `WsPretriggerRecord[count]`：

```cpp
struct WsPretriggerRecord {
    uint8_t channel;
    uint16_t id;
    uint16_t first_seen_ms_ago;
    uint16_t last_seen_ms_ago;
    uint16_t frames;
    uint16_t changes;
    uint8_t dlc;
    uint8_t data[8];
};
```

该摘要足以定位触发前窗口内突然活动的 ID。若后续 P5 需要原始时间线，再新增流式导出，不在 P3 内做。

#### Baseline payload

Header：`type(1), subtype(1), count(1)`，后接 `(channel,id)` 列表。前端收到后把这些 ID 加入黑名单。

#### Labels payload

为避免二进制变长字符串复杂度，labels 下行不使用 0x03 二进制 payload，而由 `/api/labels` HTTP GET 返回 JSON；WebSocket 只用于通知前端 labels 已更新（`WS_DIFF_LABELS`，count=0）。

### 4.2 上行 JSON 命令

新增命令：

```jsonc
{ "cmd": "snapshot", "slot": "A" }
{ "cmd": "diff" }
{ "cmd": "baseline" }
{ "cmd": "mark" }
{ "cmd": "label_set", "ch": "A", "id": 306, "text": "volume+" }
{ "cmd": "label_delete", "ch": "A", "id": 306 }
```

HTTP 辅助接口：

- `GET /api/labels`：返回当前 64 条标注。

所有命令仍复用现有 WebSocket JSON 通道。P3 不新增鉴权；TX 鉴权风险保留到后续安全专项或 P5 前处理。

## 5. 前端 UI

P3 在现有 P2 工具栏下新增「找包」面板：

- 基线：`采集基线/一键拉黑当前出现 ID`，收到 baseline 列表后加入黑名单。
- 快照：`拍 A`、`拍 B`、`Diff`，结果区分 Added / Removed / Changed。
- Pre-trigger：`标记/回看最近 5 秒`，结果按 changes、frames、last_seen 排序。
- 过滤：通道 A/B/全部、ID 输入、ID 范围、搜索框、黑名单隐藏、白名单只看。
- 标注：行内编辑 label；label 显示在 ID 旁，保存到 NVS。

前端数据结构：

```js
const filterState = {
  hidden: new Set(),
  whitelist: new Set(),
  pinned: new Set(),
  channel: 'all',
  idText: '',
  rangeFrom: '',
  rangeTo: '',
  search: ''
};

const labels = new Map(); // key: `${ch}:${id}`
```

过滤只改变显示，不影响 `records` 内的最新数据，也不影响 WebSocket 连接。

## 6. 测试策略

Native 单元测试：

- `test_pretrigger_buffer`：容量覆盖、时间窗口收集、顺序正确、超过 5 秒不返回。
- `test_snapshot_store`：捕获 A/B、added/removed/changed 分类正确、无变化不输出。
- `test_label_store`：upsert、覆盖、删除、容量上限；NVS 在 native 下用轻量内存后端或把持久化层拆成可注入接口。
- `test_ws_protocol`：0x03 subtype 布局、buffer cap 截断、字段小端保持。

集成验证：

- `pio test -e native`
- `pio run -e analyzer`
- `pio run -e analyzer -t buildfs`

手动台架验收：

1. 打开 Web UI，确认 P2 表格仍实时更新。
2. 静止总线采集基线，一键拉黑后主表隐藏已有 ID。
3. 操作前拍 A、操作后拍 B，Diff 显示新增/变化 ID。
4. 点击标记，Pre-trigger 结果显示最近 5 秒内活动 ID。
5. 给某 ID 设置 label，刷新页面后 label 仍存在。

## 7. 风险与约束

- Pre-trigger 默认 16384 帧约 384KB，需从 PSRAM 分配；分配失败时禁用该功能并在状态区提示。
- 0x03 结果消息需要分批发送，单包仍遵守 1400 字节左右推送缓冲限制。
- 前端过滤全在浏览器执行，因此黑名单不会减少设备到浏览器的带宽；这是为降低后端状态复杂度的明确取舍。
- NVS 写入仅在 label 改动时发生，不能在每帧或高频 UI 状态变化时写 flash。
- macOS `/Volumes` 挂载盘会生成 `._*` AppleDouble 文件，构建/测试前必须清理，否则 PlatformIO 可能把它们当源码或 LittleFS 文件。
