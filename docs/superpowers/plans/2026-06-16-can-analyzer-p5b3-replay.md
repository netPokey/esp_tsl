# CAN Analyzer P5b-3 Replay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build safe replay of the current stopped recorder buffer using original frame timing and the existing `TxService` safety boundary.

**Architecture:** Add a focused `ReplayService` that snapshots stopped `Recorder` frames, schedules them by `CapturedFrame.ts_us` deltas, and calls `TxService::sendSingle()` for every frame. Wire it into `analyzer_web` through pending commands and status fields, inject PSRAM snapshot storage from `can_analyzer.cpp`, and add a small Web UI near recording controls.

**Tech Stack:** C++17, Arduino/ESP32, ESPAsyncWebServer, ArduinoJson, PlatformIO native Unity tests, browser JavaScript/CSS served from LittleFS.

---

## Tasks

### Task 1: ReplayService Tests and Interface

Create `test/test_replay_service/test_replay_service.cpp`, `src/analyzer/replay_service.h`, `src/analyzer/replay_service.cpp`; modify `platformio.ini` native build filter.

Requirements:
- TDD RED before implementation.
- `ReplayTarget`: `Original`, `ForceA`, `ForceB`.
- `ReplayStartResult`: `Ok`, `Busy`, `RecorderUnavailable`, `RecordingActive`, `Empty`, `TooManyFrames`.
- `ReplayStopResult`: `Ok`, `NotRunning`.
- `ReplayState`: `Idle`, `Running`, `Completed`, `Stopped`, `Failed`.
- `ReplayService::init(Recorder*, TxService*, CapturedFrame*, size_t)`.
- `start()` rejects running, missing recorder/tx/storage, active recorder, empty recorder, count > capacity.
- Successful `start()` snapshots recorder frames old-to-new, sets running state, `sent=0`, `next_due_ms=now_ms`.
- `tick()` sends only when running and due; first frame immediately; later frames by `ts_us` delta in ms.
- Each due frame calls `TxService::sendSingle()`.
- Any non-`Ok` Tx result fails and stops replay.
- Completion after all frames sets `Completed`.
- `stop()` changes running to stopped; non-running returns `NotRunning`.
- Sub-1ms, equal, and backwards timestamps use 1ms spacing.

Commands:
```bash
COPYFILE_DISABLE=1 pio test -e native -f test_replay_service
git diff -- src/analyzer/replay_service.h src/analyzer/replay_service.cpp test/test_replay_service/test_replay_service.cpp platformio.ini
```

### Task 2: Web Replay Helpers

Modify `src/analyzer/analyzer_web.h` and `test/test_ws_protocol/test_ws_protocol.cpp`.

Requirements:
- TDD RED before helper implementation.
- Include `analyzer/replay_service.h`.
- Add `analyzerWebParseReplayTarget()` accepting only `original`, `A`, `B`.
- Add replay state string helper returning `idle`, `running`, `completed`, `stopped`, `failed`.
- Add replay start error string helper returning empty string for `Ok`, then `busy`, `recorder_unavailable`, `recording_active`, `empty_recording`, `too_many_frames`.
- Add `NATIVE_BUILD` wrappers for tests.

Commands:
```bash
COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol
git diff -- src/analyzer/analyzer_web.h test/test_ws_protocol/test_ws_protocol.cpp
```

### Task 3: Analyzer Web Replay API and Status

Modify `src/analyzer/analyzer_web.h` and `src/analyzer/analyzer_web.cpp`.

Requirements:
- Extend `analyzerWebSetContext(..., Recorder*, TxService*, ReplayService*)`.
- Store global `ReplayService *g_replayService`.
- Add pending command types `ReplayStart`, `ReplayStop` and `ReplayTarget replay_target` field.
- Add `parseReplayStartRequest(JsonDocument&, ReplayTarget&)` using web helper.
- Add `/api/status` fields: `replay_state`, `replay_total`, `replay_sent`, `replay_error`.
- Add `POST /api/replay/start` with JSON body `{ "target": "original" | "A" | "B" }`; callback only parses and enqueues.
- Add `POST /api/replay/stop`; callback only enqueues.
- Process replay start/stop in pending command path.
- Call `g_replayService->tick(millis())` from `analyzerWebLoop()`.
- Reject `record_start` while replay is running.

Commands:
```bash
COPYFILE_DISABLE=1 pio test -e native
git diff -- src/analyzer/analyzer_web.h src/analyzer/analyzer_web.cpp
```

### Task 4: Firmware Context Injection

Modify `src/can_analyzer.cpp`.

Requirements:
- Include `analyzer/replay_service.h`.
- Add PSRAM `CapturedFrame *g_replayStorage` and `ReplayService g_replayService`.
- Allocate replay storage with `kRecordCapacity` frames.
- Initialize replay service with recorder, tx service, storage, capacity.
- Pass replay service into `analyzerWebSetContext()`.

Commands:
```bash
COPYFILE_DISABLE=1 pio run -e analyzer
git diff -- src/can_analyzer.cpp
```

### Task 5: Frontend Replay UI

Modify `data/analyzer/index.html`, `data/analyzer/app.js`, `data/analyzer/style.css`.

Requirements:
- Add replay controls near record controls: target select, start, stop, status.
- `refreshTxBanner()` calls `updateReplayControls(s)` after `paintRecordStatus(s)`.
- Disable start when recording, no record, or replay running.
- Disable stop unless replay running.
- Start handler confirms with target and multi-frame CAN send warning, POSTs `/api/replay/start`, displays submitted state.
- Stop handler POSTs `/api/replay/stop`, displays submitted state.
- Keep top TX banner behavior.

Commands:
```bash
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
git diff -- data/analyzer/index.html data/analyzer/app.js data/analyzer/style.css
```

### Task 6: Full Verification and Review

Commands:
```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_replay_service
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Review requirements:
- Full code review against spec and this plan.
- Confirm no direct `CanDriver::send()` outside `TxService`.
- Confirm HTTP callbacks do not touch CAN driver.
- Confirm no CSV upload, periodic sending, scanning, scripts, replay loop, replay speed, token, or persistent config.
