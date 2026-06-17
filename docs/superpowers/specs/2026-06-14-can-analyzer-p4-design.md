# CAN 分析仪 P4 波形与信号设计规范

> 日期：2026-06-14  
> 状态：待用户审核  
> 关联总设计：`docs/superpowers/specs/2026-06-12-can-analyzer-design.md`
> 关联前序：`docs/superpowers/specs/2026-06-13-can-analyzer-p3-design.md`

## 1. 目标与范围

P4 在 P3 的“找包”工作流之上，补齐“看信号”能力，让用户能把原始 CAN 报文字段转成可观察、可复用、可导出的信号定义。

P4 本阶段只覆盖以下范围：

1. 字节 / 16 位信号的即时数值解析与小图曲线。
2. 手动定义位段、大小端、有无符号、scale/offset。
3. 自动推断候选：mux / rolling counter / checksum，仅提示候选，不自动落盘为最终信号。
4. 信号定义的浏览器 JSON 导出 / 导入。
5. 设备端 NVS 保存少量常用信号定义，供刷新或换浏览器后继续使用。

P4 不做：

- 完整 DBC 编辑器或批量数据库管理。
- 长时历史波形存储。
- 自动生成最终信号定义并直接生效。
- 录制、回放、主动发送、扫描脚本（这些仍属于 P5）。

## 2. 架构原则

P4 采用“前端主导的 Signal Workbench”方案：后端继续提供原始帧增量与轻量候选提示，前端负责信号定义、解析、绘图、导入导出与大部分交互状态。

这样做的原则是：

- 不把 ESP32 变成完整 signal engine，避免 Core1 状态面爆炸。
- 复用 P2/P3 已稳定的数据主链路：`CapturedFrame -> IdTable -> WS 增量 -> 浏览器 records`。
- 让“找包”和“看信号”连在一起，用户可从实时表或 P3 结果区直接进入分析。
- 只把需要跨刷新保留的小集合写入 NVS，完整工作集交给浏览器 JSON 文件。

后端新增的是 **P4 hint layer**，不是新的全量分析主循环。默认实时流、P3 结果、TX 安全闸行为保持不变。

## 3. 组件边界

### 3.1 后端

后端只新增 3 类能力：

1. **P4 关注目标的短窗口样本缓存**
   - 只对前端显式订阅的 `(channel,id)` 保留最近一小段原始帧样本。
   - 用于前端请求小图曲线、当前窗口统计与候选分析。
   - 不扩展为全量历史环形数据库，避免 PSRAM 被波形需求吞掉。

2. **候选分析器**
   - `mux` 候选：识别某些字节/位段是否稳定扮演 selector，并观察不同 selector 下其他字节布局变化。
   - `rolling counter` 候选：识别小位宽循环递增字段，并给出位段范围和置信度。
   - `checksum` 候选：只做候选分数与提示，不在 P4 中宣称已识别出确切算法。

3. **少量常用 SignalSpec 持久化**
   - 提供读写接口，把少量常用信号定义写入 NVS。
   - 设备端不是完整配置库，只是“常用快捷集合”。

### 3.2 前端

前端新增 **Signal Workbench**，负责：

- 从实时表或 P3 结果区选中一个 `(channel,id)`。
- 展示候选提示。
- 手动编辑信号位段与数值解释规则。
- 按定义即时从样本中解码并绘制曲线。
- 管理完整 `SignalSpec` 集合。
- 导出 / 导入完整 JSON。
- 把少量常用项保存到设备。

前端是 P4 的主工作台；设备只提供数据与候选，不拥有最终解释权。

## 4. 数据模型

### 4.1 SignalSpec

用户最终确认的信号定义统一用 `SignalSpec` 表示：

```jsonc
{
  "version": 1,
  "label": "steer_angle",
  "channel": "A",
  "id": 1025,
  "mux": { "mode": "eq", "start_bit": 0, "bit_length": 8, "value": 1 },
  "start_bit": 8,
  "bit_length": 16,
  "endian": "intel",
  "signed": true,
  "scale": 0.1,
  "offset": -780.0,
  "color": "#4fc3f7",
  "display": "line"
}
```

字段约束：

- `channel`: `A | B`
- `id`: `0x000..0x7FF`
- `mux`: 可空；非空时表示该信号只在满足 mux 条件时有效
- `start_bit` / `bit_length`: 支持 1..64 位，P4 UI 首批重点优化 8/16 位信号
- `endian`: `intel | motorola`
- `signed`: `true | false`
- `scale` / `offset`: 前端按 `physical = raw * scale + offset` 实时换算
- `display`: `raw | line | step`

P4 不引入复杂单位系统、注释层级、枚举文本表；这些若后续需要，再单开迭代。

### 4.2 HintSpec

自动推断结果不与最终信号混用，统一表示为 `HintSpec`：

```jsonc
{
  "kind": "counter",
  "channel": "A",
  "id": 1025,
  "bit_range": { "start_bit": 4, "bit_length": 4 },
  "confidence": 0.82,
  "evidence": "14/16 transitions follow +1 modulo pattern"
}
```

字段约束：

- `kind`: `mux | counter | checksum`
- `bit_range`: 指向候选字段范围
- `confidence`: `0.0..1.0`
- `evidence`: 简短说明提示来自什么统计现象

`HintSpec` 仅用于提示，不写入设备 NVS，不直接进入最终信号定义。

### 4.3 导出 / 导入文件

浏览器导入导出使用完整 JSON 文档：

```jsonc
{
  "version": 1,
  "exported_at": "2026-06-14T12:34:56Z",
  "signals": [ ... SignalSpec ... ]
}
```

导入策略：

- 只接受 `version = 1`。
- 按 `(channel,id,label,mux,start_bit,bit_length)` 视为同一项覆盖。
- 非法字段整份拒绝并给出错误提示，不做“部分吞掉”的宽松导入。

## 5. 前端交互与页面落点

P4 不新开独立页面，而是在现有 `data/analyzer/index.html` 基础上新增一个与 P3 结果区并列的 **Signal Workbench** 区域，让“找包 -> 解码 -> 看曲线”成为连续动作。

推荐工作流：

1. 用户在实时表 `CAN_A/CAN_B` 或 P3 结果区选中某个 `(channel,id)`。
2. 右侧打开 Signal Workbench，顶部显示当前 ID 与最新 data。
3. 先展示自动候选：mux / counter / checksum，仅显示候选、置信度与简短说明。
4. 用户在编辑区手动设置 `start_bit`、`bit_length`、`endian`、`signed`、`scale`、`offset`。
5. 下方即时显示：
   - 当前值
   - 最近窗口的最小/最大值
   - 简化折线或阶梯小图
   - 最近变化频率
6. 用户可选择：
   - 仅保存在浏览器工作集
   - 保存为设备常用项
   - 导出完整 JSON

交互原则：

- 候选区绝不替用户直接创建最终信号；最多提供“带入表单”快捷操作。
- 一次聚焦 1 个 `(channel,id)`，避免在 ESP32 上维持大批并发图表订阅。
- Workbench 是分析面板，不改动主表实时更新逻辑。

## 6. 后端数据流

### 6.1 样本缓存

P4 新增轻量样本缓存，存放前端显式订阅目标的最近原始帧。

建议新增：

- `src/analyzer/signal_window.h/.cpp`
- 每个被订阅目标维护一个短窗口（例如最近 64 或 128 帧）
- 记录原始 `CapturedFrame`，不提前展开成所有位段值

这样前端切换不同 `SignalSpec` 时，后端不需要重复维护大量预解码状态；只需返回原始样本即可。

### 6.2 订阅模型

前端新增低频 JSON 控制命令，例如：

```jsonc
{ "cmd": "p4_watch", "ch": "A", "id": 1025, "on": true }
{ "cmd": "p4_hints", "ch": "A", "id": 1025 }
{ "cmd": "p4_common_save", "signals": [ ... ] }
```

约束：

- 同时 watch 的 `(channel,id)` 数量需要上限（例如 4 或 8），防止用户把所有 ID 都打开放大后端负担。
- 非 watch 目标不返回额外样本窗口。
- 样本与候选可通过新增二进制 subtype 或小型 JSON 响应返回；P4 推荐优先复用现有 WebSocket 控制通道，避免再开新的传输面。

### 6.3 候选分析器

#### mux 候选

目标是提示“哪些字段可能是 selector”，而不是完整还原协议。

初版启发式：

- 优先扫描 byte 级或 nibble 级字段。
- 若某字段取值集合较小且分组后其他字节变化模式显著不同，则记为 mux 候选。
- 返回多个候选，按置信度排序。

#### rolling counter 候选

初版只识别小位宽（4/8/16 bit）循环递增模式。

启发式：

- 在连续样本中统计 `+1 mod 2^n` 的命中率。
- 跳变、回退、重复会降低置信度。
- 结果只给出候选位段与分数，不给“已确认”结论。

#### checksum 候选

P4 只做候选提示，避免把范围扩成完整 checksum 逆向器。

启发式：

- 对尾字节/尾 nibble 优先评分。
- 若该字段与其他数据位存在高相关但不符合 counter/mux 行为，则标为 checksum 候选。
- `evidence` 只说明统计迹象，不声称识别出了 XOR/sum/CRC 具体算法。

## 7. 持久化策略

### 7.1 浏览器工作集

完整 `SignalSpec` 集合常驻浏览器内存，并支持 JSON 文件导入 / 导出。浏览器是完整工作区，适合保存较多实验性定义。

### 7.2 设备常用项

设备端 NVS 只保存少量常用 `SignalSpec`。目标不是“设备内建完整数据库”，而是支持：

- 刷新后能恢复几个常用信号
- 换浏览器 / 换电脑时仍有基础工作集

建议限制：

- 总条目数上限（例如 32 或 64）
- 每项固定大小存储
- 超限时拒绝新增并明确提示

P4 不保存 `HintSpec`，也不保存完整图表状态、临时筛选条件或候选缓存。

## 8. 测试策略

### 8.1 Native 单元测试

新增测试重点：

- `signal_decode`：
  - 8/16 位字段提取
  - intel / motorola
  - signed / unsigned
  - scale / offset
  - mux 条件命中 / 未命中
- `signal_window`：
  - watch/unwatch
  - 窗口覆盖与顺序
  - 上限控制
- `hint_counter`：
  - 递增 / 回卷 / 抖动样本
  - 非 counter 误报抑制
- `hint_mux`：
  - 小集合 selector 候选识别
  - 普通变化字段不应轻易被判成 mux
- `common_signal_store`：
  - NVS 序列化 / 反序列化
  - 容量上限
  - 损坏数据拒绝加载

### 8.2 集成验证

必须继续执行：

- `COPYFILE_DISABLE=1 pio test -e native`
- `COPYFILE_DISABLE=1 pio run -e analyzer`
- `COPYFILE_DISABLE=1 pio run -e analyzer -t buildfs`

### 8.3 手动验收

1. 进入 Web UI，确认 P2/P3 现有表格与结果区未回归。
2. 选中某个 ID，Signal Workbench 能显示最新 data 与候选提示。
3. 手动定义一个 8 位信号，数值与原始字节一致。
4. 切换到 16 位 intel / motorola，验证解码结果按预期变化。
5. 导出 JSON，清空工作集后再导入，信号定义完整恢复。
6. 保存少量常用项到设备，刷新页面后仍可重新加载。
7. 对包含明显递增 nibble/byte 的报文，看到 counter 候选提示；但不会直接生成最终信号。

## 9. 风险与约束

- 前端主导方案意味着图表窗口天然较短；若要长时历史，需等 P5 录制能力接上。
- `motorola` 位序最容易出错，必须优先靠单元测试锁死边界。
- 候选提示的目标是“缩小人工排查范围”，不是“自动解码协议”；UI 必须持续传达这一点。
- watch 数量若不设上限，用户可能无意中让后端为大量 ID 保留样本，导致 PSRAM 被挤占。
- 浏览器完整 JSON 与设备常用项会形成两级存储，UI 上必须区分“保存到设备”和“导出到文件”，避免误解。
- macOS `/Volumes` 挂载盘仍需持续清理 `._*` AppleDouble 文件，并在 PlatformIO 构建前设置 `COPYFILE_DISABLE=1`。

## 10. 分阶段实施建议

P4 实施时建议再拆成以下顺序：

1. 前端 Signal Workbench 骨架 + 手动 8/16 位解码。
2. 样本 watch 窗口与小图曲线。
3. 导入 / 导出 + 设备常用项持久化。
4. mux 候选提示。
5. counter / checksum 候选提示。

这样能先把“可用的手动解码工具”交付出来，再逐步增强自动提示能力。
