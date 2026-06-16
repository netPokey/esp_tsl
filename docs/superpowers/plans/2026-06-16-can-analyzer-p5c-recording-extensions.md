# CAN Analyzer P5c Recording Extensions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ASC export and trigger recording for the current CAN analyzer recorder without adding any new CAN transmit behavior.

**Architecture:** Keep recorder formatting in focused formatter modules, add a `RecordTriggerService` that observes frames before `IdTable::update()`, and route web arm/disarm requests through existing pending command processing. UI changes stay near existing recording controls and status uses `/api/status`.

**Tech Stack:** C++17, Arduino/ESP32, ESPAsyncWebServer, ArduinoJson, PlatformIO native Unity tests, browser JavaScript/CSS served from LittleFS.

---

## Important Rules

- Follow TDD for every behavior change: write failing native tests first, then implementation.
- Do not add CAN transmit behavior in P5c.
- Do not commit implementation changes unless the user explicitly asks.
- Preserve existing CSV download behavior.
- HTTP callbacks must parse/enqueue only; recorder mutations happen in pending command / analyzer loop path.

---

## Task 1: ASC Format Tests and Formatter

**Files:**
- Create: `src/analyzer/record_asc_format.h`
- Create: `src/analyzer/record_asc_format.cpp`
- Create: `test/test_record_asc_format/test_record_asc_format.cpp`
- Modify: `platformio.ini`

Requirements:
- Add `recordAscHeader()`, `recordAscFooter()`, `recordAscLine()`, `RecordAscCursor`, `recordAscFill()`.
- Header must be exactly:
  ```text
  date Tue Jun 16 00:00:00.000 2026
  base hex  timestamps absolute
  internal events logged
  Begin Triggerblock
  ```
- Footer must be exactly `End Triggerblock\n`.
- ASC frame line format:
  ```text
     0.010500 2 321 Rx d 8 01 02 03 04 05 06 07 08
  ```
- Time is relative to first frame in seconds, 6 decimals.
- `channel 0 -> 1`, `channel 1 -> 2`, anything else -> `0`.
- ID is uppercase hex without `0x`, minimum 3 digits.
- DLC is decimal; data emits up to 8 bytes as uppercase two-digit hex separated by spaces.
- Too-small buffer returns 0 and does not write partial line/header/footer.
- `recordAscFill()` streams header, old-to-new frames, then footer; returns 0 only after footer is emitted and next call has no data.
- Add `+<analyzer/record_asc_format.cpp>` to native `build_src_filter`.

Commands:
```bash
COPYFILE_DISABLE=1 pio test -e native -f test_record_asc_format
```

Diff check:
```bash
git diff -- src/analyzer/record_asc_format.h src/analyzer/record_asc_format.cpp test/test_record_asc_format/test_record_asc_format.cpp platformio.ini
```

---

## Task 2: RecordTriggerService Tests and Implementation

**Files:**
- Create: `src/analyzer/record_trigger.h`
- Create: `src/analyzer/record_trigger.cpp`
- Create: `test/test_record_trigger/test_record_trigger.cpp`
- Modify: `platformio.ini`

Requirements:
- Define:
  - `RecordTriggerMode`: `Disabled`, `NewId`, `IdChange`, `AnyChange`.
  - `RecordTriggerState`: `Idle`, `Armed`, `Triggered`, `Failed`.
  - `RecordTriggerArmResult`: `Ok`, `RecorderUnavailable`, `AlreadyRecording`, `ReplayRunning`, `InvalidTarget`.
  - `RecordTriggerConfig { RecordTriggerMode mode; uint8_t channel; uint16_t id; }`.
- Implement `RecordTriggerService::init(Recorder*, ReplayService*, IdTable*)`.
- `arm()` rejects missing recorder, active recorder, replay running, disabled mode, invalid channel/id for `IdChange`, and missing table when a mode needs table state.
- `arm(NewId)` and `arm(AnyChange)` ignore channel/id.
- `disarm()` returns state to idle and clears errors.
- `observe()` noops unless armed.
- `observe()` must be called before `IdTable::update()` and compare against the old table record.
- `NewId` triggers when old record is not present.
- `AnyChange` triggers when old record is present and DLC or data differs.
- `IdChange` triggers only for configured channel/id and only when DLC or data differs.
- On trigger: if replay running, state `Failed`, error `replay_running`; if recorder missing, state `Failed`, error `recorder_unavailable`; if recorder active, state `Triggered`; otherwise call `Recorder::start()` and state `Triggered`.
- The triggering frame is not manually pushed into recorder.
- Add `+<analyzer/record_trigger.cpp>` to native `build_src_filter`.

Commands:
```bash
COPYFILE_DISABLE=1 pio test -e native -f test_record_trigger
```

Diff check:
```bash
git diff -- src/analyzer/record_trigger.h src/analyzer/record_trigger.cpp test/test_record_trigger/test_record_trigger.cpp platformio.ini
```

---

## Task 3: Web Helpers and Parser Tests

**Files:**
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp`

Requirements:
- Include `analyzer/record_trigger.h` in `analyzer_web.h`.
- Add helper `analyzerWebParseRecordTriggerMode()` accepting only `new_id`, `id_change`, `any_change`.
- Add string helpers:
  - state: `idle`, `armed`, `triggered`, `failed`.
  - mode: `disabled`, `new_id`, `id_change`, `any_change`.
  - arm result/error: `Ok -> ""`, then `recorder_unavailable`, `already_recording`, `replay_running`, `invalid_target`.
- Add `parseRecordTriggerArmRequest(JsonDocument&, RecordTriggerConfig&)` or a test wrapper equivalent.
- Parser behavior:
  - `new_id` and `any_change` accept no target and ignore extra `ch/id`.
  - `id_change` requires valid `ch` and standard ID.
  - ID accepts JSON number and string forms matching existing `analyzerWebParseTxId()` behavior.
  - malformed JSON fields are rejected.
- Add `NATIVE_BUILD` wrappers for tests, following existing helper wrapper style.

Commands:
```bash
COPYFILE_DISABLE=1 pio test -e native -f test_ws_protocol
```

Diff check:
```bash
git diff -- src/analyzer/analyzer_web.h test/test_ws_protocol/test_ws_protocol.cpp
```

---

## Task 4: Analyzer Web ASC API and Trigger Pending Commands

**Files:**
- Modify: `src/analyzer/analyzer_web.h`
- Modify: `src/analyzer/analyzer_web.cpp`

Requirements:
- Extend `analyzerWebSetContext(..., Recorder*, TxService*, ReplayService*, RecordTriggerService*)`, with default `nullptr` if needed for staged compile.
- Store global `RecordTriggerService *g_recordTrigger`.
- Add pending command types `RecordTriggerArm`, `RecordTriggerDisarm` and `RecordTriggerConfig record_trigger_config` field.
- Add `/api/status` fields:
  - `record_trigger_state`
  - `record_trigger_mode`
  - `record_trigger_channel`
  - `record_trigger_id`
  - `record_trigger_error`
- Add `GET /api/record/download.asc` using `RecordAscCursor` and `recordAscFill()`.
- ASC download behavior must mirror CSV download guards: recorder missing -> 404; active -> 409; empty -> 404; ok -> chunked response with `Content-Disposition: attachment; filename="can-record.asc"`.
- Add `POST /api/record/trigger/arm` JSON API. HTTP callback only parses and enqueues.
- Add `POST /api/record/trigger/disarm`. HTTP callback only enqueues.
- Process trigger arm/disarm in pending command path.
- In `drainQueueIntoTable()`, call `g_recordTrigger->observe(cap)` after stats/pretrigger/signals and before recorder push / table update, so old table state is visible and triggering frame is not force-added.
- If replay is running, `RecordTriggerService` must reject arm and trigger-start.
- Do not add any TxService or CanDriver calls.

Commands:
```bash
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
```

Diff check:
```bash
git diff -- src/analyzer/analyzer_web.h src/analyzer/analyzer_web.cpp
```

---

## Task 5: Firmware Context Injection

**Files:**
- Modify: `src/can_analyzer.cpp`

Requirements:
- Include `analyzer/record_trigger.h`.
- Add global `RecordTriggerService g_recordTrigger`.
- Initialize trigger service with recorder pointer, replay service pointer, and ID table pointer.
- Pass `&g_recordTrigger` to `analyzerWebSetContext()`.
- No new storage allocation is required for trigger service.
- If recorder or id table storage is unavailable, pass `nullptr` accordingly; arm should fail safely.

Commands:
```bash
COPYFILE_DISABLE=1 pio run -e analyzer
```

Diff check:
```bash
git diff -- src/can_analyzer.cpp
```

---

## Task 6: Frontend ASC Download and Trigger UI

**Files:**
- Modify: `data/analyzer/index.html`
- Modify: `data/analyzer/app.js`
- Modify: `data/analyzer/style.css`

Requirements:
- Add ASC download link near existing CSV link: `/api/record/download.asc`, filename handled by backend.
- Enable ASC link under same condition as CSV: not recording and `record_count > 0`.
- Add trigger recording controls near record controls:
  - mode select: `new_id`, `id_change`, `any_change`.
  - channel select A/B, enabled only for `id_change`.
  - ID input, enabled only for `id_change`.
  - Arm button, disabled when recording or replay running.
  - Disarm button, enabled when trigger state is `armed`, `triggered`, or `failed`.
  - Status text showing state, mode, target, error.
- Add arm handler POST `/api/record/trigger/arm` with canonical JSON.
- Add disarm handler POST `/api/record/trigger/disarm`.
- On successful POST, show submitted/pending text; do not claim trigger is active until status confirms.
- Keep top TX banner behavior unchanged.
- Do not add auth/token/password or any transmit UI.

Commands:
```bash
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Diff check:
```bash
git diff -- data/analyzer/index.html data/analyzer/app.js data/analyzer/style.css
```

---

## Task 7: Full Verification and Review

Commands:
```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native -f test_record_asc_format
COPYFILE_DISABLE=1 pio test -e native -f test_record_trigger
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

Review requirements:
- Confirm P5c adds no CAN transmit path.
- Confirm HTTP callbacks for trigger arm/disarm only parse/enqueue.
- Confirm ASC and CSV download both refuse active recorder.
- Confirm trigger observe runs before `IdTable::update()`.
- Confirm replay running blocks trigger arm and trigger-start.
- Confirm no ASC upload, file replay, auto-stop recording, SD/SPIFFS write, periodic sending, scanning, scripts, token, or persistent config.

---

## Implementation Notes

- Existing main worktree has unrelated local state; keep all P5c work in this worktree until integration.
- For generated AppleDouble files on macOS, run `find . -name '._*' -delete 2>/dev/null` before broad PlatformIO test runs.
- After every task, inspect `git diff` for scope creep before moving on.
