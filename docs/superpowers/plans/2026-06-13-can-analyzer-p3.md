# CAN 分析仪 P3 对比找包 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build P3 compare-and-find workflow: baseline blacklisting, snapshot Diff, 5s pre-trigger recall, frontend filters/search, and NVS-persisted ID labels.

**Architecture:** Keep Core0 unchanged. Core1 owns all P3 state: `PretriggerBuffer`, `SnapshotStore`, and `LabelStore`, updated from `analyzerWebLoop()` while draining the existing SPSC queue. Frontend handles blacklist/whitelist/range/search filtering locally; backend provides snapshots, pre-trigger summaries, baseline present-ID lists, and persistent labels.

**Tech Stack:** Arduino + PlatformIO, ESPAsyncWebServer/WebSocket, Preferences(NVS), LittleFS frontend, Unity native tests.

---

## File Structure

- Create: `src/analyzer/pretrigger_buffer.h/.cpp` — 5s raw frame ring buffer and summary builder.
- Create: `test/test_pretrigger_buffer/test_pretrigger_buffer.cpp` — native tests for time-window collection and summaries.
- Create: `src/analyzer/snapshot_store.h/.cpp` — A/B snapshot capture and current-value diff.
- Create: `test/test_snapshot_store/test_snapshot_store.cpp` — native tests for added/removed/changed.
- Create: `src/analyzer/label_store.h/.cpp` — 64-entry label store with Preferences-backed persistence on firmware and memory-only native backend.
- Create: `test/test_label_store/test_label_store.cpp` — native tests for upsert/remove/capacity.
- Modify: `src/analyzer/ws_protocol.h/.cpp` — add P3 0x03 records and builders.
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp` — layout/buffer tests for P3 messages.
- Modify: `src/analyzer/analyzer_web.h/.cpp` — wire P3 state, JSON commands, `/api/labels`, and 0x03 sends.
- Modify: `src/can_analyzer.cpp` — allocate PSRAM storage and pass P3 contexts to web layer.
- Modify: `platformio.ini` — include new `.cpp` files in native env.
- Modify: `data/analyzer/index.html`, `data/analyzer/style.css`, `data/analyzer/app.js` — P3 UI and local filtering.

---

## Task 1: PretriggerBuffer (TDD)

**Files:**
- Create: `src/analyzer/pretrigger_buffer.h`
- Create: `src/analyzer/pretrigger_buffer.cpp`
- Create: `test/test_pretrigger_buffer/test_pretrigger_buffer.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Write the failing test**

Create `test/test_pretrigger_buffer/test_pretrigger_buffer.cpp`:

```cpp
#include <unity.h>
#include "analyzer/pretrigger_buffer.h"

static CapturedFrame storage[8];
static PretriggerBuffer buf;

void setUp() { buf.init(storage, 8); }
void tearDown() {}

static CapturedFrame mk(uint8_t ch, uint32_t id, uint64_t ts_us, uint8_t d0) {
    CapturedFrame f; f.id = id; f.dlc = 1; f.data[0] = d0; f.channel = ch; f.ts_us = ts_us;
    return f;
}

void test_collect_returns_frames_in_window() {
    buf.push(mk(0, 0x100, 1000000, 1));
    buf.push(mk(0, 0x100, 2000000, 2));
    buf.push(mk(1, 0x200, 3000000, 9));
    CapturedFrame out[8];
    // now = 3.0s, window = 5s -> all three
    const size_t n = buf.collect(3000000, 5000000, out, 8);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT64(1000000, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT64(3000000, out[2].ts_us);
}

void test_collect_excludes_frames_older_than_window() {
    buf.push(mk(0, 0x100, 1000000, 1)); // 6s before now
    buf.push(mk(0, 0x100, 6500000, 2)); // 0.5s before now
    CapturedFrame out[8];
    const size_t n = buf.collect(7000000, 5000000, out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT64(6500000, out[0].ts_us);
}

void test_ring_overwrites_oldest_when_full() {
    for (uint32_t i = 0; i < 10; ++i) buf.push(mk(0, 0x100, (i + 1) * 1000000ULL, (uint8_t)i));
    CapturedFrame out[8];
    // capacity 8 -> only last 7 retained (usable = capacity-1)
    const size_t n = buf.collect(10000000, 100000000, out, 8);
    TEST_ASSERT_EQUAL_size_t(7, n);
    TEST_ASSERT_EQUAL_UINT64(4000000, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT64(10000000, out[6].ts_us);
}

void test_summarize_groups_by_channel_id() {
    buf.push(mk(0, 0x100, 1000000, 0x11));
    buf.push(mk(0, 0x100, 1500000, 0x22)); // data changed
    buf.push(mk(1, 0x100, 1800000, 0x33)); // different channel
    WsPretriggerRecord out[8];
    const size_t n = buf.summarize(2000000, 5000000, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
    // find ch0/0x100
    int idx = -1;
    for (size_t i = 0; i < n; ++i) if (out[i].channel == 0 && out[i].id == 0x100) idx = (int)i;
    TEST_ASSERT_TRUE(idx >= 0);
    TEST_ASSERT_EQUAL_UINT16(2, out[idx].frames);
    TEST_ASSERT_EQUAL_UINT16(1, out[idx].changes);  // one data change after first
    TEST_ASSERT_EQUAL_UINT8(0x22, out[idx].data[0]); // last data
    TEST_ASSERT_EQUAL_UINT16(1000, out[idx].first_seen_ms_ago); // 2.0s - 1.0s
    TEST_ASSERT_EQUAL_UINT16(500, out[idx].last_seen_ms_ago);   // 2.0s - 1.5s
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_collect_returns_frames_in_window);
    RUN_TEST(test_collect_excludes_frames_older_than_window);
    RUN_TEST(test_ring_overwrites_oldest_when_full);
    RUN_TEST(test_summarize_groups_by_channel_id);
    return UNITY_END();
}
```

- [ ] **Step 2: Add WsPretriggerRecord forward dependency**

`summarize()` returns `WsPretriggerRecord`, defined in Task 4. To keep Task 1 self-contained, define the struct in `ws_protocol.h` FIRST (do Task 4 Step 1 for `WsPretriggerRecord` before this test compiles), OR temporarily include the struct. Recommended order: implement `WsPretriggerRecord` in `ws_protocol.h` now (it is pure data):

```cpp
#pragma pack(push, 1)
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
#pragma pack(pop)
```

Add `#include "analyzer/ws_protocol.h"` to `pretrigger_buffer.h`.

- [ ] **Step 3: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_pretrigger_buffer`
Expected: FAIL — `pretrigger_buffer.h` not found / undefined `PretriggerBuffer`.

- [ ] **Step 4: Write header**

Create `src/analyzer/pretrigger_buffer.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"
#include "analyzer/ws_protocol.h"

class PretriggerBuffer {
public:
    void init(CapturedFrame *storage, size_t capacity);
    void push(const CapturedFrame &frame);
    size_t collect(uint64_t now_us, uint32_t window_us, CapturedFrame *out, size_t cap) const;
    size_t summarize(uint64_t now_us, uint32_t window_us, WsPretriggerRecord *out, size_t cap) const;

private:
    CapturedFrame *storage_ = nullptr;
    size_t capacity_ = 0;   // usable = capacity_ - 1
    size_t head_ = 0;       // next write
    size_t count_ = 0;      // current stored
};
```

- [ ] **Step 5: Write implementation**

Create `src/analyzer/pretrigger_buffer.cpp`:

```cpp
#include "analyzer/pretrigger_buffer.h"

void PretriggerBuffer::init(CapturedFrame *storage, size_t capacity) {
    storage_ = storage;
    capacity_ = capacity;
    head_ = 0;
    count_ = 0;
}

void PretriggerBuffer::push(const CapturedFrame &frame) {
    if (!storage_ || capacity_ == 0) return;
    storage_[head_] = frame;
    head_ = (head_ + 1) % capacity_;
    const size_t usable = capacity_ - 1;
    if (count_ < usable) ++count_;
}

size_t PretriggerBuffer::collect(uint64_t now_us, uint32_t window_us, CapturedFrame *out, size_t cap) const {
    if (!storage_ || count_ == 0) return 0;
    const uint64_t lo = now_us > window_us ? now_us - window_us : 0;
    size_t n = 0;
    // oldest index
    size_t idx = (head_ + capacity_ - count_) % capacity_;
    for (size_t i = 0; i < count_; ++i) {
        const CapturedFrame &f = storage_[idx];
        if (f.ts_us >= lo && f.ts_us <= now_us && n < cap) out[n++] = f;
        idx = (idx + 1) % capacity_;
    }
    return n;
}

size_t PretriggerBuffer::summarize(uint64_t now_us, uint32_t window_us, WsPretriggerRecord *out, size_t cap) const {
    if (!storage_ || count_ == 0) return 0;
    const uint64_t lo = now_us > window_us ? now_us - window_us : 0;
    size_t n = 0;
    size_t idx = (head_ + capacity_ - count_) % capacity_;
    for (size_t i = 0; i < count_; ++i) {
        const CapturedFrame &f = storage_[idx];
        idx = (idx + 1) % capacity_;
        if (f.ts_us < lo || f.ts_us > now_us) continue;
        // find existing summary
        WsPretriggerRecord *rec = nullptr;
        for (size_t j = 0; j < n; ++j)
            if (out[j].channel == f.channel && out[j].id == (uint16_t)f.id) { rec = &out[j]; break; }
        if (!rec) {
            if (n >= cap) continue;
            rec = &out[n++];
            rec->channel = f.channel;
            rec->id = (uint16_t)f.id;
            rec->frames = 0;
            rec->changes = 0;
            rec->dlc = f.dlc;
            for (int b = 0; b < 8; ++b) rec->data[b] = f.data[b];
            const uint64_t age = now_us - f.ts_us;
            rec->first_seen_ms_ago = age / 1000 > 65535 ? 65535 : (uint16_t)(age / 1000);
            rec->last_seen_ms_ago = rec->first_seen_ms_ago;
            rec->frames = 1;
            continue;
        }
        rec->frames = rec->frames < 65535 ? rec->frames + 1 : 65535;
        bool changed = rec->dlc != f.dlc;
        for (int b = 0; b < 8 && !changed; ++b) changed = rec->data[b] != f.data[b];
        if (changed) {
            rec->changes = rec->changes < 65535 ? rec->changes + 1 : 65535;
            rec->dlc = f.dlc;
            for (int b = 0; b < 8; ++b) rec->data[b] = f.data[b];
        }
        const uint64_t age = now_us - f.ts_us;
        rec->last_seen_ms_ago = age / 1000 > 65535 ? 65535 : (uint16_t)(age / 1000);
    }
    return n;
}
```

- [ ] **Step 6: Add to native build filter**

In `platformio.ini`, find the `[env:native]` `build_src_filter` and add:

```
    +<analyzer/pretrigger_buffer.cpp>
```

- [ ] **Step 7: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_pretrigger_buffer`
Expected: PASS — 4 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/analyzer/pretrigger_buffer.h src/analyzer/pretrigger_buffer.cpp test/test_pretrigger_buffer/test_pretrigger_buffer.cpp src/analyzer/ws_protocol.h platformio.ini
git commit -m "feat(analyzer): pre-trigger ring buffer with window summary"
```

---

## Task 2: SnapshotStore (TDD)

**Files:**
- Create: `src/analyzer/snapshot_store.h`
- Create: `src/analyzer/snapshot_store.cpp`
- Create: `test/test_snapshot_store/test_snapshot_store.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Write failing tests**

Create `test/test_snapshot_store/test_snapshot_store.cpp`:

```cpp
#include <unity.h>
#include "analyzer/snapshot_store.h"

static IdRecord records[kChannelCount * kStdIdCount];
static IdTable table;
static SnapshotRecord slotA[kChannelCount * kStdIdCount];
static SnapshotRecord slotB[kChannelCount * kStdIdCount];
static SnapshotStore store;

void setUp() { table.init(records); store.init(slotA, slotB); }
void tearDown() {}

static CapturedFrame frame(uint8_t ch, uint32_t id, uint8_t d0) {
    CapturedFrame f; f.channel = ch; f.id = id; f.dlc = 1; f.data[0] = d0; f.ts_us = 1000;
    return f;
}

void test_diff_reports_added_id() {
    store.capture(SnapshotSlot::A, table);
    table.update(frame(0, 0x120, 0x11));
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8];
    const size_t n = store.diff(out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x120, out[0].id);
    TEST_ASSERT_EQUAL_UINT8(SNAPSHOT_DIFF_ADDED, out[0].kind);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].dlc_a);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].dlc_b);
    TEST_ASSERT_EQUAL_UINT8(0x11, out[0].data_b[0]);
}

void test_diff_reports_removed_id() {
    table.update(frame(1, 0x220, 0x44));
    store.capture(SnapshotSlot::A, table);
    table.init(records);
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8];
    const size_t n = store.diff(out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x220, out[0].id);
    TEST_ASSERT_EQUAL_UINT8(SNAPSHOT_DIFF_REMOVED, out[0].kind);
}

void test_diff_reports_changed_data() {
    table.update(frame(0, 0x333, 0x01));
    store.capture(SnapshotSlot::A, table);
    table.update(frame(0, 0x333, 0x02));
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8];
    const size_t n = store.diff(out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(SNAPSHOT_DIFF_CHANGED, out[0].kind);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[0].data_a[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, out[0].data_b[0]);
}

void test_diff_omits_identical_records() {
    table.update(frame(0, 0x444, 0x99));
    store.capture(SnapshotSlot::A, table);
    store.capture(SnapshotSlot::B, table);
    SnapshotDiffRecord out[8];
    TEST_ASSERT_EQUAL_size_t(0, store.diff(out, 8));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_diff_reports_added_id);
    RUN_TEST(test_diff_reports_removed_id);
    RUN_TEST(test_diff_reports_changed_data);
    RUN_TEST(test_diff_omits_identical_records);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_snapshot_store`
Expected: FAIL — `snapshot_store.h` not found / undefined `SnapshotStore`.

- [ ] **Step 3: Create header**

Create `src/analyzer/snapshot_store.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer/id_table.h"

constexpr uint8_t SNAPSHOT_DIFF_ADDED = 1;
constexpr uint8_t SNAPSHOT_DIFF_REMOVED = 2;
constexpr uint8_t SNAPSHOT_DIFF_CHANGED = 3;

struct SnapshotRecord {
    bool present = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

struct SnapshotDiffRecord {
    uint8_t channel = 0;
    uint16_t id = 0;
    uint8_t kind = 0;
    uint8_t dlc_a = 0;
    uint8_t data_a[8] = {};
    uint8_t dlc_b = 0;
    uint8_t data_b[8] = {};
};

enum class SnapshotSlot : uint8_t { A = 0, B = 1 };

class SnapshotStore {
public:
    void init(SnapshotRecord *slotA, SnapshotRecord *slotB);
    void capture(SnapshotSlot slot, const IdTable &table);
    size_t diff(SnapshotDiffRecord *out, size_t cap) const;

private:
    SnapshotRecord *a_ = nullptr;
    SnapshotRecord *b_ = nullptr;
    static size_t key(uint8_t channel, uint32_t id) { return (size_t)channel * kStdIdCount + id; }
};
```

- [ ] **Step 4: Create implementation**

Create `src/analyzer/snapshot_store.cpp`:

```cpp
#include "analyzer/snapshot_store.h"
#include <cstring>

void SnapshotStore::init(SnapshotRecord *slotA, SnapshotRecord *slotB) {
    a_ = slotA;
    b_ = slotB;
    if (a_) memset(a_, 0, sizeof(SnapshotRecord) * kChannelCount * kStdIdCount);
    if (b_) memset(b_, 0, sizeof(SnapshotRecord) * kChannelCount * kStdIdCount);
}

void SnapshotStore::capture(SnapshotSlot slot, const IdTable &table) {
    SnapshotRecord *dst = slot == SnapshotSlot::A ? a_ : b_;
    if (!dst) return;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t id = 0; id < kStdIdCount; ++id) {
            const IdRecord &src = table.record(ch, id);
            SnapshotRecord &rec = dst[key(ch, id)];
            rec.present = src.present;
            rec.dlc = src.dlc;
            for (int i = 0; i < 8; ++i) rec.data[i] = src.data[i];
        }
    }
}

size_t SnapshotStore::diff(SnapshotDiffRecord *out, size_t cap) const {
    if (!a_ || !b_) return 0;
    size_t n = 0;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t id = 0; id < kStdIdCount; ++id) {
            const SnapshotRecord &ra = a_[key(ch, id)];
            const SnapshotRecord &rb = b_[key(ch, id)];
            uint8_t kind = 0;
            if (!ra.present && rb.present) kind = SNAPSHOT_DIFF_ADDED;
            else if (ra.present && !rb.present) kind = SNAPSHOT_DIFF_REMOVED;
            else if (ra.present && rb.present) {
                bool changed = ra.dlc != rb.dlc;
                for (int i = 0; i < 8 && !changed; ++i) changed = ra.data[i] != rb.data[i];
                if (changed) kind = SNAPSHOT_DIFF_CHANGED;
            }
            if (!kind || n >= cap) continue;
            SnapshotDiffRecord &dst = out[n++];
            dst.channel = ch;
            dst.id = (uint16_t)id;
            dst.kind = kind;
            dst.dlc_a = ra.dlc;
            dst.dlc_b = rb.dlc;
            for (int i = 0; i < 8; ++i) { dst.data_a[i] = ra.data[i]; dst.data_b[i] = rb.data[i]; }
        }
    }
    return n;
}
```

- [ ] **Step 5: Add to native build filter**

In `platformio.ini`, add:

```
    +<analyzer/snapshot_store.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_snapshot_store`
Expected: PASS — 4 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/analyzer/snapshot_store.h src/analyzer/snapshot_store.cpp test/test_snapshot_store/test_snapshot_store.cpp platformio.ini
git commit -m "feat(analyzer): add snapshot diff store"
```

---

## Task 3: LabelStore (TDD)

**Files:**
- Create: `src/analyzer/label_store.h`
- Create: `src/analyzer/label_store.cpp`
- Create: `test/test_label_store/test_label_store.cpp`
- Modify: `platformio.ini`

The label store keeps the in-memory table and a small persistence seam. The native build uses a no-op persistence backend; the firmware build uses Preferences (NVS). Persistence is behind `#if defined(ARDUINO)` so native tests link without NVS.

- [ ] **Step 1: Write failing tests**

Create `test/test_label_store/test_label_store.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "analyzer/label_store.h"

static LabelStore store;

void setUp() { store.begin(); }
void tearDown() {}

void test_upsert_adds_entry() {
    TEST_ASSERT_TRUE(store.upsert(0, 0x132, "volume"));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    const LabelEntry *e = store.entries();
    TEST_ASSERT_EQUAL_UINT8(0, e[0].channel);
    TEST_ASSERT_EQUAL_UINT16(0x132, e[0].id);
    TEST_ASSERT_EQUAL_STRING("volume", e[0].text);
}

void test_upsert_overwrites_same_key() {
    store.upsert(1, 0x200, "old");
    store.upsert(1, 0x200, "new");
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_STRING("new", store.entries()[0].text);
}

void test_empty_text_removes_entry() {
    store.upsert(0, 0x10, "x");
    TEST_ASSERT_TRUE(store.upsert(0, 0x10, ""));
    TEST_ASSERT_EQUAL_size_t(0, store.count());
}

void test_remove_entry() {
    store.upsert(0, 0x10, "a");
    store.upsert(0, 0x11, "b");
    TEST_ASSERT_TRUE(store.remove(0, 0x10));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_UINT16(0x11, store.entries()[0].id);
}

void test_capacity_limit_rejects_overflow() {
    char buf[8];
    for (int i = 0; i < 64; ++i) { snprintf(buf, sizeof(buf), "l%d", i); TEST_ASSERT_TRUE(store.upsert(0, i, buf)); }
    TEST_ASSERT_EQUAL_size_t(64, store.count());
    TEST_ASSERT_FALSE(store.upsert(0, 999, "overflow"));
    TEST_ASSERT_EQUAL_size_t(64, store.count());
}

void test_text_truncated_to_capacity() {
    store.upsert(0, 0x10, "0123456789012345678901234567890123456789");
    TEST_ASSERT_EQUAL_size_t(23, strlen(store.entries()[0].text)); // 24-byte buffer, 23 chars + NUL
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_upsert_adds_entry);
    RUN_TEST(test_upsert_overwrites_same_key);
    RUN_TEST(test_empty_text_removes_entry);
    RUN_TEST(test_remove_entry);
    RUN_TEST(test_capacity_limit_rejects_overflow);
    RUN_TEST(test_text_truncated_to_capacity);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_label_store`
Expected: FAIL — `label_store.h` not found.

- [ ] **Step 3: Create header**

Create `src/analyzer/label_store.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kMaxLabels = 64;
constexpr size_t kLabelTextLen = 24; // includes NUL

struct LabelEntry {
    uint8_t channel = 0;
    uint16_t id = 0;
    char text[kLabelTextLen] = {};
};

class LabelStore {
public:
    void begin();                          // load from NVS on firmware, clear on native
    bool upsert(uint8_t channel, uint16_t id, const char *text); // empty text removes
    bool remove(uint8_t channel, uint16_t id);
    const LabelEntry *entries() const { return entries_; }
    size_t count() const { return count_; }

private:
    int find(uint8_t channel, uint16_t id) const;
    void persist();                        // write blob to NVS on firmware, no-op native

    LabelEntry entries_[kMaxLabels];
    size_t count_ = 0;
};
```

- [ ] **Step 4: Create implementation**

Create `src/analyzer/label_store.cpp`:

```cpp
#include "analyzer/label_store.h"
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
namespace { Preferences g_prefs; }
#endif

void LabelStore::begin() {
    count_ = 0;
#if defined(ARDUINO)
    g_prefs.begin("analyzer", false);
    const size_t blob = g_prefs.getBytesLength("labels");
    if (blob > 0 && blob <= sizeof(entries_)) {
        g_prefs.getBytes("labels", entries_, blob);
        count_ = blob / sizeof(LabelEntry);
        if (count_ > kMaxLabels) count_ = kMaxLabels;
    }
#endif
}

int LabelStore::find(uint8_t channel, uint16_t id) const {
    for (size_t i = 0; i < count_; ++i)
        if (entries_[i].channel == channel && entries_[i].id == id) return (int)i;
    return -1;
}

bool LabelStore::upsert(uint8_t channel, uint16_t id, const char *text) {
    if (!text || text[0] == '\0') return remove(channel, id);
    int idx = find(channel, id);
    if (idx < 0) {
        if (count_ >= kMaxLabels) return false;
        idx = (int)count_++;
        entries_[idx].channel = channel;
        entries_[idx].id = id;
    }
    strncpy(entries_[idx].text, text, kLabelTextLen - 1);
    entries_[idx].text[kLabelTextLen - 1] = '\0';
    persist();
    return true;
}

bool LabelStore::remove(uint8_t channel, uint16_t id) {
    int idx = find(channel, id);
    if (idx < 0) return false;
    for (size_t i = (size_t)idx; i + 1 < count_; ++i) entries_[i] = entries_[i + 1];
    --count_;
    persist();
    return true;
}

void LabelStore::persist() {
#if defined(ARDUINO)
    g_prefs.putBytes("labels", entries_, count_ * sizeof(LabelEntry));
#endif
}
```

- [ ] **Step 5: Add to native build filter**

In `platformio.ini`, add:

```
    +<analyzer/label_store.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_label_store`
Expected: PASS — 6 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/analyzer/label_store.h src/analyzer/label_store.cpp test/test_label_store/test_label_store.cpp platformio.ini
git commit -m "feat(analyzer): NVS-backed ID label store"
```

---

## Task 4: WS 0x03 protocol builders (TDD)

**Files:**
- Modify: `src/analyzer/ws_protocol.h`
- Modify: `src/analyzer/ws_protocol.cpp`
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp`

`WsPretriggerRecord` was already added in Task 1. This task adds `WsDiffRecord`, subtype constants, and three builder functions that prepend a `type(0x03), subtype, count` header.

- [ ] **Step 1: Add structs and subtype constants to `ws_protocol.h`**

Inside the existing `#pragma pack(push, 1)` block (after `WsPretriggerRecord`), add:

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

After the `enum WsMsgType`, add:

```cpp
enum WsDiffSubtype : uint8_t {
    WS_DIFF_SNAPSHOT = 0x01,
    WS_DIFF_PRETRIGGER = 0x02,
    WS_DIFF_BASELINE = 0x03,
    WS_DIFF_LABELS = 0x04,
};

struct WsBaselineRecord {
    uint8_t channel;
    uint16_t id;
};
```

Note: put `WsBaselineRecord` inside the packed block too. Add builder declarations at the bottom of the header:

```cpp
size_t wsBuildSnapshotDiff(uint8_t *buf, size_t cap, const WsDiffRecord *recs, uint8_t count);
size_t wsBuildPretrigger(uint8_t *buf, size_t cap, const WsPretriggerRecord *recs, uint8_t count);
size_t wsBuildBaseline(uint8_t *buf, size_t cap, const WsBaselineRecord *recs, uint8_t count);
```

- [ ] **Step 2: Write failing tests**

Append to `test/test_ws_protocol/test_ws_protocol.cpp` (before `main`), and add RUN_TEST lines:

```cpp
void test_snapshot_diff_layout()
{
    WsDiffRecord rec{};
    rec.channel = 1; rec.id = 0x321; rec.kind = 3;
    rec.dlc_a = 2; rec.data_a[0] = 0xAA;
    rec.dlc_b = 2; rec.data_b[0] = 0xBB;
    uint8_t buf[128];
    const size_t n = wsBuildSnapshotDiff(buf, sizeof(buf), &rec, 1);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_SNAPSHOT, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsDiffRecord), n);
    const WsDiffRecord *out = reinterpret_cast<const WsDiffRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT16(0x321, out->id);
    TEST_ASSERT_EQUAL_UINT8(0xAA, out->data_a[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, out->data_b[0]);
}

void test_pretrigger_layout_and_cap()
{
    WsPretriggerRecord recs[4];
    memset(recs, 0, sizeof(recs));
    recs[0].id = 0x111; recs[0].frames = 7;
    uint8_t small[3 + sizeof(WsPretriggerRecord) + 1]; // room for 1
    const size_t n = wsBuildPretrigger(small, sizeof(small), recs, 4);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, small[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_PRETRIGGER, small[1]);
    TEST_ASSERT_EQUAL_UINT8(1, small[2]); // truncated to 1
    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsPretriggerRecord), n);
}

void test_baseline_layout()
{
    WsBaselineRecord recs[2];
    recs[0].channel = 0; recs[0].id = 0x100;
    recs[1].channel = 1; recs[1].id = 0x200;
    uint8_t buf[64];
    const size_t n = wsBuildBaseline(buf, sizeof(buf), recs, 2);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_DIFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_DIFF_BASELINE, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(2, buf[2]);
    const WsBaselineRecord *out = reinterpret_cast<const WsBaselineRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT16(0x200, out[1].id);
}
```

Add to `main`:

```cpp
    RUN_TEST(test_snapshot_diff_layout);
    RUN_TEST(test_pretrigger_layout_and_cap);
    RUN_TEST(test_baseline_layout);
```

Ensure `#include <cstring>` is present (it is).

- [ ] **Step 3: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol`
Expected: FAIL — undefined `wsBuildSnapshotDiff` etc.

- [ ] **Step 4: Implement builders in `ws_protocol.cpp`**

Append:

```cpp
static size_t buildDiffMsg(uint8_t *buf, size_t cap, uint8_t subtype,
                           const void *recs, size_t recSize, uint8_t count)
{
    if (cap < 3) return 0;
    size_t maxByCap = (cap - 3) / recSize;
    if (maxByCap > count) maxByCap = count;
    buf[0] = WS_MSG_DIFF;
    buf[1] = subtype;
    buf[2] = static_cast<uint8_t>(maxByCap);
    memcpy(buf + 3, recs, maxByCap * recSize);
    return 3 + maxByCap * recSize;
}

size_t wsBuildSnapshotDiff(uint8_t *buf, size_t cap, const WsDiffRecord *recs, uint8_t count)
{
    return buildDiffMsg(buf, cap, WS_DIFF_SNAPSHOT, recs, sizeof(WsDiffRecord), count);
}

size_t wsBuildPretrigger(uint8_t *buf, size_t cap, const WsPretriggerRecord *recs, uint8_t count)
{
    return buildDiffMsg(buf, cap, WS_DIFF_PRETRIGGER, recs, sizeof(WsPretriggerRecord), count);
}

size_t wsBuildBaseline(uint8_t *buf, size_t cap, const WsBaselineRecord *recs, uint8_t count)
{
    return buildDiffMsg(buf, cap, WS_DIFF_BASELINE, recs, sizeof(WsBaselineRecord), count);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol`
Expected: PASS — 6 tests pass (3 prior + 3 new).

- [ ] **Step 6: Commit**

```bash
git add src/analyzer/ws_protocol.h src/analyzer/ws_protocol.cpp test/test_ws_protocol/test_ws_protocol.cpp
git commit -m "feat(analyzer): add P3 diff/pretrigger/baseline WS builders"
```

---

## Task 5: Backend P3 wiring

**Files:**
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `src/analyzer/analyzer_web.cpp`
- Modify: `src/can_analyzer.cpp`
- Modify: `platformio.ini` (`[env:analyzer]` already includes `+<analyzer/>`, only native filter was handled earlier)

- [ ] **Step 1: Update web context signature**

In `src/analyzer/analyzer_web.h`, include new headers and change the context function:

```cpp
#pragma once
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/pretrigger_buffer.h"
#include "analyzer/snapshot_store.h"
#include "analyzer/label_store.h"

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels);
void analyzerWebBegin();
void analyzerWebLoop();
```

- [ ] **Step 2: Add globals in `analyzer_web.cpp`**

Near existing `g_queue/g_table/g_stats` globals:

```cpp
PretriggerBuffer *g_pretrigger = nullptr;
SnapshotStore *g_snapshots = nullptr;
LabelStore *g_labels = nullptr;
```

Update `analyzerWebSetContext()` to assign all six pointers.

- [ ] **Step 3: Feed PretriggerBuffer when draining queue**

In `drainQueueIntoTable()`, after `g_stats->noteRx(cap);` and before/after table update, add:

```cpp
        if (g_pretrigger)
            g_pretrigger->push(cap);
```

- [ ] **Step 4: Add batched send helpers**

In anonymous namespace in `analyzer_web.cpp`, add helpers:

```cpp
void sendSnapshotDiff()
{
    if (!g_snapshots || ws.count() == 0) return;
    static SnapshotDiffRecord diffs[64];
    static WsDiffRecord wire[64];
    static uint8_t buf[kPushBufBytes];
    size_t offset = 0;
    while (true) {
        const size_t n = g_snapshots->diff(diffs, 64); // first implementation computes from start each time
        if (n == 0) return;
        const size_t batch = n > 64 ? 64 : n;
        for (size_t i = 0; i < batch; ++i) {
            wire[i].channel = diffs[i + offset].channel;
            wire[i].id = diffs[i + offset].id;
            wire[i].kind = diffs[i + offset].kind;
            wire[i].dlc_a = diffs[i + offset].dlc_a;
            wire[i].dlc_b = diffs[i + offset].dlc_b;
            for (int b = 0; b < 8; ++b) { wire[i].data_a[b] = diffs[i + offset].data_a[b]; wire[i].data_b[b] = diffs[i + offset].data_b[b]; }
        }
        const size_t bytes = wsBuildSnapshotDiff(buf, sizeof(buf), wire, (uint8_t)batch);
        if (bytes) ws.binaryAll(buf, bytes);
        return;
    }
}
```

Then immediately simplify/fix the helper so it does not pretend to paginate but only sends first 64 diffs. This is acceptable for P3 because UI can show "first 64" and most operations produce few IDs. Use this final code instead of the loop above:

```cpp
void sendSnapshotDiff()
{
    if (!g_snapshots || ws.count() == 0) return;
    SnapshotDiffRecord diffs[64];
    WsDiffRecord wire[64];
    uint8_t buf[kPushBufBytes];
    const size_t n = g_snapshots->diff(diffs, 64);
    for (size_t i = 0; i < n; ++i) {
        wire[i].channel = diffs[i].channel;
        wire[i].id = diffs[i].id;
        wire[i].kind = diffs[i].kind;
        wire[i].dlc_a = diffs[i].dlc_a;
        wire[i].dlc_b = diffs[i].dlc_b;
        for (int b = 0; b < 8; ++b) { wire[i].data_a[b] = diffs[i].data_a[b]; wire[i].data_b[b] = diffs[i].data_b[b]; }
    }
    const size_t bytes = wsBuildSnapshotDiff(buf, sizeof(buf), wire, (uint8_t)n);
    if (bytes) ws.binaryAll(buf, bytes);
}

void sendPretrigger()
{
    if (!g_pretrigger || ws.count() == 0) return;
    WsPretriggerRecord recs[64];
    uint8_t buf[kPushBufBytes];
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    const size_t n = g_pretrigger->summarize(nowUs, 5000000UL, recs, 64);
    const size_t bytes = wsBuildPretrigger(buf, sizeof(buf), recs, (uint8_t)n);
    if (bytes) ws.binaryAll(buf, bytes);
}

void sendBaseline()
{
    if (!g_table || ws.count() == 0) return;
    WsBaselineRecord recs[64];
    uint8_t buf[kPushBufBytes];
    size_t n = 0;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t id = 0; id < kStdIdCount; ++id) {
            if (!g_table->record(ch, id).present) continue;
            recs[n++] = WsBaselineRecord{ch, (uint16_t)id};
            if (n == 64) {
                const size_t bytes = wsBuildBaseline(buf, sizeof(buf), recs, (uint8_t)n);
                if (bytes) ws.binaryAll(buf, bytes);
                n = 0;
            }
        }
    }
    if (n) {
        const size_t bytes = wsBuildBaseline(buf, sizeof(buf), recs, (uint8_t)n);
        if (bytes) ws.binaryAll(buf, bytes);
    }
}
```

- [ ] **Step 5: Handle new JSON commands**

In `handleCommand()` add after existing tx commands:

```cpp
    if (strcmp(cmd, "snapshot") == 0)
    {
        const char *slot = doc["slot"] | "A";
        if (g_snapshots && g_table)
            g_snapshots->capture((slot[0] == 'B' || slot[0] == 'b') ? SnapshotSlot::B : SnapshotSlot::A, *g_table);
        return;
    }
    if (strcmp(cmd, "diff") == 0)
    {
        sendSnapshotDiff();
        return;
    }
    if (strcmp(cmd, "baseline") == 0)
    {
        sendBaseline();
        return;
    }
    if (strcmp(cmd, "mark") == 0)
    {
        sendPretrigger();
        return;
    }
    if (strcmp(cmd, "label_set") == 0)
    {
        const char *ch = doc["ch"] | "A";
        const uint8_t channel = (ch[0] == 'B' || ch[0] == 'b') ? 1 : 0;
        const uint16_t id = doc["id"] | 0;
        const char *text = doc["text"] | "";
        if (g_labels) g_labels->upsert(channel, id, text);
        return;
    }
    if (strcmp(cmd, "label_delete") == 0)
    {
        const char *ch = doc["ch"] | "A";
        const uint8_t channel = (ch[0] == 'B' || ch[0] == 'b') ? 1 : 0;
        const uint16_t id = doc["id"] | 0;
        if (g_labels) g_labels->remove(channel, id);
        return;
    }
```

- [ ] **Step 6: Add `/api/labels` endpoint**

In `analyzerWebBegin()`, before `serveStatic`, add:

```cpp
    server.on("/api/labels", HTTP_GET, [](AsyncWebServerRequest *request) {
        String out = "[";
        if (g_labels) {
            const LabelEntry *entries = g_labels->entries();
            for (size_t i = 0; i < g_labels->count(); ++i) {
                if (i) out += ",";
                out += "{\"ch\":\"";
                out += entries[i].channel == 1 ? "B" : "A";
                out += "\",\"id\":";
                out += String(entries[i].id);
                out += ",\"text\":";
                out += String('"') + String(entries[i].text) + String('"');
                out += "}";
            }
        }
        out += "]";
        request->send(200, "application/json", out);
    });
```

Do not add complex JSON escaping in P3 plan; frontend should restrict label input to printable text without quotes/backslashes before sending. If implementation wants robust escaping, use ArduinoJson instead.

- [ ] **Step 7: Allocate and wire P3 state in `src/can_analyzer.cpp`**

Read the existing file first. Add includes:

```cpp
#include "analyzer/pretrigger_buffer.h"
#include "analyzer/snapshot_store.h"
#include "analyzer/label_store.h"
```

Add globals near existing table/stat globals:

```cpp
constexpr size_t kPretriggerCapacity = 16384;
CapturedFrame *g_pretriggerStorage = nullptr;
PretriggerBuffer g_pretrigger;
SnapshotRecord *g_snapshotA = nullptr;
SnapshotRecord *g_snapshotB = nullptr;
SnapshotStore g_snapshots;
LabelStore g_labels;
```

In setup after PSRAM is initialized/available and after IdTable storage allocation pattern, allocate:

```cpp
g_pretriggerStorage = static_cast<CapturedFrame *>(ps_malloc(sizeof(CapturedFrame) * kPretriggerCapacity));
if (g_pretriggerStorage) g_pretrigger.init(g_pretriggerStorage, kPretriggerCapacity);

g_snapshotA = static_cast<SnapshotRecord *>(ps_malloc(sizeof(SnapshotRecord) * kChannelCount * kStdIdCount));
g_snapshotB = static_cast<SnapshotRecord *>(ps_malloc(sizeof(SnapshotRecord) * kChannelCount * kStdIdCount));
if (g_snapshotA && g_snapshotB) g_snapshots.init(g_snapshotA, g_snapshotB);

g_labels.begin();
analyzerWebSetContext(&g_queue, &g_table, &g_stats,
                      g_pretriggerStorage ? &g_pretrigger : nullptr,
                      (g_snapshotA && g_snapshotB) ? &g_snapshots : nullptr,
                      &g_labels);
```

Adapt names to existing globals in `can_analyzer.cpp`.

- [ ] **Step 8: Build firmware**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer`
Expected: SUCCESS.

- [ ] **Step 9: Commit**

```bash
git add src/analyzer/analyzer_web.h src/analyzer/analyzer_web.cpp src/can_analyzer.cpp
git commit -m "feat(analyzer): wire P3 backend commands and state"
```

---

## Task 6: Frontend P3 UI and local filtering

**Files:**
- Modify: `data/analyzer/index.html`
- Modify: `data/analyzer/style.css`
- Modify: `data/analyzer/app.js`

- [ ] **Step 1: Add P3 controls to `index.html`**

In the existing toolbar/control area, add a new section:

```html
<section class="panel p3-panel">
  <h2>对比找包</h2>
  <div class="toolbar">
    <button id="baselineBtn">基线拉黑</button>
    <button id="snapABtn">拍 A</button>
    <button id="snapBBtn">拍 B</button>
    <button id="diffBtn">Diff</button>
    <button id="markBtn">标记/回看 5s</button>
  </div>
  <div class="toolbar">
    <select id="channelFilter"><option value="all">A+B</option><option value="0">A</option><option value="1">B</option></select>
    <input id="idFilter" placeholder="ID 如 132" />
    <input id="rangeFrom" placeholder="From" />
    <input id="rangeTo" placeholder="To" />
    <input id="searchBox" placeholder="搜索 ID/Label" />
    <label><input id="whitelistOnly" type="checkbox" /> 只看白名单</label>
  </div>
  <div id="p3Results" class="p3-results"></div>
</section>
```

- [ ] **Step 2: Add styles to `style.css`**

Append:

```css
.p3-panel { margin: 12px 0; padding: 10px; border: 1px solid #263445; border-radius: 8px; }
.p3-results { margin-top: 8px; max-height: 260px; overflow: auto; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
.p3-row { display: grid; grid-template-columns: 42px 70px 90px 1fr; gap: 8px; padding: 3px 0; border-bottom: 1px solid #1b2633; }
.p3-row.added { color: #4ade80; }
.p3-row.removed { color: #f87171; }
.p3-row.changed { color: #facc15; }
.row-filtered { display: none; }
.label-edit { width: 90px; }
.badge { border: 1px solid #41566f; border-radius: 4px; padding: 0 4px; margin-left: 4px; color: #93c5fd; }
```

- [ ] **Step 3: Add frontend state and helpers to `app.js`**

Near existing globals, add:

```js
const hidden = new Set();
const whitelist = new Set();
const labels = new Map();
const p3Results = document.getElementById('p3Results');
const channelFilter = document.getElementById('channelFilter');
const idFilter = document.getElementById('idFilter');
const rangeFrom = document.getElementById('rangeFrom');
const rangeTo = document.getElementById('rangeTo');
const searchBox = document.getElementById('searchBox');
const whitelistOnly = document.getElementById('whitelistOnly');
const keyOf = (ch, id) => `${ch}:${id}`;
const labelOf = (ch, id) => labels.get(keyOf(ch, id)) || '';
```

Add:

```js
function parseIdText(s) {
  s = (s || '').trim();
  if (!s) return null;
  return Number.parseInt(s.replace(/^0x/i, ''), 16);
}

function passesP3Filter(rec) {
  const k = keyOf(rec.ch, rec.id);
  if (hidden.has(k)) return false;
  if (whitelistOnly.checked && !whitelist.has(k)) return false;
  if (channelFilter.value !== 'all' && String(rec.ch) !== channelFilter.value) return false;
  const exact = parseIdText(idFilter.value);
  if (exact !== null && rec.id !== exact) return false;
  const lo = parseIdText(rangeFrom.value);
  const hi = parseIdText(rangeTo.value);
  if (lo !== null && rec.id < lo) return false;
  if (hi !== null && rec.id > hi) return false;
  const q = searchBox.value.trim().toLowerCase();
  if (q) {
    const idText = ('0x' + hex(rec.id, 3)).toLowerCase();
    const lab = labelOf(rec.ch, rec.id).toLowerCase();
    if (!idText.includes(q) && !lab.includes(q)) return false;
  }
  return true;
}
```

- [ ] **Step 4: Apply filters in `paintRecord()`**

Replace the existing hidden toggle logic with:

```js
const filtered = !passesP3Filter(rec);
tr.classList.toggle('hidden', isStatic(rec) || filtered);
pair.bit.classList.toggle('hidden', pair.bit.classList.contains('hidden') || isStatic(rec) || filtered);
```

Also add label display near ID:

```js
const label = labelOf(rec.ch, rec.id);
c[0].innerHTML = '0x' + hex(rec.id, 3) + (label ? `<span class="badge">${label}</span>` : '');
```

- [ ] **Step 5: Add control event handlers**

After WebSocket creation is available, make `sendCmd(obj)` use the current socket. If code currently scopes `ws` inside `connect()`, add a global `let socket = null;` and set `socket = ws;` in `connect()`.

Add:

```js
function sendCmd(obj) { if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(obj)); }

document.getElementById('baselineBtn').onclick = () => sendCmd({cmd:'baseline'});
document.getElementById('snapABtn').onclick = () => sendCmd({cmd:'snapshot', slot:'A'});
document.getElementById('snapBBtn').onclick = () => sendCmd({cmd:'snapshot', slot:'B'});
document.getElementById('diffBtn').onclick = () => sendCmd({cmd:'diff'});
document.getElementById('markBtn').onclick = () => sendCmd({cmd:'mark'});
for (const el of [channelFilter, idFilter, rangeFrom, rangeTo, searchBox, whitelistOnly]) el.oninput = repaintAll;
```

- [ ] **Step 6: Parse P3 0x03 messages**

Add to `app.js`:

```js
function parseP3(buf) {
  if (buf.byteLength < 3) return;
  const dv = new DataView(buf);
  const subtype = dv.getUint8(1);
  const count = dv.getUint8(2);
  let o = 3;
  if (subtype === 0x01) { // snapshot diff
    const rows = [];
    for (let i = 0; i < count && o + 22 <= buf.byteLength; i++) {
      const ch = dv.getUint8(o); o += 1;
      const id = dv.getUint16(o, true); o += 2;
      const kind = dv.getUint8(o); o += 1;
      const dlcA = dv.getUint8(o); o += 1;
      const a = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
      const dlcB = dv.getUint8(o); o += 1;
      const b = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
      const name = kind === 1 ? 'added' : kind === 2 ? 'removed' : 'changed';
      rows.push(`<div class="p3-row ${name}"><span>${ch ? 'B' : 'A'}</span><span>0x${hex(id,3)}</span><span>${name}</span><span>A:${a.slice(0,dlcA).map(x=>hex(x,2)).join(' ')} → B:${b.slice(0,dlcB).map(x=>hex(x,2)).join(' ')}</span></div>`);
    }
    p3Results.innerHTML = rows.join('') || '<div>无差异</div>';
    return;
  }
  if (subtype === 0x02) { // pretrigger
    const rows = [];
    for (let i = 0; i < count && o + 19 <= buf.byteLength; i++) {
      const ch = dv.getUint8(o); o += 1;
      const id = dv.getUint16(o, true); o += 2;
      const first = dv.getUint16(o, true); o += 2;
      const last = dv.getUint16(o, true); o += 2;
      const frames = dv.getUint16(o, true); o += 2;
      const changes = dv.getUint16(o, true); o += 2;
      const dlc = dv.getUint8(o); o += 1;
      const data = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
      rows.push(`<div class="p3-row changed"><span>${ch ? 'B' : 'A'}</span><span>0x${hex(id,3)}</span><span>${changes}chg/${frames}frm</span><span>${last}-${first}ms ago ${data.slice(0,dlc).map(x=>hex(x,2)).join(' ')}</span></div>`);
    }
    p3Results.innerHTML = rows.join('') || '<div>最近 5 秒无活动</div>';
    return;
  }
  if (subtype === 0x03) { // baseline
    for (let i = 0; i < count && o + 3 <= buf.byteLength; i++) {
      const ch = dv.getUint8(o); o += 1;
      const id = dv.getUint16(o, true); o += 2;
      hidden.add(keyOf(ch, id));
    }
    repaintAll();
    p3Results.textContent = `已加入黑名单 ${hidden.size} 个 ID`;
  }
}
```

In `ws.onmessage`, add:

```js
if (type === 0x03) parseP3(ev.data);
```

- [ ] **Step 7: Load labels and support inline label editing**

Add on startup:

```js
async function loadLabels() {
  const res = await fetch('/api/labels');
  const arr = await res.json();
  labels.clear();
  for (const e of arr) labels.set(keyOf(e.ch === 'B' ? 1 : 0, e.id), e.text);
  repaintAll();
}
loadLabels().catch(() => {});
```

For P3, implement label editing using prompt on row double-click:

```js
tr.ondblclick = (ev) => {
  ev.stopPropagation();
  const text = prompt('Label for ' + (rec.ch ? 'B' : 'A') + ':0x' + hex(rec.id,3), labelOf(rec.ch, rec.id));
  if (text === null) return;
  labels.set(keyOf(rec.ch, rec.id), text);
  sendCmd({cmd:'label_set', ch: rec.ch ? 'B' : 'A', id: rec.id, text});
  repaintAll();
};
```

Keep existing single-click bit expansion working by using `onclick` for bit toggle and `ondblclick` for label edit.

- [ ] **Step 8: Build LittleFS**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs`
Expected: SUCCESS and output lists only `/app.js`, `/index.html`, `/style.css`.

- [ ] **Step 9: Commit**

```bash
git add data/analyzer/index.html data/analyzer/style.css data/analyzer/app.js
git commit -m "feat(analyzer): add P3 compare-and-filter UI"
```

---

## Task 7: Full verification and review

**Files:**
- Read/verify: all P3 files

- [ ] **Step 1: Run all native tests**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native`
Expected: PASS — existing 16 tests plus new P3 tests all pass.

- [ ] **Step 2: Build firmware**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer`
Expected: SUCCESS.

- [ ] **Step 3: Build LittleFS image**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs`
Expected: SUCCESS and file list contains only `/app.js`, `/index.html`, `/style.css`.

- [ ] **Step 4: Review against spec**

Check `docs/superpowers/specs/2026-06-13-can-analyzer-p3-design.md`:

- PretriggerBuffer exists and is fed only from Core1 queue drain.
- Snapshot A/B diff reports added/removed/changed using current dlc+data.
- Baseline command returns present `(channel,id)` IDs to frontend.
- Frontend filtering is local and does not block backend push.
- LabelStore stores 64 entries and uses NVS only on firmware.

- [ ] **Step 5: Run code review agent**

Use `superpowers:code-reviewer` with this prompt:

```
Review CAN analyzer P3 implementation against docs/superpowers/specs/2026-06-13-can-analyzer-p3-design.md and docs/superpowers/plans/2026-06-13-can-analyzer-p3.md. Focus on Core0/Core1 safety, P3 WebSocket binary layout vs frontend offsets, PSRAM allocation failure behavior, LabelStore persistence safety, and frontend filtering correctness. Report Critical/Important/Minor. Do not edit files.
```

- [ ] **Step 6: Fix any Critical/Important review items with TDD**

For each item, write or update a failing test first, run it red, implement minimal fix, run green. Commit each logical fix with a concrete message naming the fixed behavior, for example:

```bash
git add src/analyzer/ws_protocol.h src/analyzer/ws_protocol.cpp test/test_ws_protocol/test_ws_protocol.cpp
git commit -m "fix(analyzer): align P3 websocket record sizes"
```

- [ ] **Step 7: Final verification**

Run again after fixes:

```bash
find . -name '._*' -delete
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Expected: all SUCCESS / all tests pass.

- [ ] **Step 8: Update memory**

Update `/Users/csk/.cac/envs/gpt/.claude/projects/-Volumes-csk-other-esp32/memory/project_can_analyzer.md` with P3 status, verification evidence, and next P4 scope.

- [ ] **Step 9: Complete branch**

Use `superpowers:finishing-a-development-branch`. Recommended local workflow: merge `worktree-can-analyzer-p3` back to `main` after final verification, preserving unrelated dirty state in the main worktree.

---

## Implementation Notes for Subagents

- Work only inside `/Volumes/csk/other/esp32/.claude/worktrees/can-analyzer-p3` unless explicitly told otherwise.
- Always run `find . -name '._*' -delete` before PlatformIO commands because `/Volumes` can create AppleDouble files that break PlatformIO source discovery or pollute LittleFS.
- Use TDD exactly: write failing test, observe RED, implement minimal code, observe GREEN, commit.
- Do not alter `.pio/libdeps/*/BLEOTA`, `.claude/`, or `Can分析功能开发计划.md`; those are unrelated existing dirty state in the main repo.
- Do not implement TX auth in P3; it is a known security follow-up unless the user changes scope.

