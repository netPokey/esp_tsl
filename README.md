# TeslaCAN Dual Bus Controller

一个基于 ESP32-S3 的双 CAN 控制固件。

当前工程的目标不是做一个单纯的 CAN 收发示例，而是把“车辆报文采集、功能注入、Web 控制、串口桥、BLE OTA”整理成一套可扩展的运行骨架，方便后续继续加蓝牙交互、脚本规则和更多车辆信号解析。

## 1. 当前方案概览

### 1.1 硬件拓扑

当前板级是双 CAN：

- `CAN_A`：外置 `MCP2515`
- `CAN_B`：ESP32 内建 `TWAI`
- 主控：`ESP32-S3`

### 1.2 当前固件能力

- 双 CAN 同时收包、发包、统计
- Tesla / HW4 相关报文解析与功能注入
- WiFi AP + Web 控制面
- BLE OTA 升级
- 串口桥接命令入口
- 统一运行态 `DualCanRuntime`
- 独立业务处理层 `HW4DualCanHandler`

### 1.3 当前确认的平台事实

`platformio.ini` 当前配置：

- platform: `espressif32 @ 6.5.0`
- board: `esp32s3_flash_16MB`
- framework: `arduino`
- 分区表: `default_16MB.csv`
- C++ 标准: `gnu++17`

当前分区表包含 OTA 需要的分区槽位，因此 BLE OTA 可以直接工作。

## 2. 工程目录

```text
include/
  ble_ota_service.h       BLE OTA 封装层
  can_helpers.h           通用位操作与共享控制开关
  handlers.h              车辆业务解析与控制注入核心
  lcd_display.h           显示层占位接口
  log_buffer.h            日志缓冲
  pin_config.h            工程侧统一板级引脚入口
  runtime_state.h         双 CAN 统一运行态
  uart_bridge.h           串口桥接控制面
  web/
    web_server.h          WiFi / Web 控制服务
    web_ui.h              Web 页面

src/
  main.cpp                当前正式主入口
  can.ino                 最小双 CAN 参考收发样例（非正式主入口）

libraries/private_library/pin_config.h
                         实际板级引脚定义来源
```

## 3. 运行架构

### 3.1 模块分层

当前结构可以理解成 5 层：

1. **驱动层**
   - `MCP2515Driver`
   - `TWAIDriver`

2. **运行态层**
   - `DualCanRuntime`
   - 统一存放双总线收发统计、最后一帧、在线状态

3. **业务处理层**
   - `CarManagerBase`
   - `HW4DualCanHandler`
   - 负责信号解析、状态投影、控制帧回注、周期任务

4. **控制面层**
   - `web_server.h`
   - `uart_bridge.h`
   - 后续也适合接蓝牙命令和脚本宿主

5. **输出层**
   - `lcd_display.h`
   - Web 页面
   - 串口日志
   - BLE OTA 状态输出

### 3.2 主流程数据流

当前主循环的大致链路：

```text
CAN_A / CAN_B 驱动读帧
  -> 写入 DualCanRuntime
  -> 可选串口打印原始帧
  -> 进入 HW4DualCanHandler 解析 / 改写 / 回注
  -> 周期任务运行
  -> Web / UART / LCD / BLE OTA 刷新
```

### 3.3 当前唯一正式入口

当前正式主入口是：

- `src/main.cpp`

`src/can.ino` 现在只保留为最小双 CAN 参考收发样例，不再承担正式固件入口职责，避免出现两套 `setup/loop` 并存。

## 4. 板级与引脚说明

### 4.1 板级配置来源

工程里真正的板级事实来自：

- `libraries/private_library/pin_config.h`

工程自身的：

- `include/pin_config.h`

只是做统一入口，方便业务层和驱动层引用。

### 4.2 当前适配目标

当前代码只面向：

- `T_2Can`
- `MCP2515 CAN_A + TWAI CAN_B`

如果切到其他板型或 CAN 组合，需要先确认：

- SPI 引脚
- TWAI RX/TX 引脚
- 复位与片选定义
- 板级宏是否一致

## 5. 当前已解析 / 已控制的字段

### 5.1 辅助驾驶 / 控制相关

当前处理层已经能解析或控制以下内容：

- `FSD` 当前启用状态
- `Force FSD` 强制开关
- 跟车档位映射后的 `speedProfile`
- `speedProfileName`
- `speedOffset`
- `preferred control bus`
- `emergencyDetect`
- `isaSpeedOverride`
- `isaSuppress`
- `isaSpeedMul`

### 5.2 电池相关

当前已经解析：

- `SOC`
- `packVoltage`
- `packCurrent`
- `packPowerKW`
- `packTempMin`
- `packTempMax`
- `whPerKm`
- `precondRequested`
- `precondActive`
- `precondAllowed`
- `precondWorthwhile`

### 5.3 双 CAN 运行态相关

当前运行态已经记录：

- 全局 `totalRxFrames`
- 全局 `totalTxFrames`
- `CAN_A` / `CAN_B` 各自：
  - `online`
  - `rxFrames`
  - `txFrames`
  - `lastId`
  - `lastDlc`
  - `lastData`
  - `lastSeenMs`
  - `lastInjectedMs`

### 5.4 业务层累计计数

`HW4DualCanHandler` 当前还会投影：

- `frameCount`
- `sentCount`

## 6. Web 控制面

### 6.1 工作方式

启动后，设备会建立 AP：

- SSID: `TeslaCAN`
- Password: `tesla1234`

浏览器访问：

- `http://192.168.4.1`

### 6.2 页面当前显示内容

页面当前会展示：

- 整车控制状态
- 电池解析状态
- 预热相关状态
- ISA 相关状态
- 双 CAN 汇总统计
- `CAN_A` 详情
- `CAN_B` 详情
- 控制总线最后一帧
- 运行日志

### 6.3 当前 Web 控制项

页面当前可以直接控制：

- 强制 FSD
- 电池预热请求
- 紧急车辆检测
- ISA 速度覆盖
- ISA 提示音抑制
- CAN 发送总开关
- 串口原始帧日志开关
- ISA 倍率

### 6.4 当前接口

主要接口：

- `GET /api/status`
- `POST /api/force-fsd`
- `POST /api/precond`
- `POST /api/em-detect`
- `POST /api/isa-override`
- `POST /api/isa-suppress`
- `POST /api/isa-mul`
- `POST /api/enable-print`
- `POST /api/can-tx`

说明：

- Web 控制面现在只负责控制和展示
- OTA 已经不走 Web 上传
- `CAN TX` 默认关闭，固件启动后默认进入 `LISTEN_ONLY`
- 只有在测试台确认行为正确后，才应临时打开发送总开关
- 未经测试台验证的改动，不得接入汽车总线

## 7. BLE OTA

### 7.1 当前方案

当前 OTA 方案使用：

- [`gb88/BLEOTA`](https://github.com/gb88/BLEOTA)

### 7.2 当前固件侧标识

当前初始化参数：

- BLE 设备名：`TeslaCAN-BLEOTA`
- Model：`TeslaCAN Dual CAN`
- FW Version：`0.4.0`
- Manufacturer：`csk`

### 7.3 使用方式

1. 编译生成固件 `.bin`
2. 打开 BLEOTA WebApp：
   - <https://gb88.github.io/BLEOTA/>
3. 用 Chrome / Edge 连接设备
4. 选择新的固件文件并开始升级

### 7.4 说明

BLE OTA 和 WiFi/Web 控制层是并存关系：

- WiFi 页面负责控制
- BLE 负责升级
- 两者在主循环里分别推进，各自职责独立

## 8. 串口桥协议

### 8.1 串口参数

默认宏定义：

- TX: `4`
- RX: `5`
- Baud: `115200`

### 8.2 协议格式

当前串口桥采用文本命令：

```text
CMD <verb> [arg]
```

每条命令一行，换行提交。

### 8.3 当前支持命令

- `CMD HELLO`
- `CMD STATUS`
- `CMD FSD on|off`
- `CMD MODE 0..4`
- `CMD PRECOND on|off`
- `CMD ISAOVR on|off`
- `CMD ISASUP on|off`
- `CMD STREAM on|off`
- `CMD LOG on|off`

### 8.4 串口输出类型

- `ACK ...`
- `ERR ...`
- `EVT HELLO ...`
- `EVT STATUS ...`
- `EVT BATTERY ...`
- `EVT BUS ...`
- `EVT LOG ...`

## 8.5 CAN 安全策略

当前固件增加了三层 CAN 发送保护：

- 共享运行态中存在统一的 `CAN TX` 总开关，默认值为关闭
- `MCP2515` 与 `TWAI` 驱动默认以 `LISTEN_ONLY` 模式启动
- 业务处理层和驱动层都对发送路径再次做门禁检查

建议流程：

1. 上电后先保持默认只听模式
2. 在测试台观察收包、解析状态和页面行为
3. 仅在测试台验证通过后，临时打开 `CAN TX`
4. 测试结束后重新关闭 `CAN TX`
5. 未完成测试台验证前，不要把固件接入汽车总线

## 9. 构建、烧录与监控

### 9.1 编译

```bash
pio run
```

### 9.2 烧录

```bash
pio run -t upload
```

### 9.3 串口监视

```bash
pio device monitor
```

## 10. 过滤与报文处理策略

当前处理层会把关心的 ID 下发到双总线过滤器，减少无关流量进入业务层。

当前已关注的 ID 包括：

- `CAN_AP_FOLLOW_DIST`
- `CAN_AP_CONTROL`
- `CAN_ISA_CHIME`
- `CAN_BMS_HV_BUS`
- `CAN_BMS_SOC`
- `CAN_BMS_STATUS`
- `CAN_BMS_THERMAL`
- `CAN_UI_ENERGY`
- `CAN_TRIP_PLANNING`

## 11. 当前参考文档

仓库里已经有一些辅助参考：

- `TESLA_CAN_BATTERY_REFERENCE.md`
- `TESLA_CAN_STEERING_REFERENCE.md`
- `demo/tesla-can-mod`（外部参考来源）

## 12. 已知约束

### 12.1 Web 控制面的安全性

当前 AP 账号密码是硬编码的，适合开发和调试，不适合作为最终量产方案。

### 12.2 串口桥还是轻量协议

当前串口桥更像“调试 / 集成入口”，还不是正式的消息脚本引擎。

### 12.3 页面显示依赖当前已解析字段

页面显示的是当前处理层已经稳定投影出来的数据；如果后续新增解析字段，需要同步扩展：

- `handlers.h`
- `web_server.h`
- `web_ui.h`

## 13. 后续扩展建议方向

当前结构已经适合继续往下扩：

1. **蓝牙交互层**
   - 在不碰双 CAN 主线的情况下增加 BLE 控制协议

2. **消息脚本层**
   - 在 `processMessageScriptReserved()` 入口挂脚本规则

3. **更多车辆信号解析**
   - 继续把新字段投影到 `CarManagerBase`
   - 再统一暴露给 Web / UART / LCD

4. **正式显示层**
   - 当前 `lcd_display.h` 是占位接口
   - 后续接真 LCD 时不需要重做业务层

## 14. 当前启动后你能看到什么

启动正常时，你通常会看到：

- 双 CAN 初始化结果
- WiFi AP 已启动
- Web 控制页可访问
- BLE OTA 已准备好连接
- 页面开始滚动显示运行日志和双 CAN 状态

如果只起来一条 CAN，总体仍会进入降级运行，页面会显示对应统计和状态。