# Tesla Model 3/Y CAN Bus - EPAS 转向助力参考

> 汇总自社区 DBC 文件（thezim/DBCTools tesla_can.dbc、joshwardell/model3dbc、
> commaai/opendbc）、gregjhogan/tesla-pre-ap-epas-patch、openpilot Tesla port，
> 以及 Tesla Motors Club / Tesla Owners Online 论坛。
> 研究日期：2026-04-03

---

## 1. 摘要

**能否通过 CAN 修改转向力度（Comfort/Standard/Sport）？**
可以，理论上可行。网关会在 CAN ID `0x101` 上发送 `GTW_epasTuneRequest`，
告诉 EPAS 使用哪种转向手感。EPAS 会通过 CAN ID `0x370`（十进制 880）上的
`EPAS_currentTuneMode` 回报当前激活模式。通过伪装/注入带有目标 tune 值的
`0x101`，可以请求不同的转向重量。

**哪个 CAN ID 控制 EPAS 转向助力等级？**
- **0x101**（257 dec）-- `GTW_epasControl` -- 包含 `GTW_epasTuneRequest`（命令）
- **0x370**（880 dec）-- `EPAS_sysStatus` -- 包含 `EPAS_currentTuneMode`（状态/回读）

**这是可写还是只读？**
- `0x101 GTW_epasTuneRequest` = 可写（网关发给 EPAS 的命令）
- `0x370 EPAS_currentTuneMode` = 只读（EPAS 发出的状态）

**是否有已知成功修改案例？**
- openpilot 项目（comma.ai）已成功通过 CAN 向 Tesla EPAS 发送转向扭矩命令，
  用于自动驾驶（pre-AP 和 AP 车辆）
- gregjhogan/tesla-pre-ap-epas-patch 项目通过修补 pre-Autopilot Tesla 的 EPAS 固件，
  启用 CAN 转向控制
- S3XY Commander（enhauto.com）可访问多种 CAN 参数，但其公开功能列表中没有明确列出转向模式修改
- **尚无公开确认案例表明有人仅通过 CAN 注入，在 Model 3/Y HW4/Juniper 上单独修改转向力度/重量**

---

## 2. 转向相关关键 CAN 报文

### 2.1 GTW_epasControl（0x101 / 257 dec）-- 可写

**来源：** Gateway（GTW）/ NEO
**长度：** 3 bytes
**总线：** Chassis CAN（不是 Vehicle CAN）
**用途：** 网关告诉 EPAS 使用哪种转向模式

| Signal               | Bits      | Length | Values |
|---------------------|-----------|--------|--------|
| GTW_epasTuneRequest | 2\|3@0+  | 3 bits | 0=FAIL_SAFE, 1=DM_COMFORT, 2=DM_STANDARD, 3=DM_SPORT, 4=RWD_COMFORT, 5=RWD_STANDARD, 6=RWD_SPORT, 7=UNAVAILABLE |
| GTW_epasPowerMode   | 6\|4@0+  | 4 bits | 0=DRIVE_OFF, 1=DRIVE_ON, ... 15=SNA |
| GTW_epasLDWEnabled  | 12\|1@0+ | 1 bit  | 0=DISABLE, 1=ENABLE |

**说明：**
- `DM_` 前缀 = Dual Motor（AWD），`RWD_` 前缀 = Rear Wheel Drive
- Model Y Juniper（AWD）：使用 1（COMFORT）、2（STANDARD）、3（SPORT）
- Model Y Juniper（RWD）：使用 4（COMFORT）、5（STANDARD）、6（SPORT）
- 网关会周期性发送这条报文；需要选择其中一种方式：
  - 用更高发送频率压过原报文（不理想，会产生总线竞争）
  - 在 GTW 与 EPAS 之间的 Chassis CAN 上做中间人拦截

### 2.2 EPAS_sysStatus（0x370 / 880 dec）-- 只读

**来源：** EPAS module
**长度：** 8 bytes
**总线：** Chassis CAN
**用途：** EPAS 回报当前状态

| Signal                  | Bits       | Length  | Values / Unit |
|------------------------|------------|---------|---------------|
| EPAS_currentTuneMode   | 7\|4@0+   | 4 bits  | 0=FAIL_SAFE, 1=DM_COMFORT, 2=DM_STANDARD, 3=DM_SPORT, 4=RWD_COMFORT, 5=RWD_STANDARD, 6=RWD_SPORT, 7=UNAVAILABLE |
| EPAS_eacStatus         | 55\|3@0+  | 3 bits  | 0=EAC_INHIBITED, 1=EAC_AVAILABLE, 2=EAC_ACTIVE, 3=EAC_FAULT |
| EPAS_eacErrorCode      | 23\|4@0+  | 4 bits  | 0=EAC_ERROR_IDLE, 1=EAC_ERROR_HANDS_ON, ...（16 种状态） |
| EPAS_steeringRackForce | 1\|10@0+  | 10 bits | Newtons |
| EPAS_steeringFault     | 2\|1@0+   | 1 bit   | 0=NO_FAULT, 1=FAULT |
| EPAS_steeringReduced   | 3\|1@0+   | 1 bit   | 0=NORMAL, 1=REDUCED |
| EPAS_handsOnLevel      | varies     | -       | 驾驶员握手检测等级 |
| EPAS_torsionBarTorque  | varies     | -       | 转向柱扭矩（Nm） |

### 2.3 EPB_epasControl（0x214 / 532 dec）-- 可写

**来源：** Electronic Parking Brake（EPB）
**长度：** varies
**总线：** Chassis CAN
**用途：** EPB 授权/拒绝 EPAS 电子助力控制

| Signal             | Bits      | Length | Values |
|-------------------|-----------|--------|--------|
| EPB_epasEACAllow  | varies    | 3 bits | 0=DISABLE, 1=ENABLE |

### 2.4 DAS_steeringControl（0x488 / 1160 dec）-- 可写（仅 Autopilot）

**来源：** DAS（Driver Assistance System）/ NEO / Openpilot
**长度：** 4 bytes
**总线：** Chassis CAN
**用途：** Autopilot 发送转向角/扭矩命令

| Signal                    | Bits       | Length  | Factor   | Range            | Unit |
|--------------------------|------------|---------|----------|------------------|------|
| DAS_steeringAngleRequest | 6\|15@0+  | 15 bits | 0.1      | -1638.35..1638.35| deg  |
| DAS_steeringControlType  | 23\|2@0+  | 2 bits  | -        | 0=NONE, 1=ANGLE_CONTROL, 2=RESERVED, 3=DISABLED | - |
| DAS_steeringHapticRequest| 7\|1@0+   | 1 bit   | -        | 0=IDLE, 1=ACTIVE | -    |

### 2.5 GTW_epasControl Type（0x101 extended）

| Signal               | Bits      | Length | Values |
|---------------------|-----------|--------|--------|
| GTW_epasControlType | varies    | 3 bits | 0=INHIBIT, 1=ANGLE, 2=TORQUE, 3=BOTH |

该信号启用/禁用基于 CAN 的转向控制。非 AP 车辆上，网关会将其设置为 0（INHIBIT）。
openpilot/pre-AP-patch 项目会修改该值以启用转向。

---

## 3. CAN 总线架构 -- 转向路径

### 3.1 哪条总线？

**关键点：EPAS 报文位于 Chassis CAN bus，而不是 Vehicle CAN bus。**

| Bus          | Access Point                        | Key ECUs              |
|--------------|-------------------------------------|-----------------------|
| Vehicle CAN  | OBD diagnostic port（center console）| BMS, VCU, Inverters   |
| **Chassis CAN** | **A-pillar diagnostic connector** | **EPAS, ESP, Brakes** |
| Body CAN     | 独立连接器                          | BCM, HVAC, lights     |

### 3.2 在 Model 3/Y 上访问 Chassis CAN

中控后方的标准 OBD/诊断连接器只暴露 **Vehicle CAN**。
要访问 Chassis CAN（EPAS 所在总线），需要以下方式之一：

1. **A-pillar diagnostic connector**（驾驶员脚窝，低位饰板后方）
   - 部分 Model 3/Y 在 A 柱附近有备用 CAN 连接器
   - 2023+ 车型可能已经移除此备用连接器
2. **直接接入转向柱附近的 Chassis CAN 线束**
3. **Gateway pass-through** -- 某些报文可能由网关在 Vehicle 与 Chassis CAN 之间桥接，
   但这取决于车型和固件

### 3.3 Model Y Juniper（2025+）特殊说明

- 已确认 CAN bus 仍然存在并可访问（ScanMyTesla 支持 Juniper）
- Tesla 正在新平台上转向 TDMA 网络（Cybertruck 最先采用），
  但截至 2025 年早期生产批次，Model Y Juniper 仍使用传统 CAN bus
- HW4 平台；CAN 信号 bit 位置可能与 HW3 DBC 文件不同
- **必须通过你自己车辆/固件版本的实车 CAN 抓包验证**

---

## 4. 实现思路

### 4.1 只读监控（安全）

监听 Chassis CAN 上的 `0x370`（EPAS_sysStatus），可读取：
- 当前转向模式（comfort/standard/sport）
- 转向齿条力
- 扭力杆扭矩
- 故障状态
- 握手检测等级

### 4.2 修改转向模式（需要中间人）

例如将转向力度从 Standard 改为 Sport：

1. **拦截** Chassis CAN 上、Gateway 与 EPAS 之间的 `0x101`（GTW_epasControl）
2. **修改** `GTW_epasTuneRequest` 字段为目标值：
   - 1 = DM_COMFORT（更轻的转向）
   - 2 = DM_STANDARD（默认）
   - 3 = DM_SPORT（更重的转向）
3. **转发** 修改后的帧给 EPAS
4. **验证**：读取 `0x370` 中的 `EPAS_currentTuneMode`

**替代方案（更简单但风险更高）：** 高频注入 `0x101`，覆盖网关原报文。
这会造成总线竞争，EPAS 也可能拒绝冲突命令。

### 4.3 `0x101` 的字节级构造

基于 DBC（Model S/X 时代 DBC，需要 HW4 验证）：

```
Message 0x101, DLC=3

Byte 0, bits 0-2: GTW_epasTuneRequest (3 bits)
  0 = FAIL_SAFE
  1 = DM_COMFORT
  2 = DM_STANDARD
  3 = DM_SPORT
  4 = RWD_COMFORT
  5 = RWD_STANDARD
  6 = RWD_SPORT

Byte 0, bits 3-6: GTW_epasPowerMode (4 bits)
  必须匹配当前车辆电源状态

Byte 1, bit 4: GTW_epasLDWEnabled (1 bit)
  保持与原始报文一致

Remaining: GTW_epasControlType and possibly CRC/counter
```

**警告：** HW4/Juniper 可能已在该报文中加入 CRC 和 rolling counter 字段。
如果存在这些字段，就必须计算正确 CRC 并递增 counter，否则 EPAS 会拒绝该帧。
这是新固件上常见的 Tesla 反伪装措施。

---

## 5. 风险与警告

### 5.1 安全关键系统

EPAS 是**安全关键**系统。错误 CAN 报文可能导致：
- 助力转向完全丢失
- EPAS 故障码持续存在，直到经销商重置
- 极端情况下转向锁死
- 车辆无法驾驶

### 5.2 已知风险

- **总线竞争：** 注入与网关冲突的报文可能让 EPAS 状态混乱
- **CRC/counter 拒绝：** HW4 很可能使用报文认证；无效报文会被静默丢弃
- **固件更新：** Tesla OTA 更新可能随时改变 CAN 报文格式
- **保修失效：** CAN bus 修改可被检测，且会导致保修失效
- **EPAS 变砖：** pre-AP EPAS patch 项目明确警告：“flashing firmware can fail and brick your EPAS -- do not flash something you are not willing to pay to replace”

### 5.3 建议

1. **先从只读监控开始**：监听 Chassis CAN 上的 `0x370`
2. 抓取并解码实车 `0x101` 报文，确认你的 Juniper 固件版本上的精确字节布局
3. 通过触摸屏切换转向模式时，对比抓到的 `GTW_epasTuneRequest` 值
4. 只有在完全理解报文结构、包括所有 CRC/counter 字段之后，才尝试写入
5. 在受控环境测试（车轮离地、车辆上架）

---

## 6. ESP32-C6 实现说明

当前项目在 ESP32-C6 上使用 TWAI（CAN）driver。若要加入转向模式控制：

### 6.1 需要新增的 CAN IDs

```cpp
// EPAS 转向（Chassis CAN bus!）
static constexpr uint32_t CAN_GTW_EPAS_CTRL  = 0x101; // 257 - steering mode command
static constexpr uint32_t CAN_EPB_EPAS_CTRL  = 0x214; // 532 - EAC allow
static constexpr uint32_t CAN_EPAS_SYS_STAT  = 0x370; // 880 - steering status
static constexpr uint32_t CAN_DAS_STEER_CTRL = 0x488; // 1160 - AP steering (info only)
```

### 6.2 读取当前转向模式（来自 `0x370`）

```cpp
void parseEpasSysStatus(const CanFrame &frame) {
    // EPAS_currentTuneMode: bits 7|4@0+（从 bit 7 开始的 4 bits，big-endian）
    // 按 little-endian 字节视角：byte 0, bits 4-7
    uint8_t tuneMode = (frame.data[0] >> 4) & 0x0F;
    // 0=FAIL_SAFE, 1=DM_COMFORT, 2=DM_STANDARD, 3=DM_SPORT
    // 4=RWD_COMFORT, 5=RWD_STANDARD, 6=RWD_SPORT, 7=UNAVAILABLE

    // EPAS_eacStatus: bits 55|3@0+（3 bits）
    uint8_t eacStatus = (frame.data[6] >> 7) | ((frame.data[7] & 0x03) << 1);
    // 0=INHIBITED, 1=AVAILABLE, 2=ACTIVE, 3=FAULT

    // EPAS_steeringFault: bit 2
    bool steeringFault = (frame.data[0] >> 2) & 0x01;

    // EPAS_steeringReduced: bit 3
    bool steeringReduced = (frame.data[0] >> 3) & 0x01;
}
```

### 6.3 发送转向模式请求（到 `0x101`）

```cpp
enum SteeringTune : uint8_t {
    TUNE_FAIL_SAFE    = 0,
    TUNE_DM_COMFORT   = 1,
    TUNE_DM_STANDARD  = 2,
    TUNE_DM_SPORT     = 3,
    TUNE_RWD_COMFORT  = 4,
    TUNE_RWD_STANDARD = 5,
    TUNE_RWD_SPORT    = 6,
};

void sendSteeringTuneRequest(CanDriver &driver, SteeringTune tune) {
    CanFrame pf;
    pf.id = 0x101;  // GTW_epasControl
    pf.dlc = 3;
    memset(pf.data, 0, 8);

    // GTW_epasTuneRequest: byte 0 的 bits 0-2
    pf.data[0] = (pf.data[0] & 0xF8) | (tune & 0x07);

    // GTW_epasPowerMode: byte 0 的 bits 3-6 -- 必须正确设置
    // 先从实车总线抓包，确认正确值
    // pf.data[0] |= (powerMode & 0x0F) << 3;

    // WARNING: HW4 可能需要 CRC/counter！
    // 先通过实车抓包验证报文结构。

    driver.send(pf);
}
```

### 6.4 重要：双 CAN 总线需求

当前硬件配置只连接到**一条** CAN bus。EPAS 信号位于 **Chassis CAN**，
而电池监控位于 **Vehicle CAN**。因此需要以下方案之一：

1. **两个 CAN 接口**（ESP32-C6 只有一个 TWAI 外设，需要外接 MCP2515 或类似芯片作为第二总线）
2. **独立设备**专门接入 Chassis CAN，用于转向相关功能
3. **在两条总线之间切换**（不适合实时工作）

---

## 7. DBC 文件来源

| Source | URL | Relevance |
|--------|-----|-----------|
| thezim/DBCTools tesla_can.dbc | https://github.com/thezim/DBCTools/blob/master/Samples/tesla_can.dbc | EPAS 信号参考价值最高（Model S/X 时代，部分适用） |
| joshwardell/model3dbc | https://github.com/joshwardell/model3dbc | Model 3/Y Vehicle CAN（EPAS 细节较少） |
| commaai/opendbc | https://github.com/commaai/opendbc | Openpilot DBC 文件，包含转向控制信号 |
| BYDcar/opendbc-byd tesla_can.dbc | https://github.com/BYDcar/opendbc-byd/blob/master/tesla_can.dbc | 包含 Tesla 信号的 fork |
| GENIVI/CANdevStudio tesla_can.dbc | https://github.com/GENIVI/CANdevStudio/blob/master/src/components/cansignaldecoder/tests/dbc/tesla_can.dbc | 带 EPAS 信号的测试 DBC |
| gregjhogan/tesla-pre-ap-epas-patch | https://github.com/gregjhogan/tesla-pre-ap-epas-patch | pre-AP EPAS 固件补丁，用于 CAN 转向 |

---

## 8. 快速参考

| Hex    | Dec  | Message              | Direction | Key Signal              | Purpose |
|--------|------|----------------------|-----------|-------------------------|---------|
| 0x101  | 257  | GTW_epasControl      | GTW->EPAS | GTW_epasTuneRequest     | **设置转向模式（comfort/std/sport）** |
| 0x214  | 532  | EPB_epasControl      | EPB->EPAS | EPB_epasEACAllow        | 启用/禁用电子助力控制 |
| 0x370  | 880  | EPAS_sysStatus       | EPAS->all | EPAS_currentTuneMode    | **读取当前转向模式** |
| 0x488  | 1160 | DAS_steeringControl  | DAS->EPAS | DAS_steeringAngleRequest| Autopilot 转向命令 |

---

## 9. 下一步

1. [ ] 在 Model Y Juniper 上物理定位 Chassis CAN 连接器
2. [ ] 将 ESP32-C6（或第二 CAN 接口）连接到 Chassis CAN
3. [ ] 在通过触摸屏切换转向模式时，抓取并记录 `0x101` 和 `0x370`
4. [ ] 解码 Juniper 固件的精确字节布局（对照 DBC 验证）
5. [ ] 检查 HW4 上 `0x101` 是否存在 CRC/counter 字段
6. [ ] 如果存在 CRC，逆向 CRC 算法（很可能是 CRC-8）
7. [ ] 先实现只读监控
8. [ ] 在完整重建报文后再实现写入
9. [ ] 上路前先在举升/车轮离地状态测试