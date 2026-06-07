# Tesla Model 3/Y CAN Bus - 电池与能量参考

> 汇总自社区 DBC 文件（joshwardell/model3dbc、onyx-m2/onyx-m2-dbc）、
> Tesla Motors Club 论坛以及逆向工程资料。
> 主要总线：**Vehicle CAN**（可通过 OBD 诊断口访问）。
> 适用于 Model 3/Y（2018-2024）。HW4 说明见文末。

---

## 1. 电池监控

### 1.1 电池包电压与电流

| CAN ID | Hex    | Message Name      | Signal                  | Bits   | Length | Factor  | Offset | Unit | Notes |
|--------|--------|-------------------|-------------------------|--------|--------|---------|--------|------|-------|
| 306    | 0x132  | BMS_hvBusStatus   | BMS_packVoltage         | 0-15   | 16 bit | 0.01    | 0      | V    | 主电池包电压 |
| 306    | 0x132  | BMS_hvBusStatus   | BMS_packCurrent         | 16-30  | 15 bit | -0.1    | 0      | A    | 有符号值，负数 = 放电 |
| 306    | 0x132  | BMS_hvBusStatus   | BMS_currentUnfiltered   | 32-47  | 16 bit | -0.05   | 822    | A    | 原始未滤波电流 |
| 306    | 0x132  | BMS_hvBusStatus   | BMS_chgTimeToFull       | 48-59  | 12 bit | 0.01667 | 0      | Hours| 充满剩余时间 |

### 1.2 电量状态（SoC）

| CAN ID | Hex    | Message Name      | Signal                     | Bits   | Length | Factor | Offset | Unit | Notes |
|--------|--------|-------------------|----------------------------|--------|--------|--------|--------|------|-------|
| 658    | 0x292  | BMS_socStatus     | BMS_socMin                 | 0-9    | 10 bit | 0.1    | 0      | %    | 最小 SoC |
| 658    | 0x292  | BMS_socStatus     | BMS_socUI                  | 10-19  | 10 bit | 0.1    | 0      | %    | 用户界面显示的 SoC |
| 658    | 0x292  | BMS_socStatus     | BMS_socMax                 | 20-29  | 10 bit | 0.1    | 0      | %    | 最大 SoC |
| 658    | 0x292  | BMS_socStatus     | BMS_socAvg                 | 30-39  | 10 bit | 0.1    | 0      | %    | 平均 SoC |
| 826    | 0x33A  | UI_rangeSOC       | UI_actualSOC               | 48-54  | 7 bit  | 1      | 0      | %    | 显示 SoC（整数） |
| 826    | 0x33A  | UI_rangeSOC       | UI_usableSOC               | 56-62  | 7 bit  | 1      | 0      | %    | 可用 SoC |

### 1.3 单体电压（Brick）

| CAN ID | Hex    | Message Name       | Signal            | Bits   | Length | Factor  | Offset | Unit | Notes |
|--------|--------|--------------------|-------------------|--------|--------|---------|--------|------|-------|
| 1025   | 0x401  | BMS_brickVoltages  | MUX index         | 0-7    | 8 bit  | -       | -      | -    | 多路复用索引：brick 分组索引 |
| 1025   | 0x401  | BMS_brickVoltages  | BMS_brick0..107   | varies | 16 bit | 0.0001  | 0      | V    | 96 个单体，按分组多路复用 |

**解码方式：** 报文 `0x401` 是多路复用报文。Byte 0 是索引（mux selector）。每帧携带 3 个 brick 电压（每个 16 bit）。必须收集全部 mux 帧，才能得到全部 96 个单体电压。

### 1.4 单体最小/最大电压与温度

| CAN ID | Hex    | Message Name    | Signal                  | Bits   | Length | Factor | Offset | Unit  | Notes |
|--------|--------|-----------------|-------------------------|--------|--------|--------|--------|-------|-------|
| 818    | 0x332  | BMS_bmbMinMax   | BMS_brickVoltageMax     | 2-13   | 12 bit | 0.002  | 0      | V     | mux=1 |
| 818    | 0x332  | BMS_bmbMinMax   | BMS_brickVoltageMin     | 16-27  | 12 bit | 0.002  | 0      | V     | mux=1 |
| 818    | 0x332  | BMS_bmbMinMax   | BMS_brickNumVoltageMax  | 32-38  | 7 bit  | 1      | 1      | -     | 最大电压 brick 编号，mux=1 |
| 818    | 0x332  | BMS_bmbMinMax   | BMS_brickNumVoltageMin  | 40-46  | 7 bit  | 1      | 1      | -     | 最小电压 brick 编号，mux=1 |
| 818    | 0x332  | BMS_bmbMinMax   | BMS_thermistorTMax      | 16-23  | 8 bit  | 0.5    | -40    | DegC  | mux=0 |
| 818    | 0x332  | BMS_bmbMinMax   | BMS_thermistorTMin      | 24-31  | 8 bit  | 0.5    | -40    | DegC  | mux=0 |
| 818    | 0x332  | BMS_bmbMinMax   | BMS_modelTMax           | 32-39  | 8 bit  | 0.5    | -40    | DegC  | mux=0，BMS 建模温度 |
| 818    | 0x332  | BMS_bmbMinMax   | BMS_modelTMin           | 40-47  | 8 bit  | 0.5    | -40    | DegC  | mux=0，BMS 建模温度 |

### 1.5 电池包温度

| CAN ID | Hex    | Message Name        | Signal                   | Bits   | Length | Factor | Offset | Unit  | Notes |
|--------|--------|---------------------|--------------------------|--------|--------|--------|--------|-------|-------|
| 786    | 0x312  | BMS_thermalStatus   | BMS_packTMin             | 44-52  | 9 bit  | 0.25   | -25    | DegC  | 电池包最低温度 |
| 786    | 0x312  | BMS_thermalStatus   | BMS_packTMax             | 53-61  | 9 bit  | 0.25   | -25    | DegC  | 电池包最高温度 |
| 530    | 0x212  | BMS_status          | BMS_minPackTemperature   | 56-63  | 8 bit  | 0.5    | -40    | DegC  | 快速读取的最低电池包温度 |

### 1.6 衰减 / 容量

| CAN ID | Hex    | Message Name       | Signal                       | Bits   | Length | Factor | Offset | Unit | Notes |
|--------|--------|--------------------|------------------------------|--------|--------|--------|--------|------|-------|
| 658    | 0x292  | BMS_socStatus      | BMS_initialFullPackEnergy    | 40-49  | 10 bit | 0.1    | 0      | kWh  | 出厂容量（可能是硬编码默认值） |
| 850    | 0x352  | BMS_energyStatus   | BMS_nominalFullPackEnergy    | 16-31  | 16 bit | 0.02   | 0      | kWh  | 当前满电容量（mux=0） |
| 850    | 0x352  | BMS_energyStatus   | BMS_nominalEnergyRemaining   | 32-47  | 16 bit | 0.02   | 0      | kWh  | 剩余能量（mux=0） |
| 850    | 0x352  | BMS_energyStatus   | BMS_idealEnergyRemaining     | 48-63  | 16 bit | 0.02   | 0      | kWh  | 理想剩余能量（mux=0） |
| 850    | 0x352  | BMS_energyStatus   | BMS_expectedEnergyRemaining  | 32-47  | 16 bit | 0.02   | 0      | kWh  | 预期剩余能量（mux=1） |
| 850    | 0x352  | BMS_energyStatus   | BMS_energyBuffer             | 16-31  | 16 bit | 0.01   | 0      | kWh  | 缓冲能量（mux=1） |
| 850    | 0x352  | BMS_energyStatus   | BMS_energyToChargeComplete   | 48-63  | 16 bit | 0.02   | 0      | kWh  | 充满所需能量（mux=1） |
| 850    | 0x352  | BMS_energyStatus   | BMS_fullyCharged             | 15     | 1 bit  | -      | -      | bool | mux=1 |

**衰减计算公式：**
```
degradation_pct = (1 - BMS_nominalFullPackEnergy / BMS_initialFullPackEnergy) * 100
```
注意：`BMS_initialFullPackEnergy` 可能是硬编码默认值，不一定是真实出厂测量值。社区反馈该值可能不准确。`BMS_nominalFullPackEnergy` 会随温度和校准状态波动。

---

## 2. 能耗

### 2.1 功率（kW）

| CAN ID | Hex    | Message Name         | Signal                    | Bits   | Length | Factor | Offset | Unit | Notes |
|--------|--------|----------------------|---------------------------|--------|--------|--------|--------|------|-------|
| 594    | 0x252  | BMS_powerAvailable   | BMS_maxRegenPower         | 0-15   | 16 bit | 0.01   | 0      | kW   | 可用最大动能回收功率 |
| 594    | 0x252  | BMS_powerAvailable   | BMS_maxDischargePower     | 16-31  | 16 bit | 0.01   | 0      | kW   | 可用最大放电功率 |
| 594    | 0x252  | BMS_powerAvailable   | BMS_maxStationaryHeatPower| 32-41  | 10 bit | 0.01   | 0      | kW   | 静止加热功率预算 |
| 594    | 0x252  | BMS_powerAvailable   | BMS_hvacPowerBudget       | 50-59  | 10 bit | 0.02   | 0      | kW   | HVAC 功率预算 |

**瞬时功率可按以下方式计算：**
```
power_kW = BMS_packVoltage * BMS_packCurrent / 1000
```
（使用 `0x132` 中的信号）

### 2.2 驱动功率（后/前逆变器）

| CAN ID | Hex    | Message Name   | Signal       | Notes |
|--------|--------|----------------|--------------|-------|
| 614    | 0x266  | RearTorque     | RearPower    | 后电机功率（kW） |
| 741    | 0x2E5  | FrontTorque    | FrontPower   | 前电机功率（kW）——仅 AWD |

（精确 bit 位置随 DBC 版本变化；以 joshwardell/model3dbc 当前定义为准）

### 2.3 能耗（Wh/km）

| CAN ID | Hex    | Message Name    | Signal               | Bits   | Length | Factor | Offset | Unit   | Notes |
|--------|--------|-----------------|----------------------|--------|--------|--------|--------|--------|-------|
| 826    | 0x33A  | UI_rangeSOC     | UI_ratedConsumption  | 32-41  | 10 bit | 0.625  | 0      | Wh/km  | 额定能耗 |
| 826    | 0x33A  | UI_rangeSOC     | UI_expectedRange     | 0-9    | 10 bit | 1.6    | 0      | km     | 预期续航 |
| 826    | 0x33A  | UI_rangeSOC     | UI_idealRange        | 16-25  | 10 bit | 1.6    | 0      | km     | 理想续航 |

### 2.4 生命周期能量计数器

| CAN ID | Hex    | Message Name                 | Signal                       | Bits   | Length | Factor | Offset | Unit | Notes |
|--------|--------|------------------------------|------------------------------|--------|--------|--------|--------|------|-------|
| 978    | 0x3D2  | BMS_kwhCounter               | BMS_kwhDischargeTotal        | 0-31   | 32 bit | 0.001  | 0      | kWh  | 总放电量 |
| 978    | 0x3D2  | BMS_kwhCounter               | BMS_kwhChargeTotal           | 32-63  | 32 bit | 0.001  | 0      | kWh  | 总充电量 |
| 1010   | 0x3F2  | BMS_kwhCountersMultiplexed   | BMS_acChargerKwhTotal        | 8-39   | 32 bit | 0.001  | 0      | kWh  | AC 充电总量（mux=0） |
| 1010   | 0x3F2  | BMS_kwhCountersMultiplexed   | BMS_dcChargerKwhTotal        | 8-39   | 32 bit | 0.001  | 0      | kWh  | DC 充电总量（mux=1） |
| 1010   | 0x3F2  | BMS_kwhCountersMultiplexed   | BMS_kwhRegenChargeTotal      | 8-39   | 32 bit | 0.001  | 0      | kWh  | 动能回收总量（mux=2） |
| 1010   | 0x3F2  | BMS_kwhCountersMultiplexed   | BMS_kwhDriveDischargeTotal   | 8-39   | 32 bit | 0.001  | 0      | kWh  | 驱动放电总量（mux=3） |

---

## 3. 电池预热

### 3.1 能否通过 CAN 触发电池预热？

**可以。** S3XY Buttons/Commander 产品（enhauto.com）和社区逆向资料均已确认这一点。

**工作方式：** 当车辆导航到 Supercharger 时，MCU 会在 Vehicle CAN 总线上发送 `0x082`（UI_tripPlanning）。BMS 读取 `UI_requestActiveBatteryHeating` 后开始预热。通过注入（spoofing）这条报文，可以在不实际导航到充电站的情况下触发预热。

### 3.2 预热触发信号

| CAN ID | Hex    | Message Name      | Signal                          | Bits | Length | Range | Notes |
|--------|--------|-------------------|---------------------------------|------|--------|-------|-------|
| 130    | 0x082  | UI_tripPlanning   | UI_tripPlanningActive           | 0    | 1 bit  | 0-1   | 设为 1，表示路线规划处于激活状态 |
| 130    | 0x082  | UI_tripPlanning   | UI_requestActiveBatteryHeating  | 2    | 1 bit  | 0-1   | **设为 1 以触发预热** |

**触发电池预热：**
1. 在 Vehicle CAN 总线上注入 CAN message ID `0x082`
2. 设置 bit 0（`UI_tripPlanningActive`）= 1
3. 设置 bit 2（`UI_requestActiveBatteryHeating`）= 1
4. 周期性发送（原始报文也会由 MCU 周期广播）
5. BMS 会像车辆正在导航到 Supercharger 一样开始加热电池

**重要说明：**
- 这是在伪装 MCU 的报文。真实 MCU 也会发送 `0x082`，所以需要用更高频率发送，或采用合适的中间人方式抑制原始报文。
- BMS 控制实际加热逻辑；这里仅是发起请求。温度目标和加热速率由 BMS 决定。
- `BMS_preconditionAllowed`（`0x212`，bit 3）表示 BMS 是否会接受预热请求。
- `BMS_activeHeatingWorthwhile`（`0x212`，bit 5）表示在当前温度下加热是否有收益。

### 3.3 预热监控信号

| CAN ID | Hex    | Message Name        | Signal                       | Bits   | Length | Factor | Offset | Unit  | Notes |
|--------|--------|---------------------|------------------------------|--------|--------|--------|--------|-------|-------|
| 530    | 0x212  | BMS_status          | BMS_preconditionAllowed      | 3      | 1 bit  | -      | -      | bool  | BMS 允许预热 |
| 530    | 0x212  | BMS_status          | BMS_activeHeatingWorthwhile  | 5      | 1 bit  | -      | -      | bool  | 加热有收益 |
| 530    | 0x212  | BMS_status          | BMS_keepWarmRequest          | 30     | 1 bit  | -      | -      | bool  | 保温请求激活 |
| 786    | 0x312  | BMS_thermalStatus   | BMS_powerDissipation         | 0-9    | 10 bit | 0.02   | 0      | kW    | 当前热管理功率 |
| 786    | 0x312  | BMS_thermalStatus   | BMS_flowRequest              | 10-16  | 7 bit  | 0.3    | 0      | LPM   | 冷却液流量请求 |
| 786    | 0x312  | BMS_thermalStatus   | BMS_inletActiveHeatTargetT   | 35-43  | 9 bit  | 0.25   | -25    | DegC  | 主动加热目标温度 |
| 786    | 0x312  | BMS_thermalStatus   | BMS_inletActiveCoolTargetT   | 17-25  | 9 bit  | 0.25   | -25    | DegC  | 主动冷却目标温度 |
| 786    | 0x312  | BMS_thermalStatus   | BMS_inletPassiveTargetT      | 26-34  | 9 bit  | 0.25   | -25    | DegC  | 被动目标温度 |

---

## 4. 其他 BMS 信号

### 4.1 驱动限制（电池侧施加）

| CAN ID | Hex    | Message Name     | Signal                  | Bits   | Length | Factor | Offset | Unit |
|--------|--------|------------------|-------------------------|--------|--------|--------|--------|------|
| 722    | 0x2D2  | BMS_driveLimits  | BMS_minBusVoltage       | 0-15   | 16 bit | 0.01   | 0      | V    |
| 722    | 0x2D2  | BMS_driveLimits  | BMS_maxBusVoltage       | 16-31  | 16 bit | 0.01   | 0      | V    |
| 722    | 0x2D2  | BMS_driveLimits  | BMS_maxChargeCurrent    | 32-45  | 14 bit | 0.1    | 0      | A    |
| 722    | 0x2D2  | BMS_driveLimits  | BMS_maxDischargeCurrent | 48-61  | 14 bit | 0.128  | 0      | A    |

### 4.2 接触器与高压状态

| CAN ID | Hex    | Message Name           | Signal                   | Bits  | Length | Notes |
|--------|--------|------------------------|--------------------------|-------|--------|-------|
| 530    | 0x212  | BMS_status             | BMS_hvState              | 16-18 | 3 bit  | 高压母线状态 |
| 530    | 0x212  | BMS_status             | BMS_contactorState       | 8-10  | 3 bit  | 接触器状态 |
| 522    | 0x20A  | HVP_contactorState     | HVP_packContactorSetState| 8-11  | 4 bit  | 电池包接触器 |
| 562    | 0x232  | BMS_contactorRequest   | BMS_packContactorRequest | 3-5   | 3 bit  | 接触器请求 |

### 4.3 充电状态

| CAN ID | Hex    | Message Name     | Signal                     | Bits   | Length | Factor    | Offset | Unit |
|--------|--------|------------------|----------------------------|--------|--------|-----------|--------|------|
| 516    | 0x204  | PCS_chgStatus    | PCS_chgMainState           | 0-3    | 4 bit  | -         | -      | -    |
| 690    | 0x2B2  | BMS_chargerReq   | BMS_acChargePowerRequest   | 0-15   | 16 bit | 0.001     | 0      | kW   |
| 317    | 0x13D  | CP_chargeStatus  | CP_hvChargeStatus          | 0-2    | 3 bit  | -         | -      | -    |
| 669    | 0x29D  | CP_dcChargeStatus| CP_evseOutputDcVoltage     | 16-28  | 13 bit | 0.0732422 | 0      | V    |
| 669    | 0x29D  | CP_dcChargeStatus| CP_evseOutputDcCurrent     | 0-14   | 15 bit | 0.0732467 | 0      | A    |

---

## 5. HW4 / 新款车辆说明

- DBC 文件（joshwardell、onyx-m2）主要基于 2018-2023 Model 3/Y（HW3）整理。
- **HW4（2024+ Highland Model 3、2025+ Model Y Juniper）：** Tesla 经常通过固件更新调整 CAN 信号。核心 BMS 报文（`0x132`、`0x292`、`0x352`、`0x401`）被认为基本稳定，因为它们属于 BMS 到 VCU 的基础通信。
- joshwardell DBC 在 2024 年 4 月更新过，对近期固件变更有部分支持。
- **HW4 关键风险：** 某些 message ID 可能已偏移，信号 bit 位置可能变化，也可能新增 CRC/counter 字段。必须基于具体车辆和固件版本的实车抓包验证。
- Model 3/Y 的 OBD 诊断口暴露的是 **Vehicle CAN bus**（有时称为 “party bus” 或 CAN3）。BMS 流量就在这条总线上。

---

## 6. CAN 总线架构（Model 3/Y）

| Bus Name    | Description | Key ECUs |
|-------------|-------------|----------|
| Vehicle CAN | 主动力总成总线（OBD port） | BMS, VCU, Drive Inverters, PCS |
| Chassis CAN | 悬架、制动、转向 | ESP, EPS, air suspension |
| Body CAN    | HVAC、车门、灯光、安全系统 | BCM, climate, seats |
| Autopilot   | 摄像头、雷达、AP computer | AP ECU, cameras |
| Infotainment| MCU、显示屏、音频 | MCU, amplifier |
| Ethernet    | 高带宽数据 | Cameras to AP |

电池/BMS 信号位于 **Vehicle CAN bus**。

---

## 7. 关键 DBC 文件来源

1. **joshwardell/model3dbc** - https://github.com/joshwardell/model3dbc（373+ stars，最全面）
2. **onyx-m2/onyx-m2-dbc** - https://github.com/onyx-m2/onyx-m2-dbc（详细且结构清晰）
3. **commaai/opendbc** - https://github.com/commaai/opendbc（通用，Tesla 专项程度较低）
4. **ScanMyTesla** - https://www.scanmytesla.com/（商业应用，不开放 DBC，但可验证信号）
5. **CSS Electronics Tesla Dashboard** - https://www.csselectronics.com/pages/tesla-data-dashboard-telematics-can-bus-grafana

---

## 8. 快速参考：核心 CAN IDs

| Hex    | Dec  | Message                | Key Data |
|--------|------|------------------------|----------|
| 0x082  | 130  | UI_tripPlanning        | **预热触发** |
| 0x132  | 306  | BMS_hvBusStatus        | 电池包电压、电流 |
| 0x212  | 530  | BMS_status             | 高压状态、接触器、预热标志 |
| 0x252  | 594  | BMS_powerAvailable     | 最大动能回收/放电功率 |
| 0x292  | 658  | BMS_socStatus          | SoC（min/max/avg/UI）、初始容量 |
| 0x2D2  | 722  | BMS_driveLimits        | 电压/电流限制 |
| 0x312  | 786  | BMS_thermalStatus      | 电池包温度、热管理目标、加热功率 |
| 0x332  | 818  | BMS_bmbMinMax          | 单体最小/最大电压、热敏电阻温度 |
| 0x33A  | 826  | UI_rangeSOC            | SoC%、续航、Wh/km 能耗 |
| 0x352  | 850  | BMS_energyStatus       | 满电/剩余能量、衰减数据 |
| 0x3D2  | 978  | BMS_kwhCounter         | 生命周期充电/放电计数器 |
| 0x3F2  | 1010 | BMS_kwhCountersMux     | AC/DC/动能回收/驱动能量总量 |
| 0x401  | 1025 | BMS_brickVoltages      | 单体电压（多路复用） |