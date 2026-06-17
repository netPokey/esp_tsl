# CAN 分析仪 P1 骨架 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在已测好的驱动层之上，搭出独立的双通道 CAN 分析仪固件骨架：双核采集（Core0）+ 无锁队列 + 唯一 ID 表去重 + ESPAsyncWebServer/WebSocket 二进制增量推送 + 前端 A/B 实时表 + TX 总开关（默认监听-only）+ STA/AP 回退。

**Architecture:** Core0 一个 FreeRTOS 任务轮询 MCP2515(A)/TWAI(B)，打微秒时间戳后压入无锁 SPSC 环形队列；Core1（Arduino loop）排空队列、更新 PSRAM 中的唯一 ID 表、以 10–20Hz 通过 WebSocket 二进制帧推送变化记录给浏览器。纯逻辑单元（队列/表/协议）在宿主 `native` 环境用 Unity 做 TDD，硬件相关单元（RX 任务/Web/WiFi/入口）靠台架验证编译与运行。

**Tech Stack:** Arduino + PlatformIO（ESP32-S3，16MB flash，PSRAM）、ESPAsyncWebServer + AsyncTCP、LittleFS、FreeRTOS、Unity（native 单测）。

**关联 spec:** `docs/superpowers/specs/2026-06-12-can-analyzer-design.md`

**复用的既有接口（不改动）:**
- `CanFrame { uint32_t id; uint8_t dlc; uint8_t data[8]; }` — `include/can_frame_types.h`
- `CanBusId { A, B, Unknown }` — `include/runtime_state.h`
- `CanDriver`（`init/setFilters/setBusMode/enableInterrupt/read/send`）— `include/drivers/can_driver.h`
- `MCP2515Driver(cs,rst,sck,miso,mosi,spi,clk)` — `include/drivers/mcp2515_driver.h`
- `TWAIDriver(gpio txPin, gpio rxPin)` — `include/drivers/twai_driver.h`
- TX 安全闸 `setCanTxEnabled/isCanTxEnabled`（`shouldAllowCanTx` 见 can_helpers）— `include/can_helpers.h`
- 引脚：CAN_TX=7, CAN_RX=6, MCP2515 CS=10/RST=9/INT=8, SPI 用默认 `&SPI` — `libraries/private_library/pin_config.h`
- `Shared<T>`：`NATIVE_BUILD` 下退化为普通值，设备下为 `std::atomic` — `include/shared_types.h`

---

## 文件结构（P1 范围）

| 文件 | 职责 | 可宿主测试 |
|---|---|---|
| `src/analyzer/analyzer_types.h` | `CapturedFrame` 入队载体定义 | — |
| `src/analyzer/frame_queue.{h,cpp}` | 无锁 SPSC 环形队列 | ✅ native |
| `src/analyzer/id_table.{h,cpp}` | 唯一 ID 表 `[2][2048]` + 去重/记账 | ✅ native |
| `src/analyzer/ws_protocol.{h,cpp}` | WS 二进制帧布局 + 序列化 | ✅ native |
| `src/analyzer/rx_task.{h,cpp}` | Core0 采集任务（读帧→打时间戳→入队） | 台架 |
| `src/analyzer/analyzer_wifi.{h,cpp}` | STA/AP 回退 | 台架 |
| `src/analyzer/analyzer_web.{h,cpp}` | ESPAsyncWebServer + WS + 控制路由 | 台架 |
| `src/can_analyzer.cpp` | 入口：初始化 + 接线 | 台架 |
| `data/index.html` / `data/app.js` / `data/style.css` | 前端 A/B 实时表 + TX 横幅 | 浏览器 |
| `test/test_frame_queue/*` `test/test_id_table/*` `test/test_ws_protocol/*` | Unity 单测 | ✅ native |

**测试策略：** 纯逻辑单元（队列/表/协议）在宿主 `native` 环境用 Unity 做 TDD（红→绿→提交）。硬件相关单元（RX 任务/Web/WiFi/入口）无法在宿主跑，以「`analyzer` 环境编译通过 + 台架运行」作为验收。

**背压说明（设计决定）：** SPSC 队列满时**丢弃最新帧**并累加 `dropped` 计数（而非丢最旧——后者需生产者改动消费者的 tail，破坏无锁正确性）。无论丢哪端，丢帧数都会上报前端，交付效果一致。

---

## Task 0：PlatformIO 环境（native 单测 + analyzer 固件）

**Files:**
- Modify: `platformio.ini`（追加 `[env:native]` 与 `[env:analyzer]`）

- [ ] **Step 1: 追加 native 与 analyzer 环境**

在 `platformio.ini` 末尾追加：

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

[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -D NATIVE_BUILD
    -I include
    -I src
build_src_filter =
    +<analyzer/frame_queue.cpp>
    +<analyzer/id_table.cpp>
    +<analyzer/ws_protocol.cpp>
```

- [ ] **Step 2: 验证现有环境未被破坏**

Run: `pio project config`
Expected: 列出含 `env:native` 和 `env:analyzer` 在内的全部环境，无报错。

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "build: add native test env and analyzer firmware env"
```

---

## Task 1：CapturedFrame 类型 + frame_queue（无锁 SPSC 队列，TDD）

**Files:**
- Create: `src/analyzer/analyzer_types.h`
- Create: `src/analyzer/frame_queue.h`
- Create: `src/analyzer/frame_queue.cpp`
- Test: `test/test_frame_queue/test_frame_queue.cpp`

- [ ] **Step 1: 写 CapturedFrame 类型**

`src/analyzer/analyzer_types.h`：

```cpp
#pragma once
#include <cstdint>

// Core0 采集任务入队的载体：在底层 CanFrame 之上补通道与微秒时间戳。
// 不改动底层驱动的 CanFrame，保持驱动层零侵入。
struct CapturedFrame
{
    uint32_t id = 0;        // 标准 11-bit ID
    uint8_t  dlc = 0;       // 0..8
    uint8_t  data[8] = {};
    uint8_t  channel = 0;   // 0 = A, 1 = B
    uint64_t ts_us = 0;     // esp_timer_get_time() 微秒时间戳
};
```

- [ ] **Step 2: 写失败的测试**

`test/test_frame_queue/test_frame_queue.cpp`：

```cpp
#include <unity.h>
#include "analyzer/frame_queue.h"

static FrameQueue makeQueue(uint16_t cap, CapturedFrame *buf)
{
    FrameQueue q;
    q.init(buf, cap);
    return q;
}

void test_empty_pop_returns_false()
{
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame out;
    TEST_ASSERT_FALSE(q.pop(out));
}

void test_push_then_pop_roundtrip()
{
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame in;
    in.id = 0x132; in.dlc = 8; in.channel = 1; in.ts_us = 12345;
    in.data[0] = 0xAB;
    TEST_ASSERT_TRUE(q.push(in));
    CapturedFrame out;
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_EQUAL_UINT32(0x132, out.id);
    TEST_ASSERT_EQUAL_UINT8(8, out.dlc);
    TEST_ASSERT_EQUAL_UINT8(1, out.channel);
    TEST_ASSERT_EQUAL_UINT64(12345, out.ts_us);
    TEST_ASSERT_EQUAL_UINT8(0xAB, out.data[0]);
}

void test_full_queue_drops_newest_and_counts()
{
    // 容量 cap，实际可用 cap-1（留一格区分空/满）。
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame f;
    TEST_ASSERT_TRUE(q.push(f));
    TEST_ASSERT_TRUE(q.push(f));
    TEST_ASSERT_TRUE(q.push(f));
    TEST_ASSERT_FALSE(q.push(f));          // 第 4 次：满，丢弃
    TEST_ASSERT_EQUAL_UINT32(1, q.dropped());
}

void test_wraparound()
{
    CapturedFrame buf[4];
    FrameQueue q = makeQueue(4, buf);
    CapturedFrame f, out;
    for (int round = 0; round < 10; ++round)
    {
        f.id = round;
        TEST_ASSERT_TRUE(q.push(f));
        TEST_ASSERT_TRUE(q.pop(out));
        TEST_ASSERT_EQUAL_UINT32(round, out.id);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_pop_returns_false);
    RUN_TEST(test_push_then_pop_roundtrip);
    RUN_TEST(test_full_queue_drops_newest_and_counts);
    RUN_TEST(test_wraparound);
    return UNITY_END();
}
```

- [ ] **Step 3: 运行测试，确认编译失败**

Run: `pio test -e native -f test_frame_queue`
Expected: FAIL — `frame_queue.h` 不存在 / `FrameQueue` 未定义。

- [ ] **Step 4: 写 frame_queue.h**

`src/analyzer/frame_queue.h`：

```cpp
#pragma once
#include <cstdint>
#include "analyzer_types.h"
#include "shared_types.h"

// 单生产者单消费者（SPSC）无锁环形队列。
// 生产者：Core0 采集任务（push）。消费者：Core1 分析循环（pop）。
// 缓冲由调用方提供：测试用栈/堆，设备用 PSRAM。可用容量为 capacity-1。
class FrameQueue
{
public:
    void init(CapturedFrame *buffer, uint16_t capacity);

    // 生产者侧调用。队列满时丢弃当前帧、累加 dropped，返回 false。
    bool push(const CapturedFrame &frame);

    // 消费者侧调用。无数据返回 false。
    bool pop(CapturedFrame &out);

    uint32_t dropped() const;

private:
    CapturedFrame *buffer_ = nullptr;
    uint16_t capacity_ = 0;
    Shared<uint16_t> head_{0};   // 生产者写
    Shared<uint16_t> tail_{0};   // 消费者写
    Shared<uint32_t> dropped_{0};
};
```

- [ ] **Step 5: 写 frame_queue.cpp**

`src/analyzer/frame_queue.cpp`：

```cpp
#include "analyzer/frame_queue.h"

#ifdef NATIVE_BUILD
#define LOAD(x) (x)
#define STORE(x, v) ((x) = (v))
#else
#define LOAD(x) (x).load(std::memory_order_acquire)
#define STORE(x, v) (x).store((v), std::memory_order_release)
#endif

void FrameQueue::init(CapturedFrame *buffer, uint16_t capacity)
{
    buffer_ = buffer;
    capacity_ = capacity;
    STORE(head_, 0);
    STORE(tail_, 0);
    STORE(dropped_, 0);
}

bool FrameQueue::push(const CapturedFrame &frame)
{
    const uint16_t head = LOAD(head_);
    const uint16_t next = static_cast<uint16_t>((head + 1) % capacity_);
    if (next == LOAD(tail_))   // 满
    {
        STORE(dropped_, LOAD(dropped_) + 1);
        return false;
    }
    buffer_[head] = frame;
    STORE(head_, next);
    return true;
}

bool FrameQueue::pop(CapturedFrame &out)
{
    const uint16_t tail = LOAD(tail_);
    if (tail == LOAD(head_))   // 空
        return false;
    out = buffer_[tail];
    STORE(tail_, static_cast<uint16_t>((tail + 1) % capacity_));
    return true;
}

uint32_t FrameQueue::dropped() const
{
    return LOAD(dropped_);
}
```

- [ ] **Step 6: 运行测试，确认通过**

Run: `pio test -e native -f test_frame_queue`
Expected: PASS — 4 个测试全绿。

- [ ] **Step 7: Commit**

```bash
git add src/analyzer/analyzer_types.h src/analyzer/frame_queue.h src/analyzer/frame_queue.cpp test/test_frame_queue/
git commit -m "feat(analyzer): lock-free SPSC frame queue with drop counter"
```

---

## Task 2：id_table（唯一 ID 表 + 去重/记账，TDD）

**Files:**
- Create: `src/analyzer/id_table.h`
- Create: `src/analyzer/id_table.cpp`
- Test: `test/test_id_table/test_id_table.cpp`

P1 只需「去重 + 当前帧 + 字节变化时间戳 + Delta + 计数」；周期估算/活跃度/flags 留到 P2，但字段先占位以稳定结构。

- [ ] **Step 1: 写失败的测试**

`test/test_id_table/test_id_table.cpp`：

```cpp
#include <unity.h>
#include "analyzer/id_table.h"

static IdTable table;
static IdRecord storage[2][2048];

void setUp()
{
    table.init(&storage[0][0]);
}

void test_first_frame_marks_present_and_counts()
{
    CapturedFrame f;
    f.channel = 0; f.id = 0x132; f.dlc = 3; f.ts_us = 1000;
    f.data[0] = 0x11; f.data[1] = 0x22; f.data[2] = 0x33;
    table.update(f);

    const IdRecord &r = table.record(0, 0x132);
    TEST_ASSERT_TRUE(r.present);
    TEST_ASSERT_EQUAL_UINT32(1, r.rx_count);
    TEST_ASSERT_EQUAL_UINT8(3, r.dlc);
    TEST_ASSERT_EQUAL_UINT8(0x22, r.data[1]);
}

void test_dedup_same_id_increments_count_not_new_slot()
{
    CapturedFrame f;
    f.channel = 1; f.id = 0x200; f.dlc = 1; f.ts_us = 1000;
    table.update(f);
    f.ts_us = 2000;
    table.update(f);
    TEST_ASSERT_EQUAL_UINT32(2, table.record(1, 0x200).rx_count);
}

void test_byte_change_ts_updates_only_on_change()
{
    CapturedFrame f;
    f.channel = 0; f.id = 0x300; f.dlc = 2; f.ts_us = 1000;
    f.data[0] = 0xAA; f.data[1] = 0xBB;
    table.update(f);

    f.ts_us = 5000;
    f.data[0] = 0xAA;   // 不变
    f.data[1] = 0xCC;   // 变了
    table.update(f);

    const IdRecord &r = table.record(0, 0x300);
    TEST_ASSERT_EQUAL_UINT64(1000, r.byte_change_ts[0]);  // 保持首帧
    TEST_ASSERT_EQUAL_UINT64(5000, r.byte_change_ts[1]);  // 更新到变化时刻
}

void test_delta_time_tracks_prev_and_last()
{
    CapturedFrame f;
    f.channel = 0; f.id = 0x400; f.dlc = 1; f.ts_us = 1000;
    table.update(f);
    f.ts_us = 3500;
    table.update(f);
    const IdRecord &r = table.record(0, 0x400);
    TEST_ASSERT_EQUAL_UINT64(3500, r.last_rx_ts);
    TEST_ASSERT_EQUAL_UINT64(1000, r.prev_rx_ts);   // delta = 2500us
}

void test_channel_isolation()
{
    CapturedFrame f;
    f.id = 0x500; f.dlc = 1; f.ts_us = 1000;
    f.channel = 0; table.update(f);
    TEST_ASSERT_TRUE(table.record(0, 0x500).present);
    TEST_ASSERT_FALSE(table.record(1, 0x500).present);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_frame_marks_present_and_counts);
    RUN_TEST(test_dedup_same_id_increments_count_not_new_slot);
    RUN_TEST(test_byte_change_ts_updates_only_on_change);
    RUN_TEST(test_delta_time_tracks_prev_and_last);
    RUN_TEST(test_channel_isolation);
    return UNITY_END();
}
```

- [ ] **Step 2: 运行测试，确认编译失败**

Run: `pio test -e native -f test_id_table`
Expected: FAIL — `id_table.h` 不存在。

- [ ] **Step 3: 写 id_table.h**

`src/analyzer/id_table.h`：

```cpp
#pragma once
#include <cstdint>
#include "analyzer_types.h"

constexpr uint16_t kStdIdCount = 2048;   // 标准帧 0..0x7FF
constexpr uint8_t  kChannelCount = 2;    // A=0, B=1

// 单条 (通道, ID) 记录。P1 用到 present/dlc/data/byte_change_ts/last_rx_ts/prev_rx_ts/rx_count；
// period_est/change_score/flags 为 P2 预留，先占位以稳定二进制布局。
struct IdRecord
{
    bool     present = false;
    uint8_t  dlc = 0;
    uint8_t  data[8] = {};
    uint64_t byte_change_ts[8] = {};
    uint64_t last_rx_ts = 0;
    uint64_t prev_rx_ts = 0;
    uint32_t rx_count = 0;
    uint32_t period_est = 0;    // P2
    uint16_t change_score = 0;  // P2
    uint8_t  flags = 0;         // P2: static/blacklist/whitelist/pinned
};

// 唯一 ID 表。存储由调用方提供（设备侧放 PSRAM，测试侧用静态数组）。
// 仅消费者（Core1）访问，无需加锁。
class IdTable
{
public:
    // 调用方一次性分配的存储字节数（设备侧用它向 PSRAM 申请）。
    static constexpr size_t kStorageBytes =
        sizeof(IdRecord) * kChannelCount * kStdIdCount;

    // base 指向 [kChannelCount * kStdIdCount] 块的首元素。
    // PSRAM 裸内存不会跑构造函数，init 负责清零，保证 present=false 等初值。
    void init(IdRecord *base);

    // 用一帧更新对应记录：去重、记账、字节变化时间戳、Delta。
    void update(const CapturedFrame &frame);

    IdRecord &record(uint8_t channel, uint32_t id);
    const IdRecord &record(uint8_t channel, uint32_t id) const;

private:
    IdRecord *base_ = nullptr;
    IdRecord &at(uint8_t channel, uint32_t id);
};
```

- [ ] **Step 4: 写 id_table.cpp**

`src/analyzer/id_table.cpp`：

```cpp
#include "analyzer/id_table.h"
#include <cstring>

void IdTable::init(IdRecord *base)
{
    base_ = base;
    // PSRAM 裸内存未跑构造函数，显式清零以保证 present=false 等初值。
    memset(base_, 0, kStorageBytes);
}

IdRecord &IdTable::at(uint8_t channel, uint32_t id)
{
    const uint8_t ch = channel < kChannelCount ? channel : 0;
    const uint32_t idx = id < kStdIdCount ? id : 0;
    return base_[ch * kStdIdCount + idx];
}

IdRecord &IdTable::record(uint8_t channel, uint32_t id) { return at(channel, id); }
const IdRecord &IdTable::record(uint8_t channel, uint32_t id) const
{
    return const_cast<IdTable *>(this)->at(channel, id);
}

void IdTable::update(const CapturedFrame &frame)
{
    if (frame.id >= kStdIdCount)
        return;
    IdRecord &r = at(frame.channel, frame.id);

    const uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
    for (uint8_t i = 0; i < dlc; ++i)
    {
        if (!r.present || r.data[i] != frame.data[i])
            r.byte_change_ts[i] = frame.ts_us;
        r.data[i] = frame.data[i];
    }

    r.prev_rx_ts = r.present ? r.last_rx_ts : frame.ts_us;
    r.last_rx_ts = frame.ts_us;
    r.dlc = dlc;
    r.rx_count++;
    r.present = true;
}
```

- [ ] **Step 5: 运行测试，确认通过**

Run: `pio test -e native -f test_id_table`
Expected: PASS — 5 个测试全绿。

- [ ] **Step 6: Commit**

```bash
git add src/analyzer/id_table.h src/analyzer/id_table.cpp test/test_id_table/
git commit -m "feat(analyzer): unique ID table with dedup, byte-change ts, delta time"
```

---

## Task 3：ws_protocol（WS 二进制帧序列化，TDD）

**Files:**
- Create: `src/analyzer/ws_protocol.h`
- Create: `src/analyzer/ws_protocol.cpp`
- Test: `test/test_ws_protocol/test_ws_protocol.cpp`

P1 只需下行 `0x01 帧增量`（变化记录列表）与 `0x02 总线统计`。统一小端，定长布局，前后端共用。

- [ ] **Step 1: 写失败的测试**

`test/test_ws_protocol/test_ws_protocol.cpp`：

```cpp
#include <unity.h>
#include <cstring>
#include "analyzer/ws_protocol.h"

void setUp() {}

void test_frame_delta_header_and_one_record()
{
    WsFrameRecord rec;
    rec.channel = 0; rec.id = 0x132; rec.dlc = 3;
    rec.data[0] = 0xAA; rec.data[1] = 0xBB; rec.data[2] = 0xCC;
    rec.last_rx_ms = 0x01020304;
    for (int i = 0; i < 8; ++i) rec.byte_age_ms[i] = 0;

    uint8_t buf[256];
    const size_t n = wsBuildFrameDelta(buf, sizeof(buf), &rec, 1);

    TEST_ASSERT_EQUAL_UINT8(WS_MSG_FRAME_DELTA, buf[0]); // type
    TEST_ASSERT_EQUAL_UINT8(1, buf[1]);                  // count
    // 头 2 字节 + 1 条定长记录
    TEST_ASSERT_EQUAL_size_t(2 + sizeof(WsFrameRecord), n);
    // 小端 id 校验：记录区起点 buf+2
    const WsFrameRecord *out = reinterpret_cast<const WsFrameRecord *>(buf + 2);
    TEST_ASSERT_EQUAL_UINT16(0x132, out->id);
    TEST_ASSERT_EQUAL_UINT8(0xBB, out->data[1]);
}

void test_frame_delta_respects_buffer_cap()
{
    WsFrameRecord recs[4];
    memset(recs, 0, sizeof(recs));
    uint8_t small[2 + sizeof(WsFrameRecord) + 1]; // 只够装 1 条
    const size_t n = wsBuildFrameDelta(small, sizeof(small), recs, 4);
    TEST_ASSERT_EQUAL_size_t(2 + sizeof(WsFrameRecord), n); // 截断到 1 条
    TEST_ASSERT_EQUAL_UINT8(1, small[1]);
}

void test_bus_stats_layout()
{
    WsBusStats s;
    s.fps_a = 100; s.fps_b = 50;
    s.load_a_x10 = 421; s.load_b_x10 = 88;  // 42.1% / 8.8%
    s.rx_err_a = 0; s.rx_err_b = 3;
    s.bus_off_a = 0; s.bus_off_b = 1;
    s.dropped = 7;

    uint8_t buf[64];
    const size_t n = wsBuildBusStats(buf, sizeof(buf), s);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_BUS_STATS, buf[0]);
    TEST_ASSERT_EQUAL_size_t(1 + sizeof(WsBusStats), n);
    const WsBusStats *out = reinterpret_cast<const WsBusStats *>(buf + 1);
    TEST_ASSERT_EQUAL_UINT16(421, out->load_a_x10);
    TEST_ASSERT_EQUAL_UINT32(7, out->dropped);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_delta_header_and_one_record);
    RUN_TEST(test_frame_delta_respects_buffer_cap);
    RUN_TEST(test_bus_stats_layout);
    return UNITY_END();
}
```

- [ ] **Step 2: 运行测试，确认编译失败**

Run: `pio test -e native -f test_ws_protocol`
Expected: FAIL — `ws_protocol.h` 不存在。

- [ ] **Step 3: 写 ws_protocol.h**

`src/analyzer/ws_protocol.h`：

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

// 下行消息类型（设备 → 浏览器）。首字节标识类型。
enum WsMsgType : uint8_t
{
    WS_MSG_FRAME_DELTA = 0x01, // 变化记录列表
    WS_MSG_BUS_STATS   = 0x02, // 双通道统计
    WS_MSG_DIFF        = 0x03, // P3 预留
};

#pragma pack(push, 1)
// 单条帧增量记录（定长，小端）。时间用毫秒，前端按当前时间算淡出。
struct WsFrameRecord
{
    uint8_t  channel;        // 0=A,1=B
    uint16_t id;             // 标准 11-bit
    uint8_t  dlc;
    uint8_t  data[8];
    uint32_t last_rx_ms;     // 最近到达（设备 millis）
    uint16_t byte_age_ms[8]; // 每字节"距上次变化"的毫秒数（封顶 65535）
    uint32_t rx_count;
};

// 双通道统计快照。
struct WsBusStats
{
    uint16_t fps_a, fps_b;
    uint16_t load_a_x10, load_b_x10; // 占用率%×10
    uint32_t rx_err_a, rx_err_b;
    uint8_t  bus_off_a, bus_off_b;
    uint32_t dropped;                // 队列丢帧累计
};
#pragma pack(pop)

// 序列化：写入 [type|count|records...]，按 buf 容量截断 count。返回写入字节数。
size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count);

// 序列化：写入 [type|WsBusStats]。返回写入字节数。
size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats);
```

- [ ] **Step 4: 写 ws_protocol.cpp**

`src/analyzer/ws_protocol.cpp`：

```cpp
#include "analyzer/ws_protocol.h"
#include <cstring>

size_t wsBuildFrameDelta(uint8_t *buf, size_t cap, const WsFrameRecord *recs, uint8_t count)
{
    if (cap < 2)
        return 0;
    const size_t recSize = sizeof(WsFrameRecord);
    size_t maxByCap = (cap - 2) / recSize;
    if (maxByCap > count)
        maxByCap = count;

    buf[0] = WS_MSG_FRAME_DELTA;
    buf[1] = static_cast<uint8_t>(maxByCap);
    memcpy(buf + 2, recs, maxByCap * recSize);
    return 2 + maxByCap * recSize;
}

size_t wsBuildBusStats(uint8_t *buf, size_t cap, const WsBusStats &stats)
{
    if (cap < 1 + sizeof(WsBusStats))
        return 0;
    buf[0] = WS_MSG_BUS_STATS;
    memcpy(buf + 1, &stats, sizeof(WsBusStats));
    return 1 + sizeof(WsBusStats);
}
```

- [ ] **Step 5: 运行测试，确认通过**

Run: `pio test -e native -f test_ws_protocol`
Expected: PASS — 3 个测试全绿。

- [ ] **Step 6: Commit**

```bash
git add src/analyzer/ws_protocol.h src/analyzer/ws_protocol.cpp test/test_ws_protocol/
git commit -m "feat(analyzer): WS binary protocol (frame delta + bus stats)"
```

---

## Task 4：rx_task（Core0 采集任务，台架验收）

**Files:**
- Create: `src/analyzer/rx_task.h`
- Create: `src/analyzer/rx_task.cpp`

无宿主单测（依赖 FreeRTOS / 驱动）。验收靠 `analyzer` 环境编译通过 + 台架运行。

- [ ] **Step 1: 写 rx_task.h**

`src/analyzer/rx_task.h`：

```cpp
#pragma once
#include "analyzer/frame_queue.h"
#include "drivers/can_driver.h"

// 启动 pin 到 Core0 的采集任务。轮询 A/B → 打微秒时间戳 → 入队。
// 三个参数须在固件生命周期内长期有效（入口的全局/静态存储）。
void rxTaskStart(CanDriver *driverA, CanDriver *driverB, FrameQueue *queue);
```

- [ ] **Step 2: 写 rx_task.cpp**

`src/analyzer/rx_task.cpp`：

```cpp
#include "analyzer/rx_task.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
// 采集任务上下文：两条总线驱动 + 共享队列。静态存储，任务生命周期内有效。
struct RxTaskContext
{
    CanDriver *driverA = nullptr;
    CanDriver *driverB = nullptr;
    FrameQueue *queue = nullptr;
};
RxTaskContext g_ctx;

// 从一条总线尽量排空，逐帧打时间戳并入队。
void drainInto(FrameQueue *queue, CanDriver *driver, uint8_t channel)
{
    if (!driver)
        return;
    CanFrame frame;
    while (driver->read(frame))
    {
        CapturedFrame cap;
        cap.id = frame.id;
        cap.dlc = frame.dlc <= 8 ? frame.dlc : 8;
        cap.channel = channel;
        cap.ts_us = static_cast<uint64_t>(esp_timer_get_time());
        for (uint8_t i = 0; i < 8; ++i)
            cap.data[i] = frame.data[i];
        queue->push(cap); // 满则内部丢最新帧并累加 dropped
    }
}

// 采集任务主体：持续轮询两条总线。
void rxTaskLoop(void *arg)
{
    RxTaskContext *ctx = static_cast<RxTaskContext *>(arg);
    for (;;)
    {
        drainInto(ctx->queue, ctx->driverA, 0);
        drainInto(ctx->queue, ctx->driverB, 1);
        vTaskDelay(1); // 让出 1 tick，避免独占 Core0
    }
}
} // namespace

void rxTaskStart(CanDriver *driverA, CanDriver *driverB, FrameQueue *queue)
{
    g_ctx.driverA = driverA;
    g_ctx.driverB = driverB;
    g_ctx.queue = queue;
    // pin 到 Core0，与 Arduino loop(Core1) 分离，互不抢占。
    xTaskCreatePinnedToCore(rxTaskLoop, "can_rx", 4096, &g_ctx, 10, nullptr, 0);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/analyzer/rx_task.h src/analyzer/rx_task.cpp
git commit -m "feat(analyzer): Core0 RX capture task (timestamp + enqueue)"
```

---

## Task 5：analyzer_wifi（STA/AP 回退，台架验收）

**Files:**
- Create: `src/analyzer/analyzer_wifi.h`
- Create: `src/analyzer/analyzer_wifi.cpp`

沿用现有 `web_server.h` 的连接策略：优先内置默认网络，失败回退 AP。

- [ ] **Step 1: 写 analyzer_wifi.h**

`src/analyzer/analyzer_wifi.h`：

```cpp
#pragma once
#include <Arduino.h>

// 连接 WiFi：先尝试默认 STA，失败则起 AP。返回访问 IP 字符串。
String analyzerWifiBegin();
```

- [ ] **Step 2: 写 analyzer_wifi.cpp**

`src/analyzer/analyzer_wifi.cpp`：

```cpp
#include "analyzer/analyzer_wifi.h"
#include <WiFi.h>

namespace
{
// 尝试连接一个 STA 网络，超时 10s。
bool trySta(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == 0)
        return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
        delay(200);
    return WiFi.status() == WL_CONNECTED;
}
} // namespace

String analyzerWifiBegin()
{
    // 默认网络（与现有固件一致，便于台架直接复用）。
    if (trySta("jhwctcm", "12345678"))
        return WiFi.localIP().toString();
    if (trySta("Cc", "452509526.."))
        return WiFi.localIP().toString();

    WiFi.mode(WIFI_AP);
    WiFi.softAP("CAN-Analyzer", "1234567890");
    delay(120);
    return WiFi.softAPIP().toString();
}
```

- [ ] **Step 3: Commit**

```bash
git add src/analyzer/analyzer_wifi.h src/analyzer/analyzer_wifi.cpp
git commit -m "feat(analyzer): WiFi STA with AP fallback"
```

---

## Task 6：analyzer_web（ESPAsyncWebServer + WS + TX 路由，台架验收）

**Files:**
- Create: `src/analyzer/analyzer_web.h`
- Create: `src/analyzer/analyzer_web.cpp`

职责：托管 LittleFS 静态前端；开 `/ws` WebSocket；`analyzerWebLoop()` 在 Core1 排空队列→更新表→按 ~15Hz 推二进制增量；处理上行 JSON 控制指令（P1 仅 `tx_master`）。

- [ ] **Step 1: 写 analyzer_web.h**

`src/analyzer/analyzer_web.h`：

```cpp
#pragma once
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"

// 注入共享队列与 ID 表（生命周期需长期有效）。
void analyzerWebSetContext(FrameQueue *queue, IdTable *table);

// 启动 Web + WebSocket（需 WiFi 已就绪、LittleFS 已挂载）。
void analyzerWebBegin();

// 在 Core1 loop 中周期调用：排空队列→更新表→推送增量。
void analyzerWebLoop();
```

- [ ] **Step 2: 写 analyzer_web.cpp**

`src/analyzer/analyzer_web.cpp`：

```cpp
#include "analyzer/analyzer_web.h"
#include "analyzer/ws_protocol.h"
#include "can_helpers.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_timer.h>

namespace
{
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

FrameQueue *g_queue = nullptr;
IdTable *g_table = nullptr;

// 脏标记位图：每个 (通道,ID) 一位，排空时置位，推送后清零。
// 大小 = 2 通道 × 2048 ID / 8 = 512 字节。避免每周期全表扫描 4096 项。
constexpr uint32_t kDirtyKeys = static_cast<uint32_t>(kChannelCount) * kStdIdCount;
uint8_t g_dirty[kDirtyKeys / 8] = {};

inline void markDirty(uint8_t ch, uint32_t id)
{
    const uint32_t key = static_cast<uint32_t>(ch) * kStdIdCount + id;
    g_dirty[key >> 3] |= static_cast<uint8_t>(1u << (key & 7));
}

// 上次推送时刻，控制 ~15Hz 节流。
uint32_t g_lastPushMs = 0;
constexpr uint32_t kPushIntervalMs = 66;   // ~15Hz
constexpr size_t   kPushBufBytes = 1400;   // 单帧上限，约 38 条记录

// 处理上行 JSON 控制指令。P1 仅支持 tx_master 总开关。
void handleCommand(const char *text, size_t len)
{
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, text, len))
        return;
    const char *cmd = doc["cmd"] | "";
    if (strcmp(cmd, "tx_master") == 0)
        setCanTxEnabled(doc["on"] | false);
}

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *,
               AwsEventType type, void *, uint8_t *data, size_t len)
{
    if (type == WS_EVT_DATA)
        handleCommand(reinterpret_cast<const char *>(data), len);
}

// 排空队列并把每帧并入 ID 表，同时标脏。
void drainQueueIntoTable()
{
    if (!g_queue || !g_table)
        return;
    CapturedFrame cap;
    while (g_queue->pop(cap))
    {
        if (cap.id >= kStdIdCount)
            continue;
        g_table->update(cap);
        markDirty(cap.channel, cap.id);
    }
}

// 把一条 IdRecord 转成线格式 WsFrameRecord（µs→ms、计算字节年龄）。
WsFrameRecord toWire(uint8_t ch, uint32_t id, const IdRecord &r, uint64_t nowUs)
{
    WsFrameRecord w = {};
    w.channel = ch;
    w.id = static_cast<uint16_t>(id);
    w.dlc = r.dlc;
    for (uint8_t i = 0; i < 8; ++i)
        w.data[i] = r.data[i];
    w.last_rx_ms = static_cast<uint32_t>(r.last_rx_ts / 1000);
    for (uint8_t i = 0; i < 8; ++i)
    {
        const uint64_t ageUs = nowUs > r.byte_change_ts[i] ? nowUs - r.byte_change_ts[i] : 0;
        const uint64_t ageMs = ageUs / 1000;
        w.byte_age_ms[i] = ageMs > 65535 ? 65535 : static_cast<uint16_t>(ageMs);
    }
    w.rx_count = r.rx_count;
    return w;
}

// 遍历脏位图，把变化记录分块（每块 ≤kPushBufBytes）二进制广播，随后清脏。
void pushDelta()
{
    if (!g_table || ws.count() == 0)
        return;

    static uint8_t buf[kPushBufBytes];
    const size_t recSize = sizeof(WsFrameRecord);
    const size_t maxPerFrame = (kPushBufBytes - 2) / recSize;
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());

    WsFrameRecord batch[64];
    size_t batchN = 0;

    auto flush = [&]() {
        if (batchN == 0)
            return;
        const size_t n = wsBuildFrameDelta(buf, sizeof(buf), batch, static_cast<uint8_t>(batchN));
        if (n > 0)
            ws.binaryAll(buf, n);
        batchN = 0;
    };

    for (uint32_t key = 0; key < kDirtyKeys; ++key)
    {
        if ((g_dirty[key >> 3] & (1u << (key & 7))) == 0)
            continue;
        g_dirty[key >> 3] &= static_cast<uint8_t>(~(1u << (key & 7)));

        const uint8_t ch = static_cast<uint8_t>(key / kStdIdCount);
        const uint32_t id = key % kStdIdCount;
        batch[batchN++] = toWire(ch, id, g_table->record(ch, id), nowUs);
        if (batchN >= maxPerFrame || batchN >= (sizeof(batch) / sizeof(batch[0])))
            flush();
    }
    flush();
}
} // namespace

void analyzerWebSetContext(FrameQueue *queue, IdTable *table)
{
    g_queue = queue;
    g_table = table;
}

void analyzerWebBegin()
{
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // 查询 TX 总开关状态（前端横幅轮询）。
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json",
                  String("{\"can_tx_enabled\":") + (isCanTxEnabled() ? "true" : "false") + "}");
    });

    // 切换 TX 总开关。body 含 "true" 视为开，其余视为关。
    server.on("/api/can-tx", HTTP_POST,
        [](AsyncWebServerRequest *req) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
            const String body(reinterpret_cast<const char *>(data), len);
            setCanTxEnabled(body.indexOf("true") >= 0);
            req->send(200, "application/json", "{\"ok\":true}");
        });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void analyzerWebLoop()
{
    drainQueueIntoTable();
    ws.cleanupClients();
    const uint32_t now = millis();
    if (now - g_lastPushMs >= kPushIntervalMs)
    {
        g_lastPushMs = now;
        pushDelta();
    }
}
```

- [ ] **Step 3: 编译验证（台架前先确认能构建）**

Run: `pio run -e analyzer`
Expected: 编译成功（如缺 `ArduinoJson`，在 Step 4 的依赖里补）。

- [ ] **Step 4: 若缺 ArduinoJson，补依赖**

在 `platformio.ini` 的 `[env:analyzer] lib_deps` 追加一行 `bblanchon/ArduinoJson`，重新 `pio run -e analyzer`。

- [ ] **Step 5: Commit**

```bash
git add src/analyzer/analyzer_web.h src/analyzer/analyzer_web.cpp platformio.ini
git commit -m "feat(analyzer): async web server + WS binary delta push + tx_master"
```

---

## Task 7：入口 can_analyzer.cpp（初始化 + 接线，台架验收）

**Files:**
- Create: `src/can_analyzer.cpp`

职责：上电 listen-only；挂 LittleFS；初始化双驱动；在 PSRAM 分配 ID 表存储；建队列；启动 Core0 RX 任务；连 WiFi；起 Web。`loop()` 跑在 Core1，按总开关切换驱动 Normal/ListenOnly 并推进 Web。

- [ ] **Step 1: 写 can_analyzer.cpp**

`src/can_analyzer.cpp`：

```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <memory>

#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_wifi.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/rx_task.h"
#include "can_helpers.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"
#include "pin_config.h"

namespace
{
// 共享队列容量：高负载下给 Core1 留排空余量。
constexpr uint16_t kQueueCapacity = 1024;
CapturedFrame g_queueStorage[kQueueCapacity];
FrameQueue g_queue;

// ID 表与其 PSRAM 存储。
IdTable g_table;

std::unique_ptr<MCP2515Driver> g_canA;
std::unique_ptr<TWAIDriver> g_canB;

// 按当前 TX 总开关同步两条总线的工作模式。
void syncTxMode()
{
    const CanBusMode mode = isCanTxEnabled() ? CanBusMode::Normal : CanBusMode::ListenOnly;
    if (g_canA) g_canA->setBusMode(mode);
    if (g_canB) g_canB->setBusMode(mode);
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(1000);
    setCanTxEnabled(false); // 上电只听

    if (!LittleFS.begin(true))
        Serial.println("LittleFS mount failed");

    // 在 PSRAM 分配 ID 表存储（[2][2048] IdRecord）。
    void *tableMem = heap_caps_malloc(IdTable::kStorageBytes, MALLOC_CAP_SPIRAM);
    g_table.init(static_cast<IdRecord *>(tableMem));

    g_queue.init(g_queueStorage, kQueueCapacity);

    g_canA.reset(new MCP2515Driver(MCP2515_CS, MCP2515_RST,
                                   MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI,
                                   &SPI, 10000000));
    g_canB.reset(new TWAIDriver(static_cast<gpio_num_t>(CAN_TX),
                                static_cast<gpio_num_t>(CAN_RX)));
    g_canA->init();
    g_canB->init();
    syncTxMode();
    g_canA->setFilters(nullptr, 0);
    g_canB->setFilters(nullptr, 0);

    rxTaskStart(g_canA.get(), g_canB.get(), &g_queue);

    analyzerWifiBegin();
    analyzerWebSetContext(&g_queue, &g_table);
    analyzerWebBegin();
    Serial.println("CAN analyzer ready (listen-only)");
}

void loop()
{
    syncTxMode();
    analyzerWebLoop();
    delay(1);
}
```

- [ ] **Step 2: 校验 IdTable 接口与 Task 2 一致**

确认 `src/analyzer/id_table.h`（Task 2 已实现）暴露：
- `static constexpr size_t kStorageBytes = sizeof(IdRecord) * kChannelCount * kStdIdCount;`
- `void init(IdRecord *base);`（保存指针并清零存储）

二者均由 Task 2 提供，本步仅做对齐校验，无需改动。

- [ ] **Step 3: 编译验证**

Run: `pio run -e analyzer`
Expected: 链接成功，生成固件。

- [ ] **Step 4: Commit**

```bash
git add src/can_analyzer.cpp src/analyzer/id_table.h
git commit -m "feat(analyzer): firmware entry wiring dual CAN + Core0 RX + web"
```

---

## Task 8：前端 A/B 实时表 + TX 状态横幅（浏览器验收）

**Files:**
- Create: `data/index.html`
- Create: `data/app.js`
- Create: `data/style.css`

职责：连 `/ws`，解析 `0x01 帧增量` 二进制帧，按 (通道,ID) 原地刷新 A/B 两区表格；顶部常驻 TX 状态横幅（绿=监听-only / 红=可发送），点击经 `/api/can-tx` 切换。

> 二进制布局必须与 `ws_protocol.h`（Task 3）一致：增量帧 = `[u8 type=0x01][u8 count]` 后接 `count` 个记录 `[u8 channel][u16 id][u8 dlc][u8 data[8]][u32 last_rx_ms][u16 byte_age_ms[8]][u32 rx_count]`（全小端，每条 36 字节）。Δt 由前端按相邻两次 `last_rx_ms` 之差计算。

- [ ] **Step 1: 写 index.html**

`data/index.html`：

```html
<!DOCTYPE html>
<html lang="zh">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>CAN 分析仪</title>
  <link rel="stylesheet" href="style.css"/>
</head>
<body>
  <div id="tx-banner" class="banner listen">监听-only（TX 关闭）</div>
  <div id="status">WS: 连接中…</div>
  <div class="grid">
    <section><h2>CAN_A</h2><table id="tbl-a"><thead><tr><th>ID</th><th>DLC</th><th>Data</th><th>计数</th><th>Δt(ms)</th></tr></thead><tbody></tbody></table></section>
    <section><h2>CAN_B</h2><table id="tbl-b"><thead><tr><th>ID</th><th>DLC</th><th>Data</th><th>计数</th><th>Δt(ms)</th></tr></thead><tbody></tbody></table></section>
  </div>
  <script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 2: 写 style.css**

`data/style.css`：

```css
* { box-sizing: border-box; font-family: ui-monospace, monospace; }
body { margin: 0; background: #111; color: #ddd; }
.banner { padding: 10px; text-align: center; font-weight: bold; cursor: pointer; }
.banner.listen { background: #1b5e20; color: #fff; }
.banner.tx { background: #b71c1c; color: #fff; }
#status { padding: 6px 10px; color: #888; }
.grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; padding: 8px; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th, td { border: 1px solid #333; padding: 2px 6px; text-align: left; }
h2 { margin: 4px 0; }
```

- [ ] **Step 3: 写 app.js**

`data/app.js`：

```javascript
const banner = document.getElementById('tx-banner');
const statusEl = document.getElementById('status');
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};      // key = ch*4096+id -> <tr>
const lastRxMs = {};  // key -> 上一次 last_rx_ms，用于算 Δt

function hex(n, w) { return n.toString(16).toUpperCase().padStart(w, '0'); }

function upsert(ch, id, dlc, data, count, lastRx) {
  const key = ch * 4096 + id;
  let tr = rows[key];
  if (!tr) {
    tr = document.createElement('tr');
    tr.innerHTML = '<td></td><td></td><td></td><td></td><td></td>';
    rows[key] = tr;
    tbody[ch].appendChild(tr);
  }
  const prev = lastRxMs[key];
  const deltaMs = prev === undefined ? 0 : (lastRx - prev);
  lastRxMs[key] = lastRx;
  const c = tr.children;
  c[0].textContent = '0x' + hex(id, 3);
  c[1].textContent = dlc;
  c[2].textContent = Array.from(data.slice(0, dlc)).map(b => hex(b, 2)).join(' ');
  c[3].textContent = count;
  c[4].textContent = deltaMs.toFixed(0);
}

function parseDelta(buf) {
  const dv = new DataView(buf);
  let o = 0;
  if (dv.getUint8(o++) !== 0x01) return;
  const count = dv.getUint8(o++);
  for (let i = 0; i < count; i++) {
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const dlc = dv.getUint8(o); o += 1;
    const data = new Uint8Array(buf.slice(o, o + 8)); o += 8;
    const lastRx = dv.getUint32(o, true); o += 4;
    o += 16; // byte_age_ms[8]（u16×8）—— P2 高亮用，P1 先跳过
    const rxCount = dv.getUint32(o, true); o += 4;
    upsert(ch, id, dlc, data, rxCount, lastRx);
  }
}

function connect() {
  const ws = new WebSocket('ws://' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => statusEl.textContent = 'WS: 已连接';
  ws.onclose = () => { statusEl.textContent = 'WS: 断开，重连中…'; setTimeout(connect, 1000); };
  ws.onmessage = (ev) => { if (ev.data instanceof ArrayBuffer) parseDelta(ev.data); };
}

async function refreshTxBanner() {
  try {
    const r = await fetch('/api/status'); const s = await r.json();
    const on = !!s.can_tx_enabled;
    banner.className = 'banner ' + (on ? 'tx' : 'listen');
    banner.textContent = on ? '⚠ 可发送（TX 开启）' : '监听-only（TX 关闭）';
  } catch (e) {}
}

banner.onclick = async () => {
  const on = banner.classList.contains('tx');
  await fetch('/api/can-tx', { method: 'POST', body: on ? 'false' : 'true' });
  refreshTxBanner();
};

connect();
refreshTxBanner();
setInterval(refreshTxBanner, 2000);
```

- [ ] **Step 4: 上传文件系统镜像 + 固件**

Run: `pio run -e analyzer -t uploadfs && pio run -e analyzer -t upload`
Expected: LittleFS 镜像与固件均烧录成功。

- [ ] **Step 5: Commit**

```bash
git add data/index.html data/app.js data/style.css
git commit -m "feat(analyzer): P1 frontend dual-bus live table + TX banner"
```

---

## P1 整体验收（台架，无车）

> **接线技巧**：把 CAN_A 的 TX 接到 CAN_B 总线（或用 loopback），一路发一路收，造出双通道流量。

- [ ] **A1: native 单测全绿**

Run: `pio test -e native`
Expected: frame_queue / id_table / ws_protocol 三组 Unity 测试全部 PASS。

- [ ] **A2: 固件编译 + 烧录**

Run: `pio run -e analyzer -t upload && pio run -e analyzer -t uploadfs`
Expected: 成功。串口出现 `CAN analyzer ready (listen-only)`。

- [ ] **A3: 浏览器实时表**

打开设备 IP。预期：CAN_A / CAN_B 两区随台架流量实时刷新；同一 (通道,ID) 原地更新不新增行（去重正确）；计数递增；Δt 显示合理。

- [ ] **A4: TX 安全横幅**

预期：上电横幅为绿色「监听-only」。点击切到红色「可发送」后，串口/状态反映 TX 开启；再次点击切回。验证默认 OFF。

- [ ] **A5: 背压计数**

制造高负载（提高台架发送频率），预期 `dropped` 计数可见增长而设备不卡死、不重启。
