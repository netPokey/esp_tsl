# CAN 分析仪 P2 高亮抑制时间 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 P1 双通道实时表基础上实现 P2：字节高亮淡出、位展开、多进制显示、静态抑制、准确 Delta/周期/抖动、fps/负载统计、冻结暂停、活跃度热力与排序。

**Architecture:** 后端扩展 `IdRecord` 与 `WsFrameRecord`，在 `IdTable::update()` 中计算 delta、周期估算、抖动、变化分数；新增轻量 `BusStatsTracker` 在 Core1 消费帧时统计 fps/load，并通过 `WS_MSG_BUS_STATS` 推送。前端继续使用 LittleFS 单页应用，解析扩展二进制记录，负责淡出渲染、位展开、多进制切换、静态抑制、冻结和排序。

**Tech Stack:** Arduino + PlatformIO、ESPAsyncWebServer/WebSocket、LittleFS、Unity native tests。

---

## 文件结构

- Modify: `src/analyzer/id_table.h/.cpp` — 增加 delta/周期/抖动/变化分数统计
- Create: `src/analyzer/bus_stats.h/.cpp` — 每通道 fps/load 统计
- Modify: `src/analyzer/ws_protocol.h/.cpp` — 扩展 `WsFrameRecord` 与 `WsBusStats`
- Modify: `src/analyzer/analyzer_web.h/.cpp` — 注入 stats、推送 stats、扩展记录字段
- Modify: `src/can_analyzer.cpp` — 创建 stats 并传给 web
- Modify: `data/analyzer/index.html/style.css/app.js` — P2 UI
- Tests: `test/test_id_table/test_id_table.cpp`, `test/test_bus_stats/test_bus_stats.cpp`, `test/test_ws_protocol/test_ws_protocol.cpp`

---

## Task 1: IdTable 时间统计与活跃度（TDD）

**Files:**
- Modify: `src/analyzer/id_table.h`
- Modify: `src/analyzer/id_table.cpp`
- Modify: `test/test_id_table/test_id_table.cpp`

- [ ] Step 1: 在测试中新增：第三帧后 `last_delta_us`、`period_est_us`、`jitter_us`、`change_score` 正确。
- [ ] Step 2: 跑 `pio test -e native -f test_id_table`，预期红。
- [ ] Step 3: 在 `IdRecord` 添加：`last_delta_us`, `period_est_us`, `jitter_us`, `last_change_ts`，并在 `update()` 中更新：delta=last-prev；period 首次取 delta，后续 `(old*7+delta)/8`；jitter 为 `abs(delta-period)`；任一字节变化则 `change_score++`、更新 `last_change_ts`。
- [ ] Step 4: 跑 `pio test -e native -f test_id_table`，预期绿。
- [ ] Step 5: Commit: `feat(analyzer): compute delta period jitter and activity score`

## Task 2: BusStatsTracker（TDD）

**Files:**
- Create: `src/analyzer/bus_stats.h`
- Create: `src/analyzer/bus_stats.cpp`
- Create: `test/test_bus_stats/test_bus_stats.cpp`
- Modify: `platformio.ini` native src filter

- [ ] Step 1: 写测试：通道 A/B 分开计数；1 秒窗口后 fps 更新；load_x10 根据帧位数产生非零值；dropped 透传。
- [ ] Step 2: 跑 `pio test -e native -f test_bus_stats`，预期红。
- [ ] Step 3: 实现 `BusStatsTracker::noteRx(frame)`, `update(now_ms,dropped)`, `snapshot()`；负载估算使用 `bits = 47 + dlc*8`，500kbps 下 `load_x10 = bits_per_sec * 1000 / 500000`。
- [ ] Step 4: 更新 native `build_src_filter` 加 `+<analyzer/bus_stats.cpp>`。
- [ ] Step 5: 跑 `pio test -e native -f test_bus_stats`，预期绿。
- [ ] Step 6: Commit: `feat(analyzer): track CAN fps and bus load`

## Task 3: WebSocket 协议扩展（TDD）

**Files:**
- Modify: `src/analyzer/ws_protocol.h`
- Modify: `test/test_ws_protocol/test_ws_protocol.cpp`

- [ ] Step 1: 扩展 `WsFrameRecord` 字段：`last_delta_ms`, `period_ms`, `jitter_ms`, `change_score`, `flags`；扩展 `WsBusStats` 保持 fps/load/dropped 并兼容当前测试。
- [ ] Step 2: 更新测试断言新字段序列化后保留。
- [ ] Step 3: 跑 `pio test -e native -f test_ws_protocol`，预期绿（`wsBuild*` 是 memcpy，主要验证布局）。
- [ ] Step 4: Commit: `feat(analyzer): extend WS protocol for P2 metrics`

## Task 4: 后端 P2 推送整合

**Files:**
- Modify: `src/analyzer/analyzer_web.h/.cpp`
- Modify: `src/can_analyzer.cpp`

- [ ] Step 1: `analyzerWebSetContext(FrameQueue*, IdTable*, BusStatsTracker*)`。
- [ ] Step 2: `drainQueueIntoTable()` 每帧同时 `stats.noteRx(cap)`。
- [ ] Step 3: `toWire()` 填充 P2 新字段。
- [ ] Step 4: 每 1000ms 发送 `WS_MSG_BUS_STATS`。
- [ ] Step 5: 跑 `pio run -e analyzer`，预期 SUCCESS。
- [ ] Step 6: Commit: `feat(analyzer): push P2 metrics and bus stats over websocket`

## Task 5: 前端 P2 UI

**Files:**
- Modify: `data/analyzer/index.html`
- Modify: `data/analyzer/style.css`
- Modify: `data/analyzer/app.js`

- [ ] Step 1: 添加工具栏：进制 select、静态抑制 checkbox+阈值秒、冻结 checkbox、排序 select（ID/活跃度）。
- [ ] Step 2: 解析扩展 `WsFrameRecord` 和 `WS_MSG_BUS_STATS`。
- [ ] Step 3: 字节按 `byte_age_ms` 加 `.hot/.warm` 类淡出；多进制格式化 HEX/DEC/BIN/ASCII。
- [ ] Step 4: 点击行展开 bit 视图（8 字节×8 bit）。
- [ ] Step 5: 静态抑制：所有 byte_age 超阈值且未 pinned 的行隐藏。
- [ ] Step 6: 冻结：暂停 DOM 更新但继续保持 WS 连接；解冻后恢复最新状态。
- [ ] Step 7: 活跃度热力：按 `change_score` 给行加 class，并支持按活跃度排序。
- [ ] Step 8: 顶部显示 CAN_A/B fps/load/dropped。
- [ ] Step 9: 跑 `pio run -e analyzer -t buildfs`，预期 SUCCESS。
- [ ] Step 10: Commit: `feat(analyzer): P2 frontend highlight filters stats and bit view`

## Task 6: 完整验证

- [ ] Step 1: 跑 `pio test -e native`，预期 4 组测试全绿。
- [ ] Step 2: 跑 `pio run -e analyzer`，预期 SUCCESS。
- [ ] Step 3: 跑 `pio run -e analyzer -t buildfs`，预期 SUCCESS。
- [ ] Step 4: Commit any missed files, ensure `git status --short` clean.
