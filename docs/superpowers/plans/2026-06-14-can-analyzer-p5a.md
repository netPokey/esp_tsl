# CAN 分析仪 P5a 录制/导出 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 CAN 分析仪增加只读录制：手动 start/stop 录制 A+B 全部原始帧到 PSRAM 环形缓冲，停止后以 CSV 流式下载到浏览器，不落 flash。

**Architecture:** 新增 `recorder`（PSRAM 环形缓冲，会话式）+ `record_format`（纯函数 CSV 格式化）；在 `drainQueueIntoTable()` 加录制 tap；`analyzer_web` 加 record_start/stop 命令、`GET /api/record/download` chunked 下载、`/api/status` 字段；`can_analyzer.cpp` 分配/注入；前端加录制按钮与下载链接。

**Tech Stack:** C++17、PlatformIO（env:analyzer 固件 / env:native Unity 单测）、ESPAsyncWebServer、原生 JS 前端。

**关联规范：** `docs/superpowers/specs/2026-06-14-can-analyzer-p5a-design.md`

**前置：** 本计划在专用 worktree 内执行（subagent-driven-development 前用 using-git-worktrees 创建）。基线：main `8508f55`，native 89/89 通过。

---

### Task 1: record_format 纯函数 CSV 格式化器

**Files:**
- Create: `src/analyzer/record_format.h`
- Create: `src/analyzer/record_format.cpp`
- Test: `test/test_record_format/test_record_format.cpp`

`CapturedFrame` 定义见 `src/analyzer/analyzer_types.h`：`{uint32_t id; uint8_t dlc; uint8_t data[8]; uint8_t channel; uint64_t ts_us;}`。

- [ ] **Step 1: 写头文件**

Create `src/analyzer/record_format.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"

// 写 CSV 表头 "time_s,channel,id,dlc,data\n" 到 out（含结尾 NUL 不计入返回值）。
// 返回写入的字符数（不含 NUL）。若 cap 不足则写入尽可能多并返回 0。
size_t recordCsvHeader(char *out, size_t cap);

// 写单帧 CSV 行（含结尾 '\n'）。
// time_s = (frame.ts_us - base_ts_us) / 1e6，保留 6 位小数；
// channel: 0->'A' 1->'B' 其它->'?'；id: "0x" + 3 位大写 hex；
// dlc: 十进制；data: dlc 个字节连续大写 hex（无分隔，dlc>8 截断到 8）。
// 返回写入字符数（不含 NUL）；若 cap 不足容纳整行则不写入并返回 0。
size_t recordCsvLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us);
```

- [ ] **Step 2: 写失败的测试**

Create `test/test_record_format/test_record_format.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "analyzer/record_format.h"

static CapturedFrame makeFrame(uint32_t id, uint8_t dlc, uint8_t channel, uint64_t ts_us)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = dlc;
    f.channel = channel;
    f.ts_us = ts_us;
    for (uint8_t i = 0; i < 8; ++i)
        f.data[i] = static_cast<uint8_t>(0x10 + i);
    return f;
}

void test_header_is_expected_text()
{
    char buf[64];
    size_t n = recordCsvHeader(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("time_s,channel,id,dlc,data\n", buf);
    TEST_ASSERT_EQUAL_UINT(strlen("time_s,channel,id,dlc,data\n"), n);
}

void test_line_basic_fields()
{
    CapturedFrame f = makeFrame(0x123, 3, 0, 2500000ULL);
    char buf[128];
    size_t n = recordCsvLine(buf, sizeof(buf), f, 500000ULL);
    // time = (2500000-500000)/1e6 = 2.000000
    TEST_ASSERT_EQUAL_STRING("2.000000,A,0x123,3,101112\n", buf);
    TEST_ASSERT_EQUAL_UINT(strlen("2.000000,A,0x123,3,101112\n"), n);
}

void test_line_channel_b_and_id_padding()
{
    CapturedFrame f = makeFrame(0x7, 1, 1, 1000000ULL);
    char buf[128];
    recordCsvLine(buf, sizeof(buf), f, 1000000ULL);
    // time 0.000000, channel B, id 0x007, dlc 1, data 1 byte "10"
    TEST_ASSERT_EQUAL_STRING("0.000000,B,0x007,1,10\n", buf);
}

void test_line_dlc_zero_has_empty_data()
{
    CapturedFrame f = makeFrame(0x400, 0, 0, 1000000ULL);
    char buf[128];
    recordCsvLine(buf, sizeof(buf), f, 1000000ULL);
    TEST_ASSERT_EQUAL_STRING("0.000000,A,0x400,0,\n", buf);
}

void test_line_dlc_clamped_to_eight()
{
    CapturedFrame f = makeFrame(0x400, 12, 0, 1000000ULL);
    char buf[128];
    recordCsvLine(buf, sizeof(buf), f, 1000000ULL);
    // 仅输出 8 字节 data，dlc 字段仍打印原值 12
    TEST_ASSERT_EQUAL_STRING("0.000000,A,0x400,12,1011121314151617\n", buf);
}

void test_line_returns_zero_when_buffer_too_small()
{
    CapturedFrame f = makeFrame(0x123, 8, 0, 1000000ULL);
    char buf[8];
    size_t n = recordCsvLine(buf, sizeof(buf), f, 0ULL);
    TEST_ASSERT_EQUAL_UINT(0, n);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_is_expected_text);
    RUN_TEST(test_line_basic_fields);
    RUN_TEST(test_line_channel_b_and_id_padding);
    RUN_TEST(test_line_dlc_zero_has_empty_data);
    RUN_TEST(test_line_dlc_clamped_to_eight);
    RUN_TEST(test_line_returns_zero_when_buffer_too_small);
    return UNITY_END();
}
```

- [ ] **Step 3: 运行测试确认失败**

先在 `[env:native]` 的 `build_src_filter` 末尾加 `+<analyzer/record_format.cpp>`（否则链接失败）。
Run: `COPYFILE_DISABLE=1 pio test -e native -f test_record_format`
Expected: FAIL（recordCsvHeader/recordCsvLine 未实现，链接或断言失败）

- [ ] **Step 4: 写最小实现**

Create `src/analyzer/record_format.cpp`:

```cpp
#include "record_format.h"
#include <cstdio>
#include <cstring>

static char channelChar(uint8_t channel)
{
    if (channel == 0) return 'A';
    if (channel == 1) return 'B';
    return '?';
}

size_t recordCsvHeader(char *out, size_t cap)
{
    const char *h = "time_s,channel,id,dlc,data\n";
    size_t len = strlen(h);
    if (cap < len + 1)
        return 0;
    memcpy(out, h, len + 1);
    return len;
}

size_t recordCsvLine(char *out, size_t cap, const CapturedFrame &frame, uint64_t base_ts_us)
{
    char tmp[128];
    uint64_t rel = frame.ts_us >= base_ts_us ? frame.ts_us - base_ts_us : 0;
    double t = static_cast<double>(rel) / 1000000.0;
    int prefix = snprintf(tmp, sizeof(tmp), "%.6f,%c,0x%03X,%u,",
                          t, channelChar(frame.channel),
                          static_cast<unsigned>(frame.id),
                          static_cast<unsigned>(frame.dlc));
    if (prefix < 0)
        return 0;
    size_t pos = static_cast<size_t>(prefix);
    uint8_t n = frame.dlc > 8 ? 8 : frame.dlc;
    for (uint8_t i = 0; i < n; ++i)
    {
        int w = snprintf(tmp + pos, sizeof(tmp) - pos, "%02X", frame.data[i]);
        if (w < 0) return 0;
        pos += static_cast<size_t>(w);
    }
    if (pos + 2 > sizeof(tmp))
        return 0;
    tmp[pos++] = '\n';
    tmp[pos] = '\0';
    if (cap < pos + 1)
        return 0;
    memcpy(out, tmp, pos + 1);
    return pos;
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `COPYFILE_DISABLE=1 pio test -e native -f test_record_format`
Expected: PASS（6/6）

- [ ] **Step 6: 提交**

```bash
git add src/analyzer/record_format.h src/analyzer/record_format.cpp test/test_record_format/test_record_format.cpp platformio.ini
git commit -m "feat(analyzer): add P5a CSV record formatter"
```

---

### Task 2: recorder 会话式 PSRAM 环形缓冲

**Files:**
- Create: `src/analyzer/recorder.h`
- Create: `src/analyzer/recorder.cpp`
- Test: `test/test_recorder/test_recorder.cpp`

- [ ] **Step 1: 写头文件**

Create `src/analyzer/recorder.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"

// 会话式录制器：固定容量环形缓冲，满则覆盖最旧帧并累加 dropped。
// 单生产者(drain)单消费者(download)，本架构同核串行，无内部锁。
class Recorder
{
public:
    void init(CapturedFrame *storage, size_t capacity);
    void start();                 // 清空 head/count/dropped，置 active
    void stop();                  // 清 active；缓冲内容保留供下载
    bool active() const { return active_; }
    size_t count() const { return count_; }
    size_t capacity() const { return capacity_; }
    uint32_t dropped() const { return dropped_; }
    void push(const CapturedFrame &frame);
    // 旧->新顺序，跳过前 skip 帧，最多写 cap 帧到 out，返回写入帧数。
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

- [ ] **Step 2: 写失败的测试**

Create `test/test_recorder/test_recorder.cpp`:

```cpp
#include <unity.h>
#include "analyzer/recorder.h"

static CapturedFrame frameWithId(uint32_t id)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = 1;
    f.data[0] = static_cast<uint8_t>(id & 0xFF);
    return f;
}

void test_init_and_start_reset_state()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    TEST_ASSERT_FALSE(r.active());
    TEST_ASSERT_EQUAL_UINT(0, r.count());
    TEST_ASSERT_EQUAL_UINT(4, r.capacity());
    r.start();
    TEST_ASSERT_TRUE(r.active());
    TEST_ASSERT_EQUAL_UINT(0, r.count());
    TEST_ASSERT_EQUAL_UINT(0, r.dropped());
}

void test_push_accumulates_count()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(1));
    r.push(frameWithId(2));
    TEST_ASSERT_EQUAL_UINT(2, r.count());
    TEST_ASSERT_EQUAL_UINT(0, r.dropped());
}

void test_ring_overwrites_oldest_and_counts_dropped()
{
    CapturedFrame storage[3];
    Recorder r;
    r.init(storage, 3);
    r.start();
    for (uint32_t i = 1; i <= 5; ++i)
        r.push(frameWithId(i));
    TEST_ASSERT_EQUAL_UINT(3, r.count());     // 上限
    TEST_ASSERT_EQUAL_UINT(2, r.dropped());   // 丢了 id 1、2
    CapturedFrame out[3];
    size_t n = r.collect(out, 3, 0);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT(3, out[0].id);     // 旧->新：3,4,5
    TEST_ASSERT_EQUAL_UINT(4, out[1].id);
    TEST_ASSERT_EQUAL_UINT(5, out[2].id);
}

void test_collect_old_to_new_when_not_full()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(10));
    r.push(frameWithId(20));
    CapturedFrame out[4];
    size_t n = r.collect(out, 4, 0);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(10, out[0].id);
    TEST_ASSERT_EQUAL_UINT(20, out[1].id);
}

void test_collect_with_skip_paging()
{
    CapturedFrame storage[5];
    Recorder r;
    r.init(storage, 5);
    r.start();
    for (uint32_t i = 1; i <= 5; ++i)
        r.push(frameWithId(i));
    CapturedFrame out[5];
    size_t n = r.collect(out, 2, 2);          // skip 2，取 2：id 3,4
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(3, out[0].id);
    TEST_ASSERT_EQUAL_UINT(4, out[1].id);
}

void test_collect_skip_beyond_count_returns_zero()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(1));
    CapturedFrame out[4];
    TEST_ASSERT_EQUAL_UINT(0, r.collect(out, 4, 5));
}

void test_stop_keeps_content()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(frameWithId(7));
    r.stop();
    TEST_ASSERT_FALSE(r.active());
    TEST_ASSERT_EQUAL_UINT(1, r.count());
    CapturedFrame out[4];
    TEST_ASSERT_EQUAL_UINT(1, r.collect(out, 4, 0));
    TEST_ASSERT_EQUAL_UINT(7, out[0].id);
}

void test_uninitialized_is_safe()
{
    Recorder r;
    r.start();
    r.push(frameWithId(1));     // 不应崩溃
    TEST_ASSERT_EQUAL_UINT(0, r.count());
    CapturedFrame out[2];
    TEST_ASSERT_EQUAL_UINT(0, r.collect(out, 2, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_and_start_reset_state);
    RUN_TEST(test_push_accumulates_count);
    RUN_TEST(test_ring_overwrites_oldest_and_counts_dropped);
    RUN_TEST(test_collect_old_to_new_when_not_full);
    RUN_TEST(test_collect_with_skip_paging);
    RUN_TEST(test_collect_skip_beyond_count_returns_zero);
    RUN_TEST(test_stop_keeps_content);
    RUN_TEST(test_uninitialized_is_safe);
    return UNITY_END();
}
```

- [ ] **Step 3: 运行测试确认失败**

先在 `[env:native]` 的 `build_src_filter` 末尾加 `+<analyzer/recorder.cpp>`。
Run: `COPYFILE_DISABLE=1 pio test -e native -f test_recorder`
Expected: FAIL（Recorder 方法未实现）

- [ ] **Step 4: 写最小实现**

Create `src/analyzer/recorder.cpp`:

```cpp
#include "recorder.h"

void Recorder::init(CapturedFrame *storage, size_t capacity)
{
    storage_ = storage;
    capacity_ = capacity;
    head_ = 0;
    count_ = 0;
    dropped_ = 0;
    active_ = false;
}

void Recorder::start()
{
    head_ = 0;
    count_ = 0;
    dropped_ = 0;
    active_ = true;
}

void Recorder::stop()
{
    active_ = false;
}

void Recorder::push(const CapturedFrame &frame)
{
    if (!storage_ || capacity_ == 0)
        return;
    storage_[head_] = frame;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_)
        ++count_;
    else
        ++dropped_;     // 覆盖了一条最旧帧
}

size_t Recorder::collect(CapturedFrame *out, size_t cap, size_t skip) const
{
    if (!storage_ || count_ == 0 || skip >= count_ || cap == 0)
        return 0;
    size_t oldest = (count_ < capacity_) ? 0 : head_;
    size_t avail = count_ - skip;
    size_t n = avail < cap ? avail : cap;
    for (size_t i = 0; i < n; ++i)
        out[i] = storage_[(oldest + skip + i) % capacity_];
    return n;
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `COPYFILE_DISABLE=1 pio test -e native -f test_recorder`
Expected: PASS（8/8）

- [ ] **Step 6: 提交**

```bash
git add src/analyzer/recorder.h src/analyzer/recorder.cpp test/test_recorder/test_recorder.cpp platformio.ini
git commit -m "feat(analyzer): add P5a session recorder ring buffer"
```

---

### Task 3: recordCsvFill 流式分块填充 helper

下载回调每次得到一段 buffer（`maxLen` 约 TCP MSS），需跨多次调用从录制缓冲按旧→新逐帧追加 CSV，且半行容不下时下次续传。把这段逻辑做成可 native 测试的纯函数（用真实 `Recorder` 驱动），Web 层只做薄包装。

**Files:**
- Modify: `src/analyzer/record_format.h`（追加 `RecordCsvCursor` + `recordCsvFill`）
- Modify: `src/analyzer/record_format.cpp`（追加实现，新增 `#include "recorder.h"`）
- Test: `test/test_record_stream/test_record_stream.cpp`

> 说明：`record_format.cpp` 与 `recorder.cpp` 在 Task 1/2 已加入 `[env:native]` 的 `build_src_filter`，本任务无需再改 platformio.ini。

- [ ] **Step 1: 在头文件追加游标与填充声明**

Edit `src/analyzer/record_format.h`，在文件末尾（`recordCsvLine` 声明之后）追加：

```cpp
#include "recorder.h"

// 下载流式游标：跨多次回调保持进度。
struct RecordCsvCursor
{
    size_t frame_index = 0;   // 下一个待输出帧（旧->新序号）
    bool header_sent = false;
    uint64_t base_ts_us = 0;
    bool base_set = false;
};

// 向 buf（容量 maxLen）追加尽可能多的 CSV 内容并推进 cursor。
// 首次调用先写表头；随后按旧->新从 rec 取帧格式化，半行容不下则停在边界，下次续传。
// total 为下载开始时快照的帧数（避免并发 push 越界）。返回本次写入字节数；返回 0 表示已结束。
size_t recordCsvFill(char *buf, size_t maxLen, const Recorder &rec, size_t total, RecordCsvCursor &cursor);
```

- [ ] **Step 2: 写失败的测试**

Create `test/test_record_stream/test_record_stream.cpp`:

```cpp
#include <unity.h>
#include <string>
#include "analyzer/record_format.h"
#include "analyzer/recorder.h"

static CapturedFrame mk(uint32_t id, uint64_t ts_us)
{
    CapturedFrame f = {};
    f.id = id;
    f.dlc = 1;
    f.channel = 0;
    f.ts_us = ts_us;
    f.data[0] = 0xAB;
    return f;
}

void test_empty_recorder_emits_header_only()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    RecordCsvCursor cur;
    char buf[256];
    size_t n = recordCsvFill(buf, sizeof(buf), r, r.count(), cur);
    buf[n] = '\0';
    TEST_ASSERT_EQUAL_STRING("time_s,channel,id,dlc,data\n", buf);
    // 第二次调用应结束
    TEST_ASSERT_EQUAL_UINT(0, recordCsvFill(buf, sizeof(buf), r, r.count(), cur));
}

void test_single_call_header_plus_all_lines()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(mk(0x100, 1000000ULL));   // base
    r.push(mk(0x101, 1500000ULL));
    RecordCsvCursor cur;
    char buf[512];
    size_t n = recordCsvFill(buf, sizeof(buf), r, r.count(), cur);
    buf[n] = '\0';
    std::string s(buf);
    TEST_ASSERT_EQUAL_STRING(
        "time_s,channel,id,dlc,data\n"
        "0.000000,A,0x100,1,AB\n"
        "0.500000,A,0x101,1,AB\n",
        s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, recordCsvFill(buf, sizeof(buf), r, r.count(), cur));
}

void test_small_buffer_resumes_across_calls()
{
    CapturedFrame storage[8];
    Recorder r;
    r.init(storage, 8);
    r.start();
    for (uint32_t i = 0; i < 5; ++i)
        r.push(mk(0x200 + i, 1000000ULL + i * 1000000ULL));
    const size_t total = r.count();
    // 拼接多次小 chunk 的结果应等于一次大 buffer 的结果
    RecordCsvCursor curBig;
    char big[1024];
    size_t bigN = recordCsvFill(big, sizeof(big), r, total, curBig);
    big[bigN] = '\0';

    RecordCsvCursor curSmall;
    std::string acc;
    char small[40];          // 仅够一行多一点
    for (int guard = 0; guard < 100; ++guard)
    {
        size_t m = recordCsvFill(small, sizeof(small), r, total, curSmall);
        if (m == 0)
            break;
        acc.append(small, m);
    }
    TEST_ASSERT_EQUAL_STRING(big, acc.c_str());
}

void test_base_ts_taken_from_oldest_frame()
{
    CapturedFrame storage[4];
    Recorder r;
    r.init(storage, 4);
    r.start();
    r.push(mk(0x300, 5000000ULL));   // oldest -> base
    r.push(mk(0x301, 7000000ULL));   // +2.0s
    RecordCsvCursor cur;
    char buf[512];
    size_t n = recordCsvFill(buf, sizeof(buf), r, r.count(), cur);
    buf[n] = '\0';
    std::string s(buf);
    TEST_ASSERT_TRUE(s.find("0.000000,A,0x300") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("2.000000,A,0x301") != std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_recorder_emits_header_only);
    RUN_TEST(test_single_call_header_plus_all_lines);
    RUN_TEST(test_small_buffer_resumes_across_calls);
    RUN_TEST(test_base_ts_taken_from_oldest_frame);
    return UNITY_END();
}
```

- [ ] **Step 3: 运行测试确认失败**

Run: `COPYFILE_DISABLE=1 pio test -e native -f test_record_stream`
Expected: FAIL（recordCsvFill 未实现）

- [ ] **Step 4: 写最小实现**

Edit `src/analyzer/record_format.cpp`，在文件末尾追加：

```cpp
size_t recordCsvFill(char *buf, size_t maxLen, const Recorder &rec, size_t total, RecordCsvCursor &cursor)
{
    size_t written = 0;
    if (!cursor.header_sent)
    {
        size_t h = recordCsvHeader(buf, maxLen);
        if (h == 0)
            return 0;
        cursor.header_sent = true;
        written = h;
    }
    while (cursor.frame_index < total)
    {
        CapturedFrame f;
        if (rec.collect(&f, 1, cursor.frame_index) == 0)
            break;
        if (!cursor.base_set)
        {
            cursor.base_ts_us = f.ts_us;
            cursor.base_set = true;
        }
        size_t w = recordCsvLine(buf + written, maxLen - written, f, cursor.base_ts_us);
        if (w == 0)
            break;     // 本 chunk 容不下整行，停在边界下次续传
        written += w;
        ++cursor.frame_index;
    }
    return written;
}
```

`record_format.cpp` 顶部 include 区已在 Step 1 经头文件间接引入 `recorder.h`；若实现需要可显式 `#include "recorder.h"`（头文件已含，通常无需重复）。

- [ ] **Step 5: 运行测试确认通过**

Run: `COPYFILE_DISABLE=1 pio test -e native -f test_record_stream`
Expected: PASS（4/4）

- [ ] **Step 6: 回归全部 native 测试**

Run: `COPYFILE_DISABLE=1 pio test -e native`
Expected: 现有 89 + record_format 6 + recorder 8 + record_stream 4 全通过。

- [ ] **Step 7: 提交**

```bash
git add src/analyzer/record_format.h src/analyzer/record_format.cpp test/test_record_stream/test_record_stream.cpp
git commit -m "feat(analyzer): add P5a CSV streaming fill helper"
```

---

### Task 4: analyzer_web 后端接线 + can_analyzer 分配注入

设备侧整合：命令、drain tap、status 字段、`GET /api/record/download` chunked 路由、context 注入 `Recorder*`，以及入口分配 PSRAM 并注入。逻辑核心（CSV 填充）已在 Task 3 native 测试覆盖，本任务靠固件构建验证。

**Files:**
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `src/analyzer/analyzer_web.cpp`
- Modify: `src/can_analyzer.cpp`

- [ ] **Step 1: 头文件加 include 与 context 参数**

Edit `src/analyzer/analyzer_web.h`：

在顶部 include 区追加：
```cpp
#include "analyzer/recorder.h"
```

把 `analyzerWebSetContext` 声明改为（末尾加 `Recorder *recorder`）：
```cpp
void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels,
                           WatchedSignalWindow *signals, CommonSignalStore *common_signals,
                           Recorder *recorder);
```

- [ ] **Step 2: analyzer_web.cpp 加 include、全局指针、命令类型**

Edit `src/analyzer/analyzer_web.cpp`：

(a) include 区加（与其它 analyzer 头并列）：
```cpp
#include "analyzer/recorder.h"
#include "analyzer/record_format.h"
```

(b) 全局指针区（`CommonSignalStore *g_commonSignals = nullptr;` 之后）加：
```cpp
Recorder *g_recorder = nullptr;
```

(c) `enum class PendingCmdType` 末尾（`P4Hints` 之后）加两项：
```cpp
    P4Hints,
    RecordStart,
    RecordStop
```

- [ ] **Step 3: 命令处理（process + parse）**

Edit `src/analyzer/analyzer_web.cpp`：

(a) `processPendingCommand` 的 switch，在 `P4Hints` case 之后加：
```cpp
    case PendingCmdType::RecordStart:
        if (g_recorder)
            g_recorder->start();
        break;
    case PendingCmdType::RecordStop:
        if (g_recorder)
            g_recorder->stop();
        break;
```

(b) `handleCommand` 中，在最后一个 `if (strcmp(cmd, "p4_hints") == 0) {...}` 块之后加：
```cpp
    if (strcmp(cmd, "record_start") == 0)
    {
        pending.type = PendingCmdType::RecordStart;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "record_stop") == 0)
    {
        pending.type = PendingCmdType::RecordStop;
        enqueuePendingCommand(pending);
        return;
    }
```

- [ ] **Step 4: drain tap**

Edit `drainQueueIntoTable()`，在 `if (g_signals) g_signals->push(cap);` 之后、`g_table->update(cap);` 之前加：
```cpp
        if (g_recorder && g_recorder->active())
            g_recorder->push(cap);
```

- [ ] **Step 5: context 注入**

Edit `analyzerWebSetContext` 的签名与函数体：

签名末尾加 `Recorder *recorder`（与头文件一致）；函数体末尾（`g_commonSignals = common_signals;` 之后）加：
```cpp
    g_recorder = recorder;
```

- [ ] **Step 6: /api/status 增字段 + 下载路由**

Edit `analyzerWebBegin()`：

(a) `/api/status` handler，在 `out += ",\"pending_dropped\":" + String(pendingDroppedCount());` 之后加：
```cpp
        out += ",\"recording\":" + String(g_recorder && g_recorder->active() ? "true" : "false");
        out += ",\"record_count\":" + String(g_recorder ? static_cast<uint32_t>(g_recorder->count()) : 0);
        out += ",\"record_capacity\":" + String(g_recorder ? static_cast<uint32_t>(g_recorder->capacity()) : 0);
        out += ",\"record_dropped\":" + String(g_recorder ? g_recorder->dropped() : 0);
```

(b) 在 `/api/p4/common` 的 POST 路由注册之后（`analyzerWebBegin` 内、`server.begin()` 之前的合适位置）加下载路由：
```cpp
    server.on("/api/record/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!g_recorder || g_recorder->count() == 0)
        {
            request->send(404, "text/plain", "no recording");
            return;
        }
        auto cursor = std::make_shared<RecordCsvCursor>();
        const size_t total = g_recorder->count();
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "text/csv",
            [cursor, total](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
                if (!g_recorder)
                    return 0;
                return recordCsvFill(reinterpret_cast<char *>(buffer), maxLen, *g_recorder, total, *cursor);
            });
        response->addHeader("Content-Disposition", "attachment; filename=\"can-record.csv\"");
        request->send(response);
    });
```

确认文件顶部已 `#include <memory>`（若无则添加，用于 `std::make_shared`）。

- [ ] **Step 7: can_analyzer.cpp 分配 + init + 注入**

Edit `src/can_analyzer.cpp`：

(a) include 区加：
```cpp
#include "analyzer/recorder.h"
```

(b) 匿名 namespace 内，`CommonSignalStore g_commonSignals;` 之后加：
```cpp
constexpr size_t kRecordCapacity = 100000;
CapturedFrame *g_recordStorage = nullptr;
Recorder g_recorder;
```

(c) `setup()` 内，在 `g_commonSignals.begin();` 之后加分配：
```cpp
    g_recordStorage = static_cast<CapturedFrame *>(ps_malloc(sizeof(CapturedFrame) * kRecordCapacity));
    if (g_recordStorage)
        g_recorder.init(g_recordStorage, kRecordCapacity);
    else
        Serial.println("PSRAM allocation failed for recorder");
```

(d) `analyzerWebSetContext(...)` 调用，在最后一个实参 `&g_commonSignals` 之后加：
```cpp
                          &g_commonSignals,
                          g_recordStorage ? &g_recorder : nullptr);
```
（注意把原先 `&g_commonSignals);` 的结尾分号移到新增的最后一个实参后。）

- [ ] **Step 8: 构建固件验证**

```bash
COPYFILE_DISABLE=1 pio run -e analyzer
```
Expected: SUCCESS（编译链接通过，无未定义引用、无签名不匹配）。

- [ ] **Step 9: 回归 native（确保头文件改动不破坏测试）**

Run: `COPYFILE_DISABLE=1 pio test -e native`
Expected: 全部通过（107 用例：89 + 6 + 8 + 4）。

- [ ] **Step 10: 提交**

```bash
git add src/analyzer/analyzer_web.h src/analyzer/analyzer_web.cpp src/can_analyzer.cpp
git commit -m "feat(analyzer): wire P5a recorder into web/backend and entry"
```

---

### Task 5: 前端录制 UI

复用既有 `/api/status`（2 秒轮询，`refreshTxBanner`）渲染录制状态；按钮经 `sendCmd` 发 WS 命令；下载走原生 `<a download>`。无单元测试，靠 `buildfs` 与手动台架验收。

**Files:**
- Modify: `data/analyzer/index.html`
- Modify: `data/analyzer/app.js`
- Modify: `data/analyzer/style.css`

- [ ] **Step 1: index.html 加录制控制区**

Edit `data/analyzer/index.html`，在 `<div class="controls p3-controls">…</div>` 整块之后、`<div class="controls"> <label>进制…` 之前插入：

```html
  <div class="controls record-controls">
    <button id="record-start-btn">录制开始</button>
    <button id="record-stop-btn" disabled>录制停止</button>
    <a id="record-download" class="record-link disabled" href="/api/record/download" download>下载 CSV</a>
    <span id="record-status">录制：空闲</span>
  </div>
```

- [ ] **Step 2: app.js 取元素 + 渲染函数 + 接线**

Edit `data/analyzer/app.js`：

(a) 顶部元素引用区（`masterToggle` 等附近）加：
```javascript
const recordStartBtn = document.getElementById('record-start-btn');
const recordStopBtn = document.getElementById('record-stop-btn');
const recordDownload = document.getElementById('record-download');
const recordStatusEl = document.getElementById('record-status');
```

(b) 在 `refreshTxBanner` 函数定义之后加渲染函数：
```javascript
function paintRecordStatus(s) {
  const recording = !!s.recording;
  const count = Number(s.record_count || 0);
  const dropped = Number(s.record_dropped || 0);
  recordStartBtn.disabled = recording;
  recordStopBtn.disabled = !recording;
  if (recording) {
    recordStatusEl.textContent = `录制中 · ${count} 帧 · dropped=${dropped}`;
  } else if (count > 0) {
    recordStatusEl.textContent = `空闲 · ${count} 帧可下载 · dropped=${dropped}`;
  } else {
    recordStatusEl.textContent = '录制：空闲';
  }
  const canDownload = !recording && count > 0;
  recordDownload.classList.toggle('disabled', !canDownload);
}
```

(c) 在 `refreshTxBanner` 内 `paintTxState();` 之后加：
```javascript
    paintRecordStatus(s);
```

(d) 在按钮接线区（`masterToggle.onclick = ...` 附近）加：
```javascript
recordStartBtn.onclick = () => {
  if (sendCmd({ cmd: 'record_start' })) setTimeout(refreshTxBanner, 100);
};
recordStopBtn.onclick = () => {
  if (sendCmd({ cmd: 'record_stop' })) setTimeout(refreshTxBanner, 100);
};
recordDownload.addEventListener('click', (e) => {
  if (recordDownload.classList.contains('disabled')) e.preventDefault();
});
```

- [ ] **Step 3: style.css 加样式**

Edit `data/analyzer/style.css`，文件末尾追加：

```css
.record-controls { align-items: center; }
.record-link {
  padding: 4px 10px;
  border: 1px solid #4a4a4a;
  border-radius: 4px;
  text-decoration: none;
  color: #ddd;
}
.record-link.disabled {
  opacity: 0.45;
  pointer-events: none;
  cursor: default;
}
```

- [ ] **Step 4: JS 语法自检（若有 node）**

Run: `node --check data/analyzer/app.js`
Expected: 无输出（语法正确）。若环境无 node，跳过，由 Step 5 的 buildfs 间接确认文件被打包。

- [ ] **Step 5: 构建文件系统镜像验证**

```bash
find . -name '._*' -delete 2>/dev/null; COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```
Expected: SUCCESS，LittleFS 仅包含 `/app.js`、`/index.html`、`/style.css`。

- [ ] **Step 6: 提交**

```bash
git add data/analyzer/index.html data/analyzer/app.js data/analyzer/style.css
git commit -m "feat(analyzer): add P5a record controls and CSV download UI"
```

---

### Task 6: 全量验证与收尾

- [ ] **Step 1: AppleDouble 清理 + 全量 native**

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native
```
Expected: 107 用例全通过（89 既有 + record_format 6 + recorder 8 + record_stream 4）。

- [ ] **Step 2: 固件构建**

```bash
COPYFILE_DISABLE=1 pio run -e analyzer
```
Expected: SUCCESS。

- [ ] **Step 3: 文件系统镜像**

```bash
find . -name '._*' -delete 2>/dev/null; COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```
Expected: SUCCESS，LittleFS 仅 `/app.js`、`/index.html`、`/style.css`。

- [ ] **Step 4: 最终代码审查**

派发最终 code-reviewer 审查整个 P5a 实现（对照本计划与 spec）。重点：
- recorder 环形/dropped/collect 旧→新与分页边界。
- recordCsvFill 跨 chunk 续传不丢/不重/不溢出。
- drain tap 仅在 active 时 push，未 init 安全。
- download 路由：count==0 返回 404；并发期按快照 total 为界。
- 纯只读：未触碰任何 TX 路径 / setBusMode。
- 现有 89 测试无回归。

- [ ] **Step 5: 台架手动验收（用户执行）**

烧录固件 + 文件系统后：浏览器打开分析仪 → 制造 A/B 流量 → `录制开始` → 观察 `录制中 · N 帧` 增长 → `录制停止` → 点 `下载 CSV` → 校验 CSV：表头正确、`time_s` 从 0 递增、`channel` A/B 正确、`id`/`dlc`/`data` 与实时表一致、长录时 `dropped` 计数合理。

- [ ] **Step 6: 收尾**

使用 superpowers:finishing-a-development-branch：合并到 main（用户确认后）、清理 worktree、更新项目记忆（P5a 完成、下一步 P5b 发送）。
