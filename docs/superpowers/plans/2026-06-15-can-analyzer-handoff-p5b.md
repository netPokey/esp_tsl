# CAN 分析仪新上下文交接与 P5b 入口

> 目的：新开 Claude Code 上下文后，先读取本文，直接续接 CAN 分析仪 P5b。本文不是已批准的实现计划，而是项目交接、约束和下一步需求逻辑。

## 当前仓库状态

- 仓库：`/Volumes/csk/other/esp32`
- 当前主分支：`main`
- 当前 main tip：`17901b4 fix(analyzer): handle exact-fit record CSV chunks`
- P1–P5a 已合并 main。
- P5a worktree/分支已清理：`worktree-can-analyzer-p5a` 已删除。

## 最近已完成内容

### P5a：只读录制 / CSV 导出

已合并功能：

- `src/analyzer/recorder.{h,cpp}`：PSRAM 环形录制缓冲，手动 start/stop，满时覆盖最旧帧并累计 `dropped`。
- `src/analyzer/record_format.{h,cpp}`：CSV 表头、单帧行、chunked streaming helper。
- `src/analyzer/analyzer_web.cpp`：
  - WS 命令：`record_start` / `record_stop`
  - drain tap：仅在 `g_recorder && g_recorder->active()` 时 push 原始帧
  - `/api/status` 字段：`recording`、`record_count`、`record_capacity`、`record_dropped`
  - `GET /api/record/download`：CSV chunked 下载
- `src/can_analyzer.cpp`：PSRAM 分配并注入 recorder。
- `data/analyzer/*`：录制开始/停止、状态显示、下载 CSV UI。

关键语义：

- P5a 是只读功能，不触碰 TX 路径。
- 下载必须先停止录制；如果仍在 recording，后端返回 409。
- 这样做是为了避免 AsyncTCP 下载任务与 Core1 drain 并发读写 recorder 缓冲。
- `recordCsvFill` 已修复 raw chunk exact-fit 边界：当 buffer 容量刚好等于表头或单行长度时，应写满并返回字节数，不要求额外 NUL。

相关文档：

- P5a spec：`docs/superpowers/specs/2026-06-14-can-analyzer-p5a-design.md`
- P5a plan：`docs/superpowers/plans/2026-06-14-can-analyzer-p5a.md`

### 同批合入：WiFi / 电源控制

用户明确要求“一起合并”，所以以下内容也已进入 main：

- `docs/superpowers/specs/2026-06-14-can-analyzer-wifi-power-design.md`
- `docs/superpowers/plans/2026-06-14-can-analyzer-wifi-power.md`
- `src/analyzer/analyzer_wifi.{h,cpp}` 增加持久化 WiFi 配置等能力。
- `src/analyzer/analyzer_web.cpp` 增加 WiFi / 电源控制 API。
- `data/analyzer/*` 增加网络与电源控制 UI。
- `test/test_analyzer_wifi/test_analyzer_wifi.cpp`

## 合并后验证结果

2026-06-15 在 main 合并后已运行：

```bash
find /Volumes/csk/other/esp32 -name '._*' -delete 2>/dev/null && COPYFILE_DISABLE=1 pio test -e native
```

结果：15 组测试，115/115 通过。

```bash
find /Volumes/csk/other/esp32 -name '._*' -delete 2>/dev/null && COPYFILE_DISABLE=1 pio run -e analyzer
```

结果：SUCCESS。

```bash
find /Volumes/csk/other/esp32 -name '._*' -delete 2>/dev/null && COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

结果：SUCCESS，LittleFS 仅包含：

- `/app.js`
- `/index.html`
- `/style.css`

未做：浏览器真机交互验收、真实 CAN 台架验收。

## 当前未提交/无关脏状态

新上下文不要误删这些，除非用户明确要求：

```text
m .pio/libdeps/can/BLEOTA
m .pio/libdeps/can_b_replay/BLEOTA
m .pio/libdeps/main/BLEOTA
?? Can分析功能开发计划.md
?? docs/superpowers/plans/2026-06-14-can-analyzer-p4.md
?? docs/superpowers/specs/2026-06-14-can-analyzer-p4-design.md
```

这些是既存状态，不属于 P5b 开始前必须清理的内容。

## 工作流要求

新上下文继续时建议：

1. 先读本文。
2. 读主需求文档：`Can分析功能开发计划.md`。
3. 读 P5a spec/plan，理解已有 recorder/download 边界。
4. 使用 `superpowers:brainstorming` 明确 P5b 具体切分。
5. 使用 `superpowers:writing-plans` 写 P5b spec/plan。
6. 使用 `superpowers:using-git-worktrees` 新建隔离 worktree。
7. 使用 `superpowers:subagent-driven-development` 执行计划。
8. 每个实现任务遵循 TDD：先写失败测试，再实现，再验证。
9. 完成后必须用 `superpowers:requesting-code-review` / code-reviewer 审查。
10. 合并前运行完整验证：native、firmware、buildfs。

## P5b 目标范围

P5b 是 P5 的写总线部分，至少覆盖：

1. 按时序回放录制内容。
2. 单帧发送。
3. 周期/循环发送。
4. 字节扫描 / bit 翻转扫描。
5. 发送序列/脚本可后置，除非用户明确要求 P5b 一次性包含。
6. 所有发送都必须受 TX Master Switch 与分通道 TX enable 约束。
7. UI 必须持续显示当前 TX 安全状态。
8. 回放/周期发送/扫描前需要二次确认。
9. 必须有限速，避免手滑刷爆总线。

## P5b 建议拆分

推荐不要一次把所有写总线功能塞进一个任务。建议拆成：

### P5b-1：发送安全闸与后端发送服务

目标：建立所有后续写总线功能共用的安全发送入口。

建议内容：

- 新建发送服务，例如 `src/analyzer/tx_service.{h,cpp}`。
- 输入统一结构，例如 channel/id/dlc/data/period/repeat/source。
- 所有发送都调用同一个 `canAnalyzerSendFrame` 或 `TxService::sendOnce`。
- `TxService` 内统一检查：
  - TX Master 必须 ON
  - 对应 channel TX enable 必须 ON
  - channel 必须在线/可用
  - id/dlc/data 合法
  - 发送限速
- native 测试覆盖：master off、channel off、dlc 越界、rate limit、正常发送。

注意：不要在 P5b-1 实现 UI 大功能，只先建安全后端基础。

### P5b-2：单帧发送 UI + API

目标：在浏览器手动输入 channel/id/dlc/data，发送一帧。

建议内容：

- 后端 WS/HTTP 命令解析单帧发送。
- 前端表单：channel、ID、DLC、8 字节数据。
- 发送按钮受 TX 状态影响；未开启 TX 时禁用或提示。
- 发送前明确显示“将向通道 X 发送”。
- native 测试覆盖命令解析和 tx_service 调用。
- 固件构建和 buildfs 验证。

### P5b-3：录制回放

目标：基于 P5a recorder 缓冲或下载后的上传文件，按原始时间间隔回放。

优先建议：先支持“当前 recorder 缓冲回放”，不要先做文件上传解析。

建议内容：

- recorder 已保存 `CapturedFrame.ts_us`，可按相邻帧 delta 调度。
- 回放目标通道可选：原通道 / 强制 A / 强制 B。
- 回放必须先二次确认。
- 回放过程中可停止。
- 回放受 TX Master + 分通道 TX enable + 限速约束。
- native 测试覆盖时序调度纯逻辑，不依赖真实 FreeRTOS。

### P5b-4：周期发送

目标：支持多条周期任务持续发送。

建议内容：

- 固定容量任务表，例如最多 8 条。
- 每条包含 channel/id/dlc/data/period_ms/enabled/next_due_ms。
- Web loop 中调度 due task。
- 任务启动前二次确认。
- 支持 stop 单条 / stop all。
- native 测试覆盖 due 调度、周期更新、capacity、stop。

### P5b-5：字节/bit 扫描

目标：自动枚举某个字节或 bit 值并发送，辅助逆向。

建议内容：

- 扫描任务必须有明确 target：channel/id/base data/dlc。
- byte scan：指定 byte index、start、end、step、interval_ms。
- bit scan：指定 byte index、bit index、0/1 翻转或序列。
- 必须二次确认并可停止。
- 必须受全局限速。
- native 测试覆盖枚举序列、停止、越界拒绝。

## P5b 安全要求

这是最重要的部分，不能弱化：

- 上电默认 listen-only / TX Master OFF。
- TX Master OFF 时任何单帧、周期、回放、扫描都不得发帧。
- 通道 A/B 独立 TX enable。
- UI 顶部 TX 横幅必须保持醒目。
- 回放/周期/扫描必须二次确认，且确认文案包含目标通道和风险。
- 必须有发送限速。初始建议：全局最小间隔 5–10ms，扫描/周期也不能绕过。
- 所有写总线路径必须集中走同一个安全发送函数，禁止各功能直接调用底层 driver。

## P5b 需要先确认的问题

新上下文开始 P5b brainstorming 时，优先问/决定：

1. P5b 是否先做单帧发送，还是先做回放？推荐先做 P5b-1 + P5b-2。
2. 回放输入是否只支持当前 recorder 缓冲？推荐先只支持当前缓冲。
3. 是否需要 `.csv` 上传回放？建议后置。
4. 发送限速默认值是多少？建议 10ms 最小间隔起步。
5. 是否需要 UI 密码/令牌？现状 TX API 在可信局域网假设下无鉴权；如果要接车，建议至少加简单 token 或物理确认流程。
6. P5b 是否包含发送序列/脚本？建议后置到 P5c，避免范围过大。

## 代码定位

开始 P5b 时优先看这些文件：

- `src/analyzer/analyzer_control.h`：TX master/channel enable 状态。
- `src/analyzer/analyzer_web.cpp`：WS 命令、status、HTTP 路由、主 loop。
- `src/analyzer/analyzer_web.h`：context 注入。
- `src/can_analyzer.cpp`：入口、PSRAM 分配、驱动和 Web context 接线。
- `include/drivers/can_driver.h`：底层 CAN driver 抽象。
- `include/drivers/twai_driver.h` / `include/drivers/mcp2515_driver.h`：A/B 实际驱动。
- `src/analyzer/recorder.{h,cpp}`：P5a 录制缓冲。
- `src/analyzer/record_format.{h,cpp}`：CSV 导出格式。
- `data/analyzer/app.js`：前端状态、WS、UI 逻辑。
- `data/analyzer/index.html`：前端结构。
- `data/analyzer/style.css`：前端样式。
- `test/test_ws_protocol/test_ws_protocol.cpp`：协议和 analyzer_web helper 测试模式。
- `platformio.ini`：native/analyzer env 与 build_src_filter。

## macOS / PlatformIO 注意事项

仓库在 `/Volumes` 下，macOS 会生成 AppleDouble `._*` 文件。运行 PlatformIO 前建议总是：

```bash
find . -name '._*' -delete 2>/dev/null
COPYFILE_DISABLE=1 pio test -e native
COPYFILE_DISABLE=1 pio run -e analyzer
COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs
```

如果不清理，可能出现：

- `._*.cpp` 被当源码编译导致 stray byte 错误。
- `._app.js` 被打进 LittleFS。

## 新上下文建议第一句话

可以直接对新会话说：

```text
请读取 docs/superpowers/plans/2026-06-15-can-analyzer-handoff-p5b.md，继续 CAN 分析仪 P5b。先用 brainstorming 明确 P5b-1/P5b-2 范围，再写 spec/plan，不要直接实现。
```
