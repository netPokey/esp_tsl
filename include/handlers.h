#pragma once

#include <Arduino.h>
#include <cstring>

#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/can_driver.h"
#include "runtime_state.h"

// 业务处理层抽象基类。
// 作用：把“报文解析 / 周期任务 / 控制开关 / 对外状态投影”收口到统一接口，
// 这样 Web、串口、脚本层都只依赖这一个控制面，不需要感知底层双 CAN 细节。
class CarManagerBase
{
public:
    virtual ~CarManagerBase() = default;

    // 处理一帧来自任意总线的原始 CAN 报文。
    // 输入包含来源总线、原始帧、发送能力和共享运行态，派生类可在这里完成解析或回注。
    virtual void handleFrame(CanBusId sourceBus,
                             CanFrame &frame,
                             CanDriver *sourceDriver,
                             DualCanRuntime &runtime) = 0;

    // 执行与车辆控制相关的周期任务。
    // 当前主要用于预热维持帧，后续也可扩展为脚本调度、节流发送、状态超时补偿。
    virtual void runPeriodicTasks(CanBusId controlBus,
                                  CanDriver *controlDriver,
                                  DualCanRuntime &runtime) = 0;

    // 返回当前业务层关心的报文 ID 过滤表。
    // 主流程会把它同步给双总线驱动，尽量把无关流量挡在解析层外面。
    virtual const uint32_t *filterIds() const = 0;
    virtual uint8_t filterIdCount() const = 0;

    // 设置是否强制打开 FSD 路径。
    // 这里保留成统一入口，避免 Web / UART / 脚本层各自直接碰底层共享开关。
    void setForceFSDEnabled(bool enabled)
    {
        setForceFSDEnabledInternal(enabled);
    }

    // 读取当前强制 FSD 的外部可见状态。
    bool getForceFSDEnabled() const
    {
        return isForceFSDEnabled();
    }

    // 手动切换速度策略档位。
    // 外部输入会被限制在有效区间内，避免控制面把非法值写入处理层。
    void setSpeedProfileManual(int profile)
    {
        if (profile < 0)
            profile = 0;
        if (profile > 4)
            profile = 4;
        speedProfile = profile;
    }

    // 设置电池预热请求状态。
    // 当请求被关闭时，同时把当前激活态清掉，避免 UI 继续展示为执行中。
    void setPreconditioningRequested(bool enabled)
    {
        precondRequested = enabled;
        if (!enabled)
            precondActive = false;
    }

    // 设置紧急车辆检测开关。
    void setEmergencyDetection(bool enabled)
    {
        emergencyDetect = enabled;
    }

    // 设置 ISA 速度覆盖开关。
    void setIsaOverride(bool enabled)
    {
        isaSpeedOverride = enabled;
    }

    // 设置 ISA 提示音抑制开关。
    void setIsaSuppress(bool enabled)
    {
        isaSuppress = enabled;
    }

    // 设置 ISA 速度倍率。
    // 值被限制在 0-7，和当前控制帧里可表达的范围对齐。
    void setIsaMultiplier(uint8_t value)
    {
        if (value > 7)
            value = 7;
        isaSpeedMul = value;
    }

    // 把内部速度档位映射成人类可读名称，供 Web / UART 展示层直接使用。
    const char *speedProfileName() const
    {
        static const char *kNames[] = {"Chill", "Normal", "Hurry", "Max", "Sloth"};
        if (speedProfile < 0 || speedProfile > 4)
            return "Unknown";
        return kNames[speedProfile];
    }

    // 返回当前优先控制总线名称。
    const char *controlBusName() const
    {
        return canBusName(preferredControlBus);
    }

    // 返回当前优先控制总线枚举值。
    CanBusId controlBus() const
    {
        return preferredControlBus;
    }

    // 对外展示的累计接收帧计数。
    uint32_t frameCount = 0;

    // 对外展示的累计发送帧计数。
    uint32_t sentCount = 0;

    // 当前 UI/控制链路是否处于 FSD 已选中状态。
    bool fsdEnabled = false;

    // 当前内部速度策略档位。
    // 这是业务层统一使用的抽象档位，不直接等同于底层 Tesla 位域值。
    int speedProfile = 0;

    // 当前计算出的速度偏移值。
    // 单位沿用现有控制链路语义，供注入层直接拆位写回。
    int speedOffset = 0;

    // 是否启用紧急车辆检测相关控制位。
    bool emergencyDetect = true;

    // 是否启用 ISA 速度覆盖。
    bool isaSpeedOverride = true;

    // 是否启用 ISA 提示音抑制。
    bool isaSuppress = false;

    // ISA 速度倍率。
    // 输入来自控制面，最终影响速度偏移的放大倍数。
    uint8_t isaSpeedMul = 7;

    // 电池包电压。
    float packVoltage = 0;

    // 电池包电流。
    float packCurrent = 0;

    // 电池包功率，单位 kW。
    float packPowerKW = 0;

    // 电池 SOC 百分比。
    float socPercent = 0;

    // 电池最低温度。
    float packTempMin = 0;

    // 电池最高温度。
    float packTempMax = 0;

    // UI 侧能耗投影，单位 Wh/km。
    float whPerKm = 0;

    // 当前是否处于预热执行中。
    bool precondActive = false;

    // 外部控制面是否请求了预热。
    bool precondRequested = false;

    // 车辆当前是否允许预热。
    bool precondAllowed = false;

    // 车辆当前是否认为预热有收益。
    bool precondWorthwhile = false;

protected:
    // 当前优先作为控制回注出口的总线。
    // 谁先提供了关键控制帧，后续控制注入通常就跟随哪一条总线。
    CanBusId preferredControlBus = CanBusId::B;

private:
    void setForceFSDEnabledInternal(bool enabled)
    {
        ::setForceFSDEnabled(enabled);
    }
};

// HW4 双 CAN 业务处理器。
// 作用：
// 1. 统一解析 AP / BMS / UI 等关键报文。
// 2. 维护页面、串口和显示层所需的业务投影视图。
// 3. 在控制总线上回注速度策略、ISA 覆盖和电池预热等控制报文。
class HW4DualCanHandler : public CarManagerBase
{
public:
    // AP 跟车距离报文，用来推导当前速度策略档位。
    static constexpr uint32_t CAN_AP_FOLLOW_DIST = 1016;

    // AP 主控制报文，承载 FSD、ISA 与速度偏移等关键控制位。
    static constexpr uint32_t CAN_AP_CONTROL = 1021;

    // ISA 提示音相关报文，用于抑制提示音时做原帧修补。
    static constexpr uint32_t CAN_ISA_CHIME = 921;

    // 电池高压母线电压/电流报文。
    static constexpr uint32_t CAN_BMS_HV_BUS = 0x132;

    // 电池 SOC 报文。
    static constexpr uint32_t CAN_BMS_SOC = 0x292;

    // 电池状态报文，包含预热是否允许/是否值得等状态位。
    static constexpr uint32_t CAN_BMS_STATUS = 0x212;

    // 电池热管理报文，包含温度上下界。
    static constexpr uint32_t CAN_BMS_THERMAL = 0x312;

    // UI 能耗报文，用来提取 Wh/km。
    static constexpr uint32_t CAN_UI_ENERGY = 0x33A;

    // 行程规划/预热控制报文。
    static constexpr uint32_t CAN_TRIP_PLANNING = 0x082;

    void handleFrame(CanBusId sourceBus,
                     CanFrame &frame,
                     CanDriver *sourceDriver,
                     DualCanRuntime &runtime) override
    {
        frameCount = runtime.totalRxFrames;

        if (processIsaChimeSuppression(frame, sourceDriver, runtime, sourceBus))
            return;

        switch (frame.id)
        {
        case CAN_AP_FOLLOW_DIST:
            processFollowDistanceFrame(frame);
            break;
        case CAN_AP_CONTROL:
            processAutopilotControlFrame(sourceBus, frame, sourceDriver, runtime);
            break;
        case CAN_BMS_HV_BUS:
            processBatteryVoltageCurrentFrame(frame);
            break;
        case CAN_BMS_SOC:
            processBatterySocFrame(frame);
            break;
        case CAN_BMS_STATUS:
            processBatteryStatusFrame(frame);
            break;
        case CAN_BMS_THERMAL:
            processBatteryThermalFrame(frame);
            break;
        case CAN_UI_ENERGY:
            processEnergyFrame(frame);
            break;
        case CAN_TRIP_PLANNING:
            processTripPlanningFrame(frame);
            break;
        default:
            processMessageScriptReserved(frame);
            break;
        }
    }

    void runPeriodicTasks(CanBusId controlBus,
                          CanDriver *controlDriver,
                          DualCanRuntime &runtime) override
    {
        preferredControlBus = controlBus;
        runPreconditioningTask(controlBus, controlDriver, runtime);
        sentCount = runtime.totalTxFrames;
        frameCount = runtime.totalRxFrames;
    }

    const uint32_t *filterIds() const override
    {
        static const uint32_t ids[] = {
            CAN_AP_FOLLOW_DIST,
            CAN_AP_CONTROL,
            CAN_ISA_CHIME,
            CAN_BMS_HV_BUS,
            CAN_BMS_SOC,
            CAN_BMS_STATUS,
            CAN_BMS_THERMAL,
            CAN_UI_ENERGY,
            CAN_TRIP_PLANNING,
        };
        return ids;
    }

    uint8_t filterIdCount() const override
    {
        return 9;
    }

private:
    // 上一次向车辆发送预热维持帧的时间戳。
    unsigned long lastPreconditioningSendMs_ = 0;

    // 拦截并重发 ISA 提示音相关报文。
    // 这是一个“读到即改、原总线回注”的快速路径，命中后直接结束当前帧处理。
    bool processIsaChimeSuppression(CanFrame &frame,
                                    CanDriver *sourceDriver,
                                    DualCanRuntime &runtime,
                                    CanBusId sourceBus)
    {
        if (!isaSuppress || frame.id != CAN_ISA_CHIME || !sourceDriver || !shouldAllowCanTx())
            return false;

        frame.data[1] |= 0x20;

        uint8_t checksum = 0;
        for (int i = 0; i < 7; ++i)
            checksum = static_cast<uint8_t>(checksum + frame.data[i]);
        checksum = static_cast<uint8_t>(checksum + (CAN_ISA_CHIME & 0xFF) + (CAN_ISA_CHIME >> 8));
        frame.data[7] = checksum;

        sourceDriver->send(frame);
        runtime.noteTx(sourceBus, frame);
        sentCount = runtime.totalTxFrames;
        return true;
    }

    // 解析跟车距离档位，并映射到当前内部速度策略档位。
    void processFollowDistanceFrame(const CanFrame &frame)
    {
        const uint8_t followDistance = (frame.data[5] & 0xE0) >> 5;
        switch (followDistance)
        {
        case 1:
            speedProfile = 3;
            break;
        case 2:
            speedProfile = 2;
            break;
        case 3:
            speedProfile = 1;
            break;
        case 4:
            speedProfile = 0;
            break;
        case 5:
            speedProfile = 4;
            break;
        default:
            break;
        }
    }

    // 处理 Tesla AP 控制帧。
    // 该帧按 mux 分段承载不同语义，这里先锁定控制总线，再把不同子通道分发到独立函数。
    void processAutopilotControlFrame(CanBusId sourceBus,
                                      CanFrame &frame,
                                      CanDriver *sourceDriver,
                                      DualCanRuntime &runtime)
    {
        if (!sourceDriver)
            return;

        preferredControlBus = sourceBus;

        const uint8_t mux = readMuxID(frame);
        const bool fsdSelected = (mux == 0) ? isFSDSelectedInUI(frame) : fsdEnabled;

        if (mux == 0)
        {
            processAutopilotMux0(frame, fsdSelected, sourceDriver, runtime, sourceBus);
            return;
        }

        if (mux == 1)
        {
            processAutopilotMux1(frame, sourceDriver, runtime, sourceBus);
            return;
        }

        if (mux == 2)
        {
            processAutopilotMux2(frame, fsdSelected, sourceDriver, runtime, sourceBus);
            return;
        }
    }

    // 处理 mux0 控制片段。
    // 这里负责同步 FSD 选择态、解析速度偏移，并在需要时对关键开关位做回注。
    void processAutopilotMux0(CanFrame &frame,
                              bool fsdSelected,
                              CanDriver *sourceDriver,
                              DualCanRuntime &runtime,
                              CanBusId sourceBus)
    {
        fsdEnabled = fsdSelected;

        const int offset = static_cast<int>((frame.data[3] >> 1) & 0x3F) - 30;
        int scaledOffset = offset * static_cast<int>(isaSpeedMul);
        if (scaledOffset < 0)
            scaledOffset = 0;
        if (scaledOffset > 200)
            scaledOffset = 200;
        speedOffset = scaledOffset;

        if (!fsdSelected || !shouldAllowCanTx())
            return;

        setBit(frame, 46, true);
        setBit(frame, 60, true);
        if (emergencyDetect)
            setBit(frame, 59, true);

        sourceDriver->send(frame);
        runtime.noteTx(sourceBus, frame);
        sentCount = runtime.totalTxFrames;
    }

    // 处理 mux1 控制片段。
    // 这里主要处理 ISA 相关位和辅助控制位，属于较轻量的原帧修补路径。
    void processAutopilotMux1(CanFrame &frame,
                              CanDriver *sourceDriver,
                              DualCanRuntime &runtime,
                              CanBusId sourceBus)
    {
        if (!shouldAllowCanTx())
            return;

        setBit(frame, 19, false);
        setBit(frame, 47, true);
        if (isaSpeedOverride)
            frame.data[2] &= static_cast<uint8_t>(~0x08);

        sourceDriver->send(frame);
        runtime.noteTx(sourceBus, frame);
        sentCount = runtime.totalTxFrames;
    }

    // 处理 mux2 控制片段。
    // 这里主要改写速度档位和 ISA 速度偏移编码，是速度控制注入的核心出口。
    void processAutopilotMux2(CanFrame &frame,
                              bool fsdSelected,
                              CanDriver *sourceDriver,
                              DualCanRuntime &runtime,
                              CanBusId sourceBus)
    {
        if (!shouldAllowCanTx())
            return;

        const uint8_t mappedProfile = mapSpeedProfileToTeslaValue(speedProfile);
        frame.data[7] = static_cast<uint8_t>((frame.data[7] & 0x1F) | ((mappedProfile & 0x07) << 5));

        if (isaSpeedOverride && fsdSelected)
        {
            frame.data[0] = static_cast<uint8_t>((frame.data[0] & ~0xC0) | ((speedOffset & 0x03) << 6));
            frame.data[1] = static_cast<uint8_t>((frame.data[1] & ~0x3F) | ((speedOffset >> 2) & 0x3F));
        }

        sourceDriver->send(frame);
        runtime.noteTx(sourceBus, frame);
        sentCount = runtime.totalTxFrames;
    }

    // 把内部速度策略档位映射成 Tesla 控制帧使用的编码值。
    uint8_t mapSpeedProfileToTeslaValue(int profile) const
    {
        switch (profile)
        {
        case 3:
            return 1;
        case 2:
            return 2;
        case 1:
            return 3;
        case 0:
            return 4;
        case 4:
            return 5;
        default:
            return 3;
        }
    }

    void processBatteryVoltageCurrentFrame(const CanFrame &frame)
    {
        const uint16_t rawVoltage = static_cast<uint16_t>(frame.data[0]) |
                                    (static_cast<uint16_t>(frame.data[1]) << 8);
        const uint16_t rawCurrentUnsigned = static_cast<uint16_t>(frame.data[2]) |
                                            (static_cast<uint16_t>(frame.data[3]) << 8);

        int16_t rawCurrent = static_cast<int16_t>(rawCurrentUnsigned & 0x7FFF);
        if (rawCurrentUnsigned & 0x4000)
            rawCurrent = static_cast<int16_t>(rawCurrent - 0x8000);

        packVoltage = rawVoltage * 0.01f;
        packCurrent = rawCurrent * -0.1f;
        packPowerKW = (packVoltage * packCurrent) / 1000.0f;
    }

    void processBatterySocFrame(const CanFrame &frame)
    {
        const uint32_t raw = static_cast<uint32_t>(frame.data[1]) |
                             (static_cast<uint32_t>(frame.data[2]) << 8) |
                             (static_cast<uint32_t>(frame.data[3]) << 16);
        const uint16_t socUi = static_cast<uint16_t>((raw >> 2) & 0x03FF);
        socPercent = socUi * 0.1f;
    }

    void processBatteryStatusFrame(const CanFrame &frame)
    {
        precondAllowed = ((frame.data[0] >> 3) & 0x01) != 0;
        precondWorthwhile = ((frame.data[0] >> 5) & 0x01) != 0;
    }

    void processBatteryThermalFrame(const CanFrame &frame)
    {
        const uint32_t raw = static_cast<uint32_t>(frame.data[5]) |
                             (static_cast<uint32_t>(frame.data[6]) << 8) |
                             (static_cast<uint32_t>(frame.data[7]) << 16);
        const uint16_t rawMin = static_cast<uint16_t>((raw >> 4) & 0x01FF);
        const uint16_t rawMax = static_cast<uint16_t>((raw >> 13) & 0x01FF);

        packTempMin = rawMin * 0.25f - 25.0f;
        packTempMax = rawMax * 0.25f - 25.0f;
    }

    void processEnergyFrame(const CanFrame &frame)
    {
        const uint16_t raw = static_cast<uint16_t>(frame.data[0]) |
                             (static_cast<uint16_t>(frame.data[1]) << 8);
        whPerKm = raw * 0.625f;
    }

    void processTripPlanningFrame(const CanFrame &frame)
    {
        const bool active = (frame.data[0] & 0x01) != 0;
        const bool heating = (frame.data[0] & 0x04) != 0;
        if (!precondRequested)
            precondActive = active && heating;
    }

    void processMessageScriptReserved(const CanFrame &frame)
    {
        (void)frame;
        // 这里故意保留为空函数。
        // 后续蓝牙交互、脚本规则、远程消息编排，都可以先在这里挂输入匹配，
        // 再把命中的规则分发到独立策略函数，不需要改主循环。
    }

    void runPreconditioningTask(CanBusId controlBus,
                                CanDriver *controlDriver,
                                DualCanRuntime &runtime)
    {
        if (!precondRequested)
        {
            precondActive = false;
            return;
        }

        if (!controlDriver || !shouldAllowCanTx())
            return;

        const unsigned long now = millis();
        if (now - lastPreconditioningSendMs_ < 100)
            return;

        CanFrame frame{};
        frame.id = CAN_TRIP_PLANNING;
        frame.dlc = 8;
        memset(frame.data, 0, sizeof(frame.data));
        frame.data[0] = 0x05;

        controlDriver->send(frame);
        runtime.noteTx(controlBus, frame);
        lastPreconditioningSendMs_ = now;
        precondActive = true;
        sentCount = runtime.totalTxFrames;
    }
};