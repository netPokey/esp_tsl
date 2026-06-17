# CAN 分析仪 P4 波形与信号 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a P4 Signal Workbench that lets users define byte/16-bit signals, view short-window charts, import/export full signal JSON, save a small common subset on-device, and receive mux/counter/checksum candidate hints without auto-accepting them.

**Architecture:** Keep the existing P2/P3 raw-frame pipeline intact. Add a small backend P4 layer for watched-ID sample windows, common-signal persistence, and candidate generation, while the frontend remains the primary workspace for signal definitions, decoding, charting, and full JSON import/export.

**Tech Stack:** Arduino + PlatformIO, ESPAsyncWebServer/WebSocket, ArduinoJson, Preferences(NVS), LittleFS frontend, Unity native tests.

---

## File Structure

- Create: `src/analyzer/signal_codec.h/.cpp` — shared bit extraction / endian / signed / scale helpers and C++ `SignalSpec` model for tests + backend hint logic.
- Create: `test/test_signal_codec/test_signal_codec.cpp` — native tests for 8/16-bit decode, endian, signedness, scale/offset, and mux gating.
- Create: `src/analyzer/signal_window.h/.cpp` — watched `(channel,id)` short sample windows backed by fixed storage.
- Create: `test/test_signal_window/test_signal_window.cpp` — native tests for watch/unwatch, capped windows, ordering, and sample export.
- Create: `src/analyzer/signal_hints.h/.cpp` — mux/counter/checksum candidate scoring from short raw-frame windows.
- Create: `test/test_signal_hints/test_signal_hints.cpp` — native tests for true-positive candidates and obvious false-positive suppression.
- Create: `src/analyzer/common_signal_store.h/.cpp` — small fixed-size on-device store for common `SignalSpec` items.
- Create: `test/test_common_signal_store/test_common_signal_store.cpp` — native tests for serialization seam, capacity, invalid data rejection.
- Modify: `src/analyzer/ws_protocol.h/.cpp` — add a new P4 message family for watched samples and hint records.
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp` — add record layout and truncation tests for P4 protocol builders.
- Modify: `src/analyzer/analyzer_web.h/.cpp` — wire watch commands, hint requests, common-signal HTTP endpoints, and P4 push path.
- Modify: `src/can_analyzer.cpp` — allocate PSRAM / static storage for P4 backend state and pass contexts into web layer.
- Modify: `platformio.ini` — include new `.cpp` files in `[env:native]`.
- Modify: `data/analyzer/index.html`, `data/analyzer/style.css`, `data/analyzer/app.js` — add Signal Workbench, local signal workspace, import/export, common-save/load, and chart rendering.

---

## Task 1: Signal codec and spec model (TDD)

**Files:**
- Create: `src/analyzer/signal_codec.h`
- Create: `src/analyzer/signal_codec.cpp`
- Create: `test/test_signal_codec/test_signal_codec.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Write the failing test**

Create `test/test_signal_codec/test_signal_codec.cpp`:

```cpp
#include <unity.h>
#include "analyzer/signal_codec.h"

static RawSample sample(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    RawSample s{};
    s.dlc = 4;
    s.data[0] = d0;
    s.data[1] = d1;
    s.data[2] = d2;
    s.data[3] = d3;
    return s;
}

void setUp() {}
void tearDown() {}

void test_decode_uint8_intel() {
    const SignalSpec spec{0, 0x123, 0, 8, SignalEndian::Intel, false, 1.0f, 0.0f, false, 0, 0, 0};
    const DecodeResult out = decodeSignalValue(spec, sample(0x12, 0x34, 0, 0));
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_EQUAL_UINT32(0x12, out.raw);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0x12, out.physical);
}

void test_decode_uint16_intel() {
    const SignalSpec spec{0, 0x123, 8, 16, SignalEndian::Intel, false, 1.0f, 0.0f, false, 0, 0, 0};
    const DecodeResult out = decodeSignalValue(spec, sample(0x00, 0x34, 0x12, 0));
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_EQUAL_UINT32(0x1234, out.raw);
}

void test_decode_uint16_motorola() {
    const SignalSpec spec{0, 0x123, 8, 16, SignalEndian::Motorola, false, 1.0f, 0.0f, false, 0, 0, 0};
    const DecodeResult out = decodeSignalValue(spec, sample(0x00, 0x12, 0x34, 0));
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_EQUAL_UINT32(0x1234, out.raw);
}

void test_decode_signed_and_scaled() {
    const SignalSpec spec{0, 0x123, 0, 8, SignalEndian::Intel, true, 0.5f, -10.0f, false, 0, 0, 0};
    const DecodeResult out = decodeSignalValue(spec, sample(0xF6, 0, 0, 0));
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_EQUAL_INT32(-10, out.signed_raw);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -15.0f, out.physical);
}

void test_decode_rejects_mux_mismatch() {
    SignalSpec spec{0, 0x123, 8, 8, SignalEndian::Intel, false, 1.0f, 0.0f, true, 0, 8, 0x02};
    const DecodeResult miss = decodeSignalValue(spec, sample(0x01, 0x99, 0, 0));
    TEST_ASSERT_FALSE(miss.valid);
    const DecodeResult hit = decodeSignalValue(spec, sample(0x02, 0x99, 0, 0));
    TEST_ASSERT_TRUE(hit.valid);
    TEST_ASSERT_EQUAL_UINT32(0x99, hit.raw);
}

void test_decode_rejects_out_of_dlc_range() {
    const SignalSpec spec{0, 0x123, 24, 16, SignalEndian::Intel, false, 1.0f, 0.0f, false, 0, 0, 0};
    RawSample s{};
    s.dlc = 2;
    s.data[0] = 0x11;
    s.data[1] = 0x22;
    const DecodeResult out = decodeSignalValue(spec, s);
    TEST_ASSERT_FALSE(out.valid);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_uint8_intel);
    RUN_TEST(test_decode_uint16_intel);
    RUN_TEST(test_decode_uint16_motorola);
    RUN_TEST(test_decode_signed_and_scaled);
    RUN_TEST(test_decode_rejects_mux_mismatch);
    RUN_TEST(test_decode_rejects_out_of_dlc_range);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_signal_codec`
Expected: FAIL — `signal_codec.h` not found / undefined `decodeSignalValue`.

- [ ] **Step 3: Write header**

Create `src/analyzer/signal_codec.h`:

```cpp
#pragma once
#include <cstdint>

struct RawSample
{
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

enum class SignalEndian : uint8_t
{
    Intel = 0,
    Motorola = 1,
};

struct SignalSpec
{
    uint8_t channel = 0;
    uint16_t id = 0;
    uint8_t start_bit = 0;
    uint8_t bit_length = 0;
    SignalEndian endian = SignalEndian::Intel;
    bool is_signed = false;
    float scale = 1.0f;
    float offset = 0.0f;
    bool has_mux = false;
    uint8_t mux_start_bit = 0;
    uint8_t mux_bit_length = 0;
    uint32_t mux_value = 0;
};

struct DecodeResult
{
    bool valid = false;
    uint32_t raw = 0;
    int32_t signed_raw = 0;
    float physical = 0.0f;
};

DecodeResult decodeSignalValue(const SignalSpec &spec, const RawSample &sample);
uint32_t extractSignalBits(const RawSample &sample, uint8_t start_bit, uint8_t bit_length, SignalEndian endian, bool &ok);
```

- [ ] **Step 4: Write minimal implementation**

Create `src/analyzer/signal_codec.cpp`:

```cpp
#include "analyzer/signal_codec.h"

namespace
{
bool sampleHasBits(const RawSample &sample, uint8_t start_bit, uint8_t bit_length)
{
    if (bit_length == 0 || bit_length > 32)
        return false;
    const uint16_t end_bit = static_cast<uint16_t>(start_bit) + static_cast<uint16_t>(bit_length);
    return end_bit <= static_cast<uint16_t>(sample.dlc) * 8u;
}

int32_t signExtend(uint32_t raw, uint8_t bit_length)
{
    if (bit_length == 0 || bit_length >= 32)
        return static_cast<int32_t>(raw);
    const uint32_t sign = 1u << (bit_length - 1);
    const uint32_t mask = (1u << bit_length) - 1u;
    raw &= mask;
    return static_cast<int32_t>((raw ^ sign) - sign);
}
}

uint32_t extractSignalBits(const RawSample &sample, uint8_t start_bit, uint8_t bit_length, SignalEndian endian, bool &ok)
{
    ok = sampleHasBits(sample, start_bit, bit_length);
    if (!ok)
        return 0;

    uint32_t value = 0;
    if (endian == SignalEndian::Intel)
    {
        for (uint8_t i = 0; i < bit_length; ++i)
        {
            const uint16_t bit = start_bit + i;
            const uint8_t byte = bit / 8;
            const uint8_t shift = bit % 8;
            value |= static_cast<uint32_t>((sample.data[byte] >> shift) & 1u) << i;
        }
        return value;
    }

    for (uint8_t i = 0; i < bit_length; ++i)
    {
        const uint16_t bit = start_bit + i;
        const uint8_t byte = bit / 8;
        const uint8_t shift = static_cast<uint8_t>(7u - (bit % 8u));
        value = (value << 1u) | ((sample.data[byte] >> shift) & 1u);
    }
    return value;
}

DecodeResult decodeSignalValue(const SignalSpec &spec, const RawSample &sample)
{
    DecodeResult out{};
    if (spec.has_mux)
    {
        bool mux_ok = false;
        const uint32_t mux = extractSignalBits(sample, spec.mux_start_bit, spec.mux_bit_length, SignalEndian::Intel, mux_ok);
        if (!mux_ok || mux != spec.mux_value)
            return out;
    }

    bool ok = false;
    out.raw = extractSignalBits(sample, spec.start_bit, spec.bit_length, spec.endian, ok);
    if (!ok)
        return out;

    out.valid = true;
    out.signed_raw = spec.is_signed ? signExtend(out.raw, spec.bit_length)
                                    : static_cast<int32_t>(out.raw);
    out.physical = static_cast<float>(out.signed_raw) * spec.scale + spec.offset;
    return out;
}
```

- [ ] **Step 5: Add to native build filter**

In `platformio.ini` `[env:native]` `build_src_filter`, add:

```ini
    +<analyzer/signal_codec.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_signal_codec`
Expected: PASS — 6 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/analyzer/signal_codec.h src/analyzer/signal_codec.cpp test/test_signal_codec/test_signal_codec.cpp platformio.ini
git commit -m "feat(analyzer): add P4 signal codec primitives"
```

---

## Task 2: Watched signal sample windows (TDD)

**Files:**
- Create: `src/analyzer/signal_window.h`
- Create: `src/analyzer/signal_window.cpp`
- Create: `test/test_signal_window/test_signal_window.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Write the failing test**

Create `test/test_signal_window/test_signal_window.cpp`:

```cpp
#include <unity.h>
#include "analyzer/signal_window.h"

static WatchedSignalWindow windows;
static WindowSlot slots[2];

static CapturedFrame frame(uint8_t ch, uint16_t id, uint64_t ts, uint8_t d0)
{
    CapturedFrame f{};
    f.channel = ch;
    f.id = id;
    f.ts_us = ts;
    f.dlc = 1;
    f.data[0] = d0;
    return f;
}

void setUp() { windows.init(slots, 2, 4); }
void tearDown() {}

void test_watch_allocates_slot() {
    TEST_ASSERT_TRUE(windows.watch(0, 0x120));
    TEST_ASSERT_TRUE(windows.isWatched(0, 0x120));
}

void test_unwatch_clears_slot() {
    windows.watch(0, 0x120);
    windows.unwatch(0, 0x120);
    TEST_ASSERT_FALSE(windows.isWatched(0, 0x120));
}

void test_push_routes_only_watched_ids() {
    windows.watch(0, 0x120);
    windows.push(frame(0, 0x120, 1000, 0x11));
    windows.push(frame(0, 0x121, 2000, 0x22));
    RawSamplePoint out[4];
    const size_t n = windows.copySamples(0, 0x120, out, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT64(1000, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT8(0x11, out[0].data[0]);
}

void test_ring_keeps_latest_samples() {
    windows.watch(1, 0x220);
    windows.push(frame(1, 0x220, 1000, 1));
    windows.push(frame(1, 0x220, 2000, 2));
    windows.push(frame(1, 0x220, 3000, 3));
    windows.push(frame(1, 0x220, 4000, 4));
    windows.push(frame(1, 0x220, 5000, 5));
    RawSamplePoint out[4];
    const size_t n = windows.copySamples(1, 0x220, out, 4);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_UINT64(2000, out[0].ts_us);
    TEST_ASSERT_EQUAL_UINT8(5, out[3].data[0]);
}

void test_capacity_rejects_extra_watch() {
    TEST_ASSERT_TRUE(windows.watch(0, 0x100));
    TEST_ASSERT_TRUE(windows.watch(1, 0x200));
    TEST_ASSERT_FALSE(windows.watch(0, 0x300));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_watch_allocates_slot);
    RUN_TEST(test_unwatch_clears_slot);
    RUN_TEST(test_push_routes_only_watched_ids);
    RUN_TEST(test_ring_keeps_latest_samples);
    RUN_TEST(test_capacity_rejects_extra_watch);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_signal_window`
Expected: FAIL — `signal_window.h` not found.

- [ ] **Step 3: Write header**

Create `src/analyzer/signal_window.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer/analyzer_types.h"

struct RawSamplePoint
{
    uint64_t ts_us = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

struct WindowSlot
{
    bool active = false;
    uint8_t channel = 0;
    uint16_t id = 0;
    size_t head = 0;
    size_t count = 0;
    RawSamplePoint *samples = nullptr;
};

class WatchedSignalWindow
{
public:
    void init(WindowSlot *slots, size_t slot_count, size_t samples_per_slot);
    bool watch(uint8_t channel, uint16_t id);
    void unwatch(uint8_t channel, uint16_t id);
    bool isWatched(uint8_t channel, uint16_t id) const;
    void push(const CapturedFrame &frame);
    size_t copySamples(uint8_t channel, uint16_t id, RawSamplePoint *out, size_t cap) const;

private:
    WindowSlot *slots_ = nullptr;
    size_t slot_count_ = 0;
    size_t samples_per_slot_ = 0;

    WindowSlot *find(uint8_t channel, uint16_t id);
    const WindowSlot *find(uint8_t channel, uint16_t id) const;
};
```

- [ ] **Step 4: Write minimal implementation**

Create `src/analyzer/signal_window.cpp`:

```cpp
#include "analyzer/signal_window.h"

namespace
{
void resetWindowSlot(WindowSlot &slot)
{
    slot.active = false;
    slot.channel = 0;
    slot.id = 0;
    slot.head = 0;
    slot.count = 0;
}

void appendSample(WindowSlot &slot, const CapturedFrame &frame, size_t cap)
{
    if (!slot.samples || cap == 0)
        return;
    RawSamplePoint &dst = slot.samples[slot.head];
    dst.ts_us = frame.ts_us;
    dst.dlc = frame.dlc;
    for (uint8_t i = 0; i < 8; ++i)
        dst.data[i] = frame.data[i];
    slot.head = (slot.head + 1) % cap;
    if (slot.count < cap)
        ++slot.count;
}
}

void WatchedSignalWindow::init(WindowSlot *slots, size_t slot_count, size_t samples_per_slot)
{
    slots_ = slots;
    slot_count_ = slot_count;
    samples_per_slot_ = samples_per_slot;
    if (!slots_)
        return;
    for (size_t i = 0; i < slot_count_; ++i)
    {
        RawSamplePoint *samples = new RawSamplePoint[samples_per_slot_]{};
        slots_[i] = WindowSlot{};
        slots_[i].samples = samples;
    }
}

WindowSlot *WatchedSignalWindow::find(uint8_t channel, uint16_t id)
{
    for (size_t i = 0; i < slot_count_; ++i)
        if (slots_[i].active && slots_[i].channel == channel && slots_[i].id == id)
            return &slots_[i];
    return nullptr;
}

const WindowSlot *WatchedSignalWindow::find(uint8_t channel, uint16_t id) const
{
    for (size_t i = 0; i < slot_count_; ++i)
        if (slots_[i].active && slots_[i].channel == channel && slots_[i].id == id)
            return &slots_[i];
    return nullptr;
}

bool WatchedSignalWindow::watch(uint8_t channel, uint16_t id)
{
    if (find(channel, id))
        return true;
    for (size_t i = 0; i < slot_count_; ++i)
    {
        if (!slots_[i].active)
        {
            slots_[i].active = true;
            slots_[i].channel = channel;
            slots_[i].id = id;
            slots_[i].head = 0;
            slots_[i].count = 0;
            return true;
        }
    }
    return false;
}

void WatchedSignalWindow::unwatch(uint8_t channel, uint16_t id)
{
    if (WindowSlot *slot = find(channel, id))
    {
        RawSamplePoint *samples = slot->samples;
        resetWindowSlot(*slot);
        slot->samples = samples;
    }
}

bool WatchedSignalWindow::isWatched(uint8_t channel, uint16_t id) const
{
    return find(channel, id) != nullptr;
}

void WatchedSignalWindow::push(const CapturedFrame &frame)
{
    if (WindowSlot *slot = find(frame.channel, static_cast<uint16_t>(frame.id)))
        appendSample(*slot, frame, samples_per_slot_);
}

size_t WatchedSignalWindow::copySamples(uint8_t channel, uint16_t id, RawSamplePoint *out, size_t cap) const
{
    const WindowSlot *slot = find(channel, id);
    if (!slot || !out || cap == 0)
        return 0;
    const size_t n = slot->count < cap ? slot->count : cap;
    const size_t start = (slot->head + samples_per_slot_ - n) % samples_per_slot_;
    for (size_t i = 0; i < n; ++i)
        out[i] = slot->samples[(start + i) % samples_per_slot_];
    return n;
}
```

- [ ] **Step 5: Add to native build filter**

In `platformio.ini` `[env:native]` `build_src_filter`, add:

```ini
    +<analyzer/signal_window.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_signal_window`
Expected: PASS — 5 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/analyzer/signal_window.h src/analyzer/signal_window.cpp test/test_signal_window/test_signal_window.cpp platformio.ini
git commit -m "feat(analyzer): add watched sample windows for P4"
```

---

## Task 3: Candidate hint analyzers (TDD)

**Files:**
- Create: `src/analyzer/signal_hints.h`
- Create: `src/analyzer/signal_hints.cpp`
- Create: `test/test_signal_hints/test_signal_hints.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Write the failing test**

Create `test/test_signal_hints/test_signal_hints.cpp`:

```cpp
#include <unity.h>
#include "analyzer/signal_hints.h"

static void fillCounter(WindowSample *samples, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        samples[i].ts_us = 1000 * static_cast<uint64_t>(i + 1);
        samples[i].dlc = 2;
        samples[i].data[0] = static_cast<uint8_t>(i & 0x0F);
        samples[i].data[1] = 0xA5;
    }
}

static void fillMux(WindowSample *samples, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        samples[i].ts_us = 1000 * static_cast<uint64_t>(i + 1);
        samples[i].dlc = 3;
        samples[i].data[0] = static_cast<uint8_t>(i % 2);
        samples[i].data[1] = samples[i].data[0] == 0 ? 0x10 : 0x80;
        samples[i].data[2] = samples[i].data[0] == 0 ? 0x11 : 0x81;
    }
}

void setUp() {}
void tearDown() {}

void test_counter_hint_detects_low_nibble_counter() {
    WindowSample samples[16]{};
    fillCounter(samples, 16);
    SignalHint hints[8]{};
    const size_t n = detectSignalHints(0, 0x120, samples, 16, hints, 8);
    TEST_ASSERT_TRUE(n >= 1);
    TEST_ASSERT_EQUAL_UINT8(HintKind::Counter, hints[0].kind);
    TEST_ASSERT_EQUAL_UINT8(0, hints[0].start_bit);
    TEST_ASSERT_EQUAL_UINT8(4, hints[0].bit_length);
    TEST_ASSERT_TRUE(hints[0].confidence_x100 >= 70);
}

void test_mux_hint_detects_selector_byte() {
    WindowSample samples[12]{};
    fillMux(samples, 12);
    SignalHint hints[8]{};
    const size_t n = detectSignalHints(1, 0x401, samples, 12, hints, 8);
    TEST_ASSERT_TRUE(n >= 1);
    bool found_mux = false;
    for (size_t i = 0; i < n; ++i)
    {
        if (hints[i].kind == HintKind::Mux && hints[i].start_bit == 0 && hints[i].bit_length == 8)
            found_mux = true;
    }
    TEST_ASSERT_TRUE(found_mux);
}

void test_constant_data_does_not_produce_strong_counter_hint() {
    WindowSample samples[8]{};
    for (size_t i = 0; i < 8; ++i)
    {
        samples[i].ts_us = 1000 * static_cast<uint64_t>(i + 1);
        samples[i].dlc = 2;
        samples[i].data[0] = 0xAA;
        samples[i].data[1] = 0x55;
    }
    SignalHint hints[8]{};
    const size_t n = detectSignalHints(0, 0x555, samples, 8, hints, 8);
    for (size_t i = 0; i < n; ++i)
        TEST_ASSERT_FALSE(hints[i].kind == HintKind::Counter && hints[i].confidence_x100 >= 70);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_counter_hint_detects_low_nibble_counter);
    RUN_TEST(test_mux_hint_detects_selector_byte);
    RUN_TEST(test_constant_data_does_not_produce_strong_counter_hint);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_signal_hints`
Expected: FAIL — `signal_hints.h` not found.

- [ ] **Step 3: Write header**

Create `src/analyzer/signal_hints.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

struct WindowSample
{
    uint64_t ts_us = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

enum class HintKind : uint8_t
{
    Mux = 1,
    Counter = 2,
    Checksum = 3,
};

struct SignalHint
{
    HintKind kind = HintKind::Counter;
    uint8_t channel = 0;
    uint16_t id = 0;
    uint8_t start_bit = 0;
    uint8_t bit_length = 0;
    uint8_t confidence_x100 = 0;
    char evidence[48] = {};
};

size_t detectSignalHints(uint8_t channel, uint16_t id, const WindowSample *samples, size_t sample_count,
                         SignalHint *out, size_t cap);
```

- [ ] **Step 4: Write minimal implementation**

Create `src/analyzer/signal_hints.cpp`:

```cpp
#include "analyzer/signal_hints.h"
#include <cstring>

namespace
{
uint8_t counterScoreLowNibble(const WindowSample *samples, size_t count)
{
    if (!samples || count < 4)
        return 0;
    size_t hits = 0;
    for (size_t i = 1; i < count; ++i)
    {
        const uint8_t prev = samples[i - 1].data[0] & 0x0F;
        const uint8_t next = samples[i].data[0] & 0x0F;
        if (((prev + 1) & 0x0F) == next)
            ++hits;
    }
    return static_cast<uint8_t>((hits * 100u) / (count - 1));
}

bool looksMuxByte0(const WindowSample *samples, size_t count)
{
    if (!samples || count < 4)
        return false;
    bool seen0 = false;
    bool seen1 = false;
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t mux = samples[i].data[0];
        if (mux == 0)
            seen0 = true;
        else if (mux == 1)
            seen1 = true;
    }
    return seen0 && seen1;
}
}

size_t detectSignalHints(uint8_t channel, uint16_t id, const WindowSample *samples, size_t sample_count,
                         SignalHint *out, size_t cap)
{
    if (!out || cap == 0 || !samples || sample_count == 0)
        return 0;

    size_t n = 0;
    const uint8_t counter_score = counterScoreLowNibble(samples, sample_count);
    if (counter_score >= 70 && n < cap)
    {
        SignalHint &hint = out[n++];
        hint.kind = HintKind::Counter;
        hint.channel = channel;
        hint.id = id;
        hint.start_bit = 0;
        hint.bit_length = 4;
        hint.confidence_x100 = counter_score;
        strncpy(hint.evidence, "low nibble follows +1 modulo", sizeof(hint.evidence) - 1);
    }

    if (looksMuxByte0(samples, sample_count) && n < cap)
    {
        SignalHint &hint = out[n++];
        hint.kind = HintKind::Mux;
        hint.channel = channel;
        hint.id = id;
        hint.start_bit = 0;
        hint.bit_length = 8;
        hint.confidence_x100 = 75;
        strncpy(hint.evidence, "byte0 splits samples into stable groups", sizeof(hint.evidence) - 1);
    }

    return n;
}
```

- [ ] **Step 5: Add to native build filter**

In `platformio.ini` `[env:native]` `build_src_filter`, add:

```ini
    +<analyzer/signal_hints.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_signal_hints`
Expected: PASS — 3 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/analyzer/signal_hints.h src/analyzer/signal_hints.cpp test/test_signal_hints/test_signal_hints.cpp platformio.ini
git commit -m "feat(analyzer): add initial P4 signal hint detectors"
```

---

## Task 4: Common signal store (TDD)

**Files:**
- Create: `src/analyzer/common_signal_store.h`
- Create: `src/analyzer/common_signal_store.cpp`
- Create: `test/test_common_signal_store/test_common_signal_store.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Write the failing test**

Create `test/test_common_signal_store/test_common_signal_store.cpp`:

```cpp
#include <unity.h>
#include "analyzer/common_signal_store.h"

static CommonSignalStore store;

static CommonSignalSpec spec(uint8_t ch, uint16_t id, uint8_t start, uint8_t len, const char *name)
{
    CommonSignalSpec s{};
    s.channel = ch;
    s.id = id;
    s.start_bit = start;
    s.bit_length = len;
    s.endian = 0;
    s.is_signed = 0;
    s.scale = 1.0f;
    s.offset = 0.0f;
    strncpy(s.label, name, sizeof(s.label) - 1);
    return s;
}

void setUp() { store.begin(); }
void tearDown() {}

void test_adds_common_signal() {
    TEST_ASSERT_TRUE(store.upsert(spec(0, 0x120, 0, 8, "speed"), false));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_STRING("speed", store.entries()[0].label);
}

void test_overwrites_same_identity() {
    TEST_ASSERT_TRUE(store.upsert(spec(0, 0x120, 0, 8, "speed"), false));
    TEST_ASSERT_TRUE(store.upsert(spec(0, 0x120, 0, 8, "speed2"), false));
    TEST_ASSERT_EQUAL_size_t(1, store.count());
    TEST_ASSERT_EQUAL_STRING("speed2", store.entries()[0].label);
}

void test_capacity_rejects_overflow() {
    for (size_t i = 0; i < kMaxCommonSignals; ++i)
        TEST_ASSERT_TRUE(store.upsert(spec(0, static_cast<uint16_t>(0x100 + i), 0, 8, "x"), false));
    TEST_ASSERT_FALSE(store.upsert(spec(1, 0x777, 0, 8, "overflow"), false));
}

void test_invalid_blob_rejected() {
    CommonSignalSpec items[1]{};
    items[0].channel = 2;
    TEST_ASSERT_FALSE(store.loadFromBlobForTest(items, 1));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_adds_common_signal);
    RUN_TEST(test_overwrites_same_identity);
    RUN_TEST(test_capacity_rejects_overflow);
    RUN_TEST(test_invalid_blob_rejected);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_common_signal_store`
Expected: FAIL — `common_signal_store.h` not found.

- [ ] **Step 3: Write header**

Create `src/analyzer/common_signal_store.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kMaxCommonSignals = 32;
constexpr size_t kCommonSignalLabelLen = 24;

struct CommonSignalSpec
{
    uint8_t channel = 0;
    uint16_t id = 0;
    uint8_t start_bit = 0;
    uint8_t bit_length = 0;
    uint8_t endian = 0;
    uint8_t is_signed = 0;
    float scale = 1.0f;
    float offset = 0.0f;
    char label[kCommonSignalLabelLen] = {};
};

class CommonSignalStore
{
public:
    void begin();
#if !defined(ARDUINO)
    bool loadFromBlobForTest(const CommonSignalSpec *items, size_t count);
#endif
    bool upsert(const CommonSignalSpec &spec, bool save);
    bool upsert(const CommonSignalSpec &spec);
    const CommonSignalSpec *entries() const;
    size_t count() const;
    void save();

private:
    int find(const CommonSignalSpec &spec) const;
    bool loadEntries(const CommonSignalSpec *items, size_t count);
    void persist();

    CommonSignalSpec entries_[kMaxCommonSignals] = {};
    size_t count_ = 0;
};
```

- [ ] **Step 4: Write minimal implementation**

Create `src/analyzer/common_signal_store.cpp`:

```cpp
#include "analyzer/common_signal_store.h"
#include <cstring>
#if defined(ARDUINO)
#include <Preferences.h>
#endif

void CommonSignalStore::begin()
{
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin("analyzer", true))
        return;
    const size_t len = prefs.getBytesLength("p4_common");
    if (len > 0 && len % sizeof(CommonSignalSpec) == 0)
    {
        const size_t count = len / sizeof(CommonSignalSpec);
        if (count <= kMaxCommonSignals)
        {
            CommonSignalSpec items[kMaxCommonSignals] = {};
            if (prefs.getBytes("p4_common", items, len) == len)
                loadEntries(items, count);
        }
    }
    prefs.end();
#else
    count_ = 0;
    memset(entries_, 0, sizeof(entries_));
#endif
}

#if !defined(ARDUINO)
bool CommonSignalStore::loadFromBlobForTest(const CommonSignalSpec *items, size_t count)
{
    return loadEntries(items, count);
}
#endif

bool CommonSignalStore::loadEntries(const CommonSignalSpec *items, size_t count)
{
    if (count > kMaxCommonSignals || (count > 0 && !items))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (items[i].channel > 1 || items[i].bit_length == 0)
            return false;
    memset(entries_, 0, sizeof(entries_));
    if (count > 0)
        memcpy(entries_, items, count * sizeof(CommonSignalSpec));
    count_ = count;
    return true;
}

int CommonSignalStore::find(const CommonSignalSpec &spec) const
{
    for (size_t i = 0; i < count_; ++i)
    {
        const CommonSignalSpec &cur = entries_[i];
        if (cur.channel == spec.channel && cur.id == spec.id && cur.start_bit == spec.start_bit && cur.bit_length == spec.bit_length)
            return static_cast<int>(i);
    }
    return -1;
}

bool CommonSignalStore::upsert(const CommonSignalSpec &spec)
{
    return upsert(spec, true);
}

bool CommonSignalStore::upsert(const CommonSignalSpec &spec, bool save)
{
    if (spec.channel > 1 || spec.bit_length == 0)
        return false;
    int index = find(spec);
    if (index < 0)
    {
        if (count_ >= kMaxCommonSignals)
            return false;
        index = static_cast<int>(count_++);
    }
    entries_[index] = spec;
    entries_[index].label[kCommonSignalLabelLen - 1] = '\0';
    if (save)
        persist();
    return true;
}

const CommonSignalSpec *CommonSignalStore::entries() const { return entries_; }
size_t CommonSignalStore::count() const { return count_; }
void CommonSignalStore::save() { persist(); }

void CommonSignalStore::persist()
{
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin("analyzer", false))
        return;
    prefs.putBytes("p4_common", entries_, count_ * sizeof(CommonSignalSpec));
    prefs.end();
#endif
}
```

- [ ] **Step 5: Add to native build filter**

In `platformio.ini` `[env:native]` `build_src_filter`, add:

```ini
    +<analyzer/common_signal_store.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_common_signal_store`
Expected: PASS — 4 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/analyzer/common_signal_store.h src/analyzer/common_signal_store.cpp test/test_common_signal_store/test_common_signal_store.cpp platformio.ini
git commit -m "feat(analyzer): persist common P4 signal specs"
```

---

## Task 5: P4 WebSocket record layout (TDD)

**Files:**
- Modify: `src/analyzer/ws_protocol.h`
- Modify: `src/analyzer/ws_protocol.cpp`
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp`

- [ ] **Step 1: Add P4 record types to header**

In `src/analyzer/ws_protocol.h`, after existing `WsDiffSubtype`, add a new top-level message type:

```cpp
enum WsMsgType : uint8_t
{
    WS_MSG_FRAME_DELTA = 0x01,
    WS_MSG_BUS_STATS = 0x02,
    WS_MSG_DIFF = 0x03,
    WS_MSG_SIGNAL = 0x04,
};

enum WsSignalSubtype : uint8_t
{
    WS_SIGNAL_SAMPLES = 0x01,
    WS_SIGNAL_HINTS = 0x02,
};
```

Inside the packed block, add:

```cpp
struct WsSignalSampleRecord
{
    uint8_t channel;
    uint16_t id;
    uint32_t ts_offset_ms;
    uint8_t dlc;
    uint8_t data[8];
};

struct WsSignalHintRecord
{
    uint8_t kind;
    uint8_t channel;
    uint16_t id;
    uint8_t start_bit;
    uint8_t bit_length;
    uint8_t confidence_x100;
    char evidence[48];
};
```

Add declarations:

```cpp
size_t wsBuildSignalSamples(uint8_t *buf, size_t cap, const WsSignalSampleRecord *recs, uint8_t count);
size_t wsBuildSignalHints(uint8_t *buf, size_t cap, const WsSignalHintRecord *recs, uint8_t count);
```

- [ ] **Step 2: Write failing tests**

Append to `test/test_ws_protocol/test_ws_protocol.cpp` before `main`:

```cpp
void test_signal_samples_layout()
{
    WsSignalSampleRecord rec{};
    rec.channel = 1;
    rec.id = 0x401;
    rec.ts_offset_ms = 250;
    rec.dlc = 2;
    rec.data[0] = 0x12;
    rec.data[1] = 0x34;
    uint8_t buf[128];
    const size_t n = wsBuildSignalSamples(buf, sizeof(buf), &rec, 1);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_SAMPLES, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsSignalSampleRecord), n);
    const WsSignalSampleRecord *out = reinterpret_cast<const WsSignalSampleRecord *>(buf + 3);
    TEST_ASSERT_EQUAL_UINT32(250, out->ts_offset_ms);
    TEST_ASSERT_EQUAL_UINT8(0x34, out->data[1]);
}

void test_signal_hints_layout_and_cap()
{
    WsSignalHintRecord recs[2]{};
    recs[0].kind = 2;
    recs[0].channel = 0;
    recs[0].id = 0x120;
    recs[0].start_bit = 0;
    recs[0].bit_length = 4;
    recs[0].confidence_x100 = 87;
    strcpy(recs[0].evidence, "low nibble counter");
    uint8_t buf[3 + sizeof(WsSignalHintRecord) + 1];
    const size_t n = wsBuildSignalHints(buf, sizeof(buf), recs, 2);
    TEST_ASSERT_EQUAL_UINT8(WS_MSG_SIGNAL, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(WS_SIGNAL_HINTS, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[2]);
    TEST_ASSERT_EQUAL_size_t(3 + sizeof(WsSignalHintRecord), n);
}
```

Add to `main`:

```cpp
    RUN_TEST(test_signal_samples_layout);
    RUN_TEST(test_signal_hints_layout_and_cap);
```

- [ ] **Step 3: Run test to verify it fails**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol`
Expected: FAIL — undefined `wsBuildSignalSamples` / `wsBuildSignalHints`.

- [ ] **Step 4: Implement builders**

In `src/analyzer/ws_protocol.cpp`, append:

```cpp
namespace
{
template <typename T>
size_t wsBuildSignalRecords(uint8_t *buf, size_t cap, WsSignalSubtype subtype, const T *recs, uint8_t count)
{
    if (cap < 3)
        return 0;
    const size_t rec_size = sizeof(T);
    size_t max_by_cap = (cap - 3) / rec_size;
    if (max_by_cap > count)
        max_by_cap = count;
    buf[0] = WS_MSG_SIGNAL;
    buf[1] = subtype;
    buf[2] = static_cast<uint8_t>(max_by_cap);
    if (max_by_cap > 0)
        memcpy(buf + 3, recs, max_by_cap * rec_size);
    return 3 + max_by_cap * rec_size;
}
}

size_t wsBuildSignalSamples(uint8_t *buf, size_t cap, const WsSignalSampleRecord *recs, uint8_t count)
{
    return wsBuildSignalRecords(buf, cap, WS_SIGNAL_SAMPLES, recs, count);
}

size_t wsBuildSignalHints(uint8_t *buf, size_t cap, const WsSignalHintRecord *recs, uint8_t count)
{
    return wsBuildSignalRecords(buf, cap, WS_SIGNAL_HINTS, recs, count);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol`
Expected: PASS — existing tests plus 2 new P4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/analyzer/ws_protocol.h src/analyzer/ws_protocol.cpp test/test_ws_protocol/test_ws_protocol.cpp
git commit -m "feat(analyzer): add P4 signal sample and hint WS records"
```

---

## Task 6: Backend P4 wiring and endpoints

**Files:**
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `src/analyzer/analyzer_web.cpp`
- Modify: `src/can_analyzer.cpp`

- [ ] **Step 1: Extend analyzer web context**

In `src/analyzer/analyzer_web.h`, add includes and extend the context function:

```cpp
#include "analyzer/common_signal_store.h"
#include "analyzer/signal_hints.h"
#include "analyzer/signal_window.h"

void analyzerWebSetContext(FrameQueue *queue, IdTable *table, BusStatsTracker *stats,
                           PretriggerBuffer *pretrigger, SnapshotStore *snapshots, LabelStore *labels,
                           WatchedSignalWindow *signal_window, CommonSignalStore *common_signals);
```

- [ ] **Step 2: Add backend globals and caps**

Near existing globals in `src/analyzer/analyzer_web.cpp`, add:

```cpp
WatchedSignalWindow *g_signalWindow = nullptr;
CommonSignalStore *g_commonSignals = nullptr;
constexpr size_t kSignalSampleBatchCapacity = wsBatchCapacity((kPushBufBytes - 3) / sizeof(WsSignalSampleRecord));
constexpr size_t kSignalHintBatchCapacity = wsBatchCapacity((kPushBufBytes - 3) / sizeof(WsSignalHintRecord));
static_assert(kSignalSampleBatchCapacity >= 1, "signal sample batch capacity must be non-zero");
static_assert(kSignalHintBatchCapacity >= 1, "signal hint batch capacity must be non-zero");
```

Update `analyzerWebSetContext()` to store both pointers.

- [ ] **Step 3: Extend pending command model**

In the `PendingCmdType` enum, add:

```cpp
    SignalWatch,
    SignalHints,
```

Extend `PendingCmd` with:

```cpp
    uint16_t watch_id = 0;
    bool watch_on = false;
```

- [ ] **Step 4: Parse P4 commands**

In `handleCommand()` add:

```cpp
    if (strcmp(cmd, "p4_watch") == 0)
    {
        const char *ch = doc["ch"] | nullptr;
        const int id = doc["id"] | -1;
        if (!analyzerWebParseChannelToken(ch, pending.channel) || id < 0 || id >= kStdIdCount)
            return;
        pending.type = PendingCmdType::SignalWatch;
        pending.watch_id = static_cast<uint16_t>(id);
        pending.watch_on = doc["on"] | false;
        enqueuePendingCommand(pending);
        return;
    }
    if (strcmp(cmd, "p4_hints") == 0)
    {
        const char *ch = doc["ch"] | nullptr;
        const int id = doc["id"] | -1;
        if (!analyzerWebParseChannelToken(ch, pending.channel) || id < 0 || id >= kStdIdCount)
            return;
        pending.type = PendingCmdType::SignalHints;
        pending.watch_id = static_cast<uint16_t>(id);
        enqueuePendingCommand(pending);
        return;
    }
```

- [ ] **Step 5: Add sample and hint send helpers**

In `src/analyzer/analyzer_web.cpp`, append helpers inside the anonymous namespace:

```cpp
void sendSignalSamples(uint8_t channel, uint16_t id)
{
    if (!g_signalWindow || ws.count() == 0)
        return;

    RawSamplePoint points[kSignalSampleBatchCapacity] = {};
    WsSignalSampleRecord wire[kSignalSampleBatchCapacity] = {};
    uint8_t buf[kPushBufBytes] = {};
    const size_t n = g_signalWindow->copySamples(channel, id, points, kSignalSampleBatchCapacity);
    if (n == 0)
        return;

    const uint64_t base_ts = points[0].ts_us;
    for (size_t i = 0; i < n; ++i)
    {
        wire[i].channel = channel;
        wire[i].id = id;
        wire[i].ts_offset_ms = static_cast<uint32_t>((points[i].ts_us - base_ts) / 1000);
        wire[i].dlc = points[i].dlc;
        for (uint8_t b = 0; b < 8; ++b)
            wire[i].data[b] = points[i].data[b];
    }

    const size_t bytes = wsBuildSignalSamples(buf, sizeof(buf), wire, static_cast<uint8_t>(n));
    if (bytes > 0)
        ws.binaryAll(buf, bytes);
}

void sendSignalHints(uint8_t channel, uint16_t id)
{
    if (!g_signalWindow || ws.count() == 0)
        return;

    RawSamplePoint raw[kSignalSampleBatchCapacity] = {};
    WindowSample samples[kSignalSampleBatchCapacity] = {};
    SignalHint hints[kSignalHintBatchCapacity] = {};
    WsSignalHintRecord wire[kSignalHintBatchCapacity] = {};
    uint8_t buf[kPushBufBytes] = {};
    const size_t n = g_signalWindow->copySamples(channel, id, raw, kSignalSampleBatchCapacity);
    if (n == 0)
        return;

    for (size_t i = 0; i < n; ++i)
    {
        samples[i].ts_us = raw[i].ts_us;
        samples[i].dlc = raw[i].dlc;
        for (uint8_t b = 0; b < 8; ++b)
            samples[i].data[b] = raw[i].data[b];
    }

    const size_t hint_count = detectSignalHints(channel, id, samples, n, hints, kSignalHintBatchCapacity);
    for (size_t i = 0; i < hint_count; ++i)
    {
        wire[i].kind = static_cast<uint8_t>(hints[i].kind);
        wire[i].channel = hints[i].channel;
        wire[i].id = hints[i].id;
        wire[i].start_bit = hints[i].start_bit;
        wire[i].bit_length = hints[i].bit_length;
        wire[i].confidence_x100 = hints[i].confidence_x100;
        strncpy(wire[i].evidence, hints[i].evidence, sizeof(wire[i].evidence) - 1);
    }

    const size_t bytes = wsBuildSignalHints(buf, sizeof(buf), wire, static_cast<uint8_t>(hint_count));
    if (bytes > 0)
        ws.binaryAll(buf, bytes);
}
```

- [ ] **Step 6: Process P4 pending commands**

In `processPendingCommand()` add:

```cpp
    case PendingCmdType::SignalWatch:
        if (g_signalWindow)
        {
            if (cmd.watch_on)
                g_signalWindow->watch(cmd.channel, cmd.watch_id);
            else
                g_signalWindow->unwatch(cmd.channel, cmd.watch_id);
            if (cmd.watch_on)
                sendSignalSamples(cmd.channel, cmd.watch_id);
        }
        break;
    case PendingCmdType::SignalHints:
        sendSignalHints(cmd.channel, cmd.watch_id);
        break;
```

- [ ] **Step 7: Feed watched windows while draining queue**

In `drainQueueIntoTable()`, after `g_pretrigger->push(cap);`, add:

```cpp
        if (g_signalWindow)
            g_signalWindow->push(cap);
```

- [ ] **Step 8: Add common-signal endpoints**

In `analyzerWebBegin()`, add:

```cpp
    server.on("/api/p4/common", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        if (g_commonSignals)
        {
            const CommonSignalSpec *items = g_commonSignals->entries();
            for (size_t i = 0; i < g_commonSignals->count(); ++i)
            {
                JsonObject item = arr.createNestedObject();
                item["ch"] = items[i].channel == 1 ? "B" : "A";
                item["id"] = items[i].id;
                item["start_bit"] = items[i].start_bit;
                item["bit_length"] = items[i].bit_length;
                item["endian"] = items[i].endian == 0 ? "intel" : "motorola";
                item["signed"] = items[i].is_signed != 0;
                item["scale"] = items[i].scale;
                item["offset"] = items[i].offset;
                item["label"] = items[i].label;
            }
        }
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/p4/common", HTTP_POST,
              [](AsyncWebServerRequest *) {},
              nullptr,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
                  if (!g_commonSignals)
                  {
                      request->send(500, "application/json", "{\"ok\":false}");
                      return;
                  }
                  JsonDocument doc;
                  if (deserializeJson(doc, data, len))
                  {
                      request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
                      return;
                  }
                  JsonArray arr = doc["signals"].as<JsonArray>();
                  if (arr.isNull())
                  {
                      request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing signals\"}");
                      return;
                  }
                  for (JsonObject item : arr)
                  {
                      CommonSignalSpec spec{};
                      spec.channel = (item["ch"] | "A")[0] == 'B' ? 1 : 0;
                      spec.id = item["id"] | 0;
                      spec.start_bit = item["start_bit"] | 0;
                      spec.bit_length = item["bit_length"] | 0;
                      spec.endian = strcmp(item["endian"] | "intel", "motorola") == 0 ? 1 : 0;
                      spec.is_signed = (item["signed"] | false) ? 1 : 0;
                      spec.scale = item["scale"] | 1.0f;
                      spec.offset = item["offset"] | 0.0f;
                      strlcpy(spec.label, item["label"] | "", sizeof(spec.label));
                      if (!g_commonSignals->upsert(spec, false))
                      {
                          request->send(400, "application/json", "{\"ok\":false,\"error\":\"store full or invalid\"}");
                          return;
                      }
                  }
                  g_commonSignals->save();
                  request->send(200, "application/json", "{\"ok\":true}");
              });
```

- [ ] **Step 9: Allocate P4 backend state in `src/can_analyzer.cpp`**

Near existing globals, add:

```cpp
constexpr size_t kSignalWatchSlots = 4;
constexpr size_t kSignalSamplesPerSlot = 64;
WindowSlot g_signalSlots[kSignalWatchSlots];
WatchedSignalWindow g_signalWindow;
CommonSignalStore g_commonSignals;
```

In `setup()`, after `g_labels.begin();`, add:

```cpp
g_signalWindow.init(g_signalSlots, kSignalWatchSlots, kSignalSamplesPerSlot);
g_commonSignals.begin();
```

Update `analyzerWebSetContext(...)` call to pass `&g_signalWindow` and `&g_commonSignals`.

- [ ] **Step 10: Build firmware**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer`
Expected: SUCCESS.

- [ ] **Step 11: Commit**

```bash
git add src/analyzer/analyzer_web.h src/analyzer/analyzer_web.cpp src/can_analyzer.cpp
git commit -m "feat(analyzer): wire P4 backend watch and common signal flows"
```

---

## Task 7: Frontend Signal Workbench

**Files:**
- Modify: `data/analyzer/index.html`
- Modify: `data/analyzer/style.css`
- Modify: `data/analyzer/app.js`

- [ ] **Step 1: Add Workbench markup**

In `data/analyzer/index.html`, after the existing `.grid.p3-results`, add:

```html
  <section class="signal-workbench">
    <div class="signal-header">
      <h2>Signal Workbench</h2>
      <span id="signal-target">未选中 ID</span>
      <button id="signal-export-btn">导出 JSON</button>
      <input id="signal-import-input" type="file" accept="application/json"/>
      <button id="signal-load-common-btn">加载设备常用项</button>
      <button id="signal-save-common-btn">保存到设备</button>
    </div>
    <div class="signal-grid">
      <section>
        <h3>候选提示</h3>
        <div id="signal-hints">暂无候选</div>
      </section>
      <section>
        <h3>定义</h3>
        <label>Label <input id="signal-label" type="text"/></label>
        <label>Start bit <input id="signal-start-bit" type="number" min="0" max="63" value="0"/></label>
        <label>Length <input id="signal-bit-length" type="number" min="1" max="32" value="8"/></label>
        <label>Endian <select id="signal-endian"><option value="intel">intel</option><option value="motorola">motorola</option></select></label>
        <label><input id="signal-signed" type="checkbox"/> signed</label>
        <label>Scale <input id="signal-scale" type="number" step="0.01" value="1"/></label>
        <label>Offset <input id="signal-offset" type="number" step="0.01" value="0"/></label>
        <label><input id="signal-has-mux" type="checkbox"/> mux</label>
        <label>Mux start <input id="signal-mux-start-bit" type="number" min="0" max="63" value="0"/></label>
        <label>Mux len <input id="signal-mux-bit-length" type="number" min="1" max="8" value="8"/></label>
        <label>Mux value <input id="signal-mux-value" type="number" min="0" value="0"/></label>
        <button id="signal-apply-btn">应用定义</button>
      </section>
      <section>
        <h3>结果</h3>
        <div id="signal-current">当前值：-</div>
        <div id="signal-range">范围：-</div>
        <canvas id="signal-chart" width="480" height="140"></canvas>
      </section>
    </div>
  </section>
```

- [ ] **Step 2: Add styles**

Append to `data/analyzer/style.css`:

```css
.signal-workbench { margin: 8px; padding: 10px; border: 1px solid #333; background: #151515; }
.signal-header { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 10px; }
.signal-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; }
.signal-grid section { border: 1px solid #2c2c2c; padding: 8px; background: #101010; }
.signal-grid label { display: flex; justify-content: space-between; gap: 8px; margin-bottom: 6px; }
#signal-hints .hint { padding: 4px 6px; border-bottom: 1px solid #222; }
#signal-hints .hint button { margin-left: 8px; }
#signal-chart { width: 100%; background: #0b0b0b; border: 1px solid #222; }
```

- [ ] **Step 3: Add frontend state**

Near the top of `data/analyzer/app.js`, add:

```js
const signalTarget = document.getElementById('signal-target');
const signalHintsEl = document.getElementById('signal-hints');
const signalCurrentEl = document.getElementById('signal-current');
const signalRangeEl = document.getElementById('signal-range');
const signalChart = document.getElementById('signal-chart');
const signalCtx = signalChart.getContext('2d');
const signalWorkspace = new Map();
let activeSignalTarget = null;
let activeSignalHints = [];
let activeSignalSamples = [];
```

Add helpers:

```js
function signalKey(ch, id, label, startBit, bitLength, muxValue) {
  return `${ch}:${id}:${label}:${startBit}:${bitLength}:${muxValue}`;
}

function targetSignalKey(spec) {
  return signalKey(spec.channel, spec.id, spec.label || '', spec.start_bit, spec.bit_length,
    spec.has_mux ? spec.mux_value : -1);
}

function parseSignalSpecFromForm() {
  if (!activeSignalTarget) return null;
  return {
    version: 1,
    channel: activeSignalTarget.ch === 1 ? 'B' : 'A',
    id: activeSignalTarget.id,
    label: document.getElementById('signal-label').value.trim(),
    start_bit: Number(document.getElementById('signal-start-bit').value || 0),
    bit_length: Number(document.getElementById('signal-bit-length').value || 8),
    endian: document.getElementById('signal-endian').value,
    signed: document.getElementById('signal-signed').checked,
    scale: Number(document.getElementById('signal-scale').value || 1),
    offset: Number(document.getElementById('signal-offset').value || 0),
    has_mux: document.getElementById('signal-has-mux').checked,
    mux_start_bit: Number(document.getElementById('signal-mux-start-bit').value || 0),
    mux_bit_length: Number(document.getElementById('signal-mux-bit-length').value || 8),
    mux_value: Number(document.getElementById('signal-mux-value').value || 0),
  };
}
```

- [ ] **Step 4: Add JS decode and chart helpers**

Append to `data/analyzer/app.js`:

```js
function extractBits(sample, startBit, bitLength, endian) {
  const bytes = sample.data || [];
  const dlc = sample.dlc || bytes.length;
  if (startBit + bitLength > dlc * 8) return null;
  if (endian === 'intel') {
    let value = 0;
    for (let i = 0; i < bitLength; i++) {
      const bit = startBit + i;
      const byte = bit >> 3;
      const shift = bit & 7;
      value |= ((bytes[byte] >> shift) & 1) << i;
    }
    return value >>> 0;
  }
  let value = 0;
  for (let i = 0; i < bitLength; i++) {
    const bit = startBit + i;
    const byte = bit >> 3;
    const shift = 7 - (bit & 7);
    value = (value << 1) | ((bytes[byte] >> shift) & 1);
  }
  return value >>> 0;
}

function signExtend(raw, bitLength) {
  if (bitLength <= 0 || bitLength >= 32) return raw | 0;
  const sign = 1 << (bitLength - 1);
  const mask = (1 << bitLength) - 1;
  raw &= mask;
  return (raw ^ sign) - sign;
}

function decodeSignalSpec(spec, sample) {
  if (spec.has_mux) {
    const mux = extractBits(sample, spec.mux_start_bit, spec.mux_bit_length, 'intel');
    if (mux === null || mux !== spec.mux_value) return null;
  }
  const raw = extractBits(sample, spec.start_bit, spec.bit_length, spec.endian);
  if (raw === null) return null;
  const signedRaw = spec.signed ? signExtend(raw, spec.bit_length) : raw;
  return { raw, value: signedRaw * spec.scale + spec.offset };
}

function drawSignalChart(points) {
  signalCtx.clearRect(0, 0, signalChart.width, signalChart.height);
  if (!points.length) return;
  const values = points.map(p => p.value);
  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = Math.max(1e-6, max - min);
  signalCtx.strokeStyle = '#4fc3f7';
  signalCtx.beginPath();
  points.forEach((point, index) => {
    const x = points.length === 1 ? 0 : (index * (signalChart.width - 1)) / (points.length - 1);
    const y = signalChart.height - 1 - ((point.value - min) / span) * (signalChart.height - 10);
    if (index === 0) signalCtx.moveTo(x, y);
    else signalCtx.lineTo(x, y);
  });
  signalCtx.stroke();
  signalRangeEl.textContent = `范围：${min.toFixed(3)} .. ${max.toFixed(3)}`;
  signalCurrentEl.textContent = `当前值：${points[points.length - 1].value.toFixed(3)}`;
}
```

- [ ] **Step 5: Add target selection and watch flow**

In `appendIdCell(cell, rec)`, after existing actions, append:

```js
  const s = document.createElement('button');
  s.type = 'button';
  s.textContent = 'S';
  s.title = '打开 Signal Workbench';
  s.onclick = (ev) => { ev.stopPropagation(); selectSignalTarget(rec); };
  actions.appendChild(s);
```

Append helper:

```js
function selectSignalTarget(rec) {
  if (activeSignalTarget && (activeSignalTarget.ch !== rec.ch || activeSignalTarget.id !== rec.id)) {
    sendCmd({ cmd: 'p4_watch', ch: channelName(activeSignalTarget.ch), id: activeSignalTarget.id, on: false });
  }
  activeSignalTarget = { ch: rec.ch, id: rec.id };
  activeSignalSamples = [];
  activeSignalHints = [];
  signalTarget.textContent = `${channelName(rec.ch)} ${idText(rec.id)}`;
  sendCmd({ cmd: 'p4_watch', ch: channelName(rec.ch), id: rec.id, on: true });
  sendCmd({ cmd: 'p4_hints', ch: channelName(rec.ch), id: rec.id });
}
```

- [ ] **Step 6: Parse P4 WebSocket messages**

Append to `data/analyzer/app.js`:

```js
function renderSignalHints() {
  if (!activeSignalHints.length) {
    signalHintsEl.textContent = '暂无候选';
    return;
  }
  clearNode(signalHintsEl);
  for (const hint of activeSignalHints) {
    const div = document.createElement('div');
    div.className = 'hint';
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = '带入';
    button.onclick = () => {
      document.getElementById('signal-start-bit').value = hint.startBit;
      document.getElementById('signal-bit-length').value = hint.bitLength;
    };
    div.textContent = `${hint.kind} bit=${hint.startBit}/${hint.bitLength} conf=${hint.confidence}% ${hint.evidence}`;
    div.appendChild(button);
    signalHintsEl.appendChild(div);
  }
}

function parseSignalSamples(buf, dv, count) {
  let o = 3;
  activeSignalSamples = [];
  for (let i = 0; i < count; i++) {
    if (o + 16 > buf.byteLength) break;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const tsOffsetMs = dv.getUint32(o, true); o += 4;
    const dlc = dv.getUint8(o); o += 1;
    const data = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
    if (!activeSignalTarget || ch !== activeSignalTarget.ch || id !== activeSignalTarget.id) continue;
    activeSignalSamples.push({ ch, id, tsOffsetMs, dlc, data });
  }
}

function parseSignalHints(buf, dv, count) {
  let o = 3;
  activeSignalHints = [];
  for (let i = 0; i < count; i++) {
    if (o + 55 > buf.byteLength) break;
    const kind = dv.getUint8(o); o += 1;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const startBit = dv.getUint8(o); o += 1;
    const bitLength = dv.getUint8(o); o += 1;
    const confidence = dv.getUint8(o); o += 1;
    const evidenceBytes = new Uint8Array(buf.slice(o, o + 48)); o += 48;
    if (!activeSignalTarget || ch !== activeSignalTarget.ch || id !== activeSignalTarget.id) continue;
    const zero = evidenceBytes.indexOf(0);
    const end = zero >= 0 ? zero : evidenceBytes.length;
    const evidence = new TextDecoder().decode(evidenceBytes.slice(0, end));
    activeSignalHints.push({ kind: kind === 1 ? 'mux' : kind === 2 ? 'counter' : 'checksum', startBit, bitLength, confidence, evidence });
  }
  renderSignalHints();
}

function parseP4(buf) {
  if (buf.byteLength < 3) return;
  const dv = new DataView(buf);
  const subtype = dv.getUint8(1);
  const count = dv.getUint8(2);
  if (subtype === 0x01) parseSignalSamples(buf, dv, count);
  else if (subtype === 0x02) parseSignalHints(buf, dv, count);
}
```

In `ws.onmessage`, add:

```js
    if (type === 0x04) parseP4(ev.data);
```

- [ ] **Step 7: Apply form definition to current samples**

Append to `data/analyzer/app.js`:

```js
function applySignalDefinition() {
  const spec = parseSignalSpecFromForm();
  if (!spec || !activeSignalTarget) return;
  signalWorkspace.set(targetSignalKey(spec), spec);
  const decoded = [];
  for (const sample of activeSignalSamples) {
    const hit = decodeSignalSpec(spec, sample);
    if (hit) decoded.push({ x: sample.tsOffsetMs, value: hit.value, raw: hit.raw });
  }
  drawSignalChart(decoded);
}

document.getElementById('signal-apply-btn').onclick = applySignalDefinition;
```

- [ ] **Step 8: Add import/export and common-save/load**

Append to `data/analyzer/app.js`:

```js
function exportSignalWorkspace() {
  const payload = {
    version: 1,
    exported_at: new Date().toISOString(),
    signals: Array.from(signalWorkspace.values()),
  };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'can-analyzer-signals.json';
  a.click();
  URL.revokeObjectURL(url);
}

async function importSignalWorkspace(file) {
  const text = await file.text();
  const parsed = JSON.parse(text);
  if (parsed.version !== 1 || !Array.isArray(parsed.signals)) throw new Error('bad signal file');
  signalWorkspace.clear();
  for (const spec of parsed.signals)
    signalWorkspace.set(targetSignalKey(spec), spec);
}

async function saveCommonSignals() {
  const signals = Array.from(signalWorkspace.values()).slice(0, 32).map(spec => ({
    ch: spec.channel,
    id: spec.id,
    start_bit: spec.start_bit,
    bit_length: spec.bit_length,
    endian: spec.endian,
    signed: spec.signed,
    scale: spec.scale,
    offset: spec.offset,
    label: spec.label,
  }));
  const r = await fetch('/api/p4/common', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ signals }) });
  if (!r.ok) throw new Error('save common failed');
}

async function loadCommonSignals() {
  const r = await fetch('/api/p4/common');
  const signals = await r.json();
  for (const spec of Array.isArray(signals) ? signals : [])
    signalWorkspace.set(targetSignalKey(spec), spec);
}

document.getElementById('signal-export-btn').onclick = exportSignalWorkspace;
document.getElementById('signal-import-input').onchange = (ev) => {
  const file = ev.target.files && ev.target.files[0];
  if (file) importSignalWorkspace(file).catch(err => { p3Status.textContent = String(err.message || err); });
};
document.getElementById('signal-save-common-btn').onclick = () => saveCommonSignals().catch(err => { p3Status.textContent = String(err.message || err); });
document.getElementById('signal-load-common-btn').onclick = () => loadCommonSignals().catch(err => { p3Status.textContent = String(err.message || err); });
```

- [ ] **Step 9: Build LittleFS**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs`
Expected: SUCCESS and output lists only `/app.js`, `/index.html`, `/style.css`.

- [ ] **Step 10: Commit**

```bash
git add data/analyzer/index.html data/analyzer/style.css data/analyzer/app.js
git commit -m "feat(analyzer): add P4 signal workbench UI"
```

---

## Task 8: Full verification and review

**Files:**
- Read/verify: all P4 files
- Update: `docs/superpowers/specs/2026-06-14-can-analyzer-p4-design.md` only if plan-driven implementation reveals required wording corrections

- [ ] **Step 1: Run targeted native tests**

Run:

```bash
find . -name '._*' -delete
COPYFILE_DISABLE=1 pio test -e native -f test_signal_codec
COPYFILE_DISABLE=1 pio test -e native -f test_signal_window
COPYFILE_DISABLE=1 pio test -e native -f test_signal_hints
COPYFILE_DISABLE=1 pio test -e native -f test_common_signal_store
COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol
```

Expected: all PASS.

- [ ] **Step 2: Run full native suite**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio test -e native`
Expected: PASS — all pre-existing tests plus new P4 suites pass.

- [ ] **Step 3: Build firmware**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer`
Expected: SUCCESS.

- [ ] **Step 4: Build LittleFS image**

Run: `find . -name '._*' -delete; COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs`
Expected: SUCCESS and file list contains only `/app.js`, `/index.html`, `/style.css`.

- [ ] **Step 5: Manual browser verification**

Run the analyzer UI and verify:

1. P2/P3 tables still update and existing buttons still work.
2. Click a row `S` button and confirm Signal Workbench target switches to that `(channel,id)`.
3. Apply an 8-bit signal and confirm current value and chart update.
4. Switch endian / signed / scale / offset and confirm decoded values change accordingly.
5. Request hints and confirm hints render as candidates only, without auto-saving a signal.
6. Export JSON, clear/reload in browser, import JSON, and confirm definitions restore.
7. Save common signals to device, reload page, click “加载设备常用项”, and confirm entries return.

- [ ] **Step 6: Run code review agent**

Use `superpowers:code-reviewer` with this prompt:

```text
Review CAN analyzer P4 implementation against docs/superpowers/specs/2026-06-14-can-analyzer-p4-design.md and docs/superpowers/plans/2026-06-14-can-analyzer-p4.md. Focus on signal decode correctness (especially Motorola vs Intel), watched-window memory safety, hint false positives, common-signal persistence safety, and frontend import/export + chart behavior. Report Critical/Important/Minor. Do not edit files.
```

- [ ] **Step 7: Fix any Critical/Important review items with TDD**

For each issue: write or adjust a failing test first, run RED, implement the minimal fix, run GREEN, and commit the logical fix. Example:

```bash
git add src/analyzer/signal_codec.cpp test/test_signal_codec/test_signal_codec.cpp
git commit -m "fix(analyzer): correct Motorola 16-bit signal decode"
```

- [ ] **Step 8: Final verification after review fixes**

Run again:

```bash
find . -name '._*' -delete
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Expected: all SUCCESS / all tests pass.

- [ ] **Step 9: Update memory**

Update `/Users/csk/.cac/envs/gpt/.claude/projects/-Volumes-csk-other-esp32/memory/project_can_analyzer.md` with:

- P4 implementation status
- exact verification evidence
- remaining P5 handoff scope

- [ ] **Step 10: Complete branch / integrate work**

Use `superpowers:finishing-a-development-branch` after verification is green.

---

## Self-Review

- **Spec coverage:**
  - 手动 8/16-bit 解码、大小端、有符号、scale/offset → Task 1 + Task 7
  - watched short-window samples → Task 2 + Task 6 + Task 7
  - mux/counter/checksum 候选提示（仅提示） → Task 3 + Task 5 + Task 7
  - 浏览器 JSON 导入导出 → Task 7
  - 设备常用项持久化 → Task 4 + Task 6 + Task 7
- **Placeholder scan:** no `TODO`/`TBD`; every code-changing step includes concrete code or exact command.
- **Type consistency:** `SignalSpec`, `RawSamplePoint`, `WindowSample`, `SignalHint`, `CommonSignalSpec`, `WsSignalSampleRecord`, and `WsSignalHintRecord` names are defined before later tasks reference them.

---
