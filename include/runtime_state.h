#pragma once

#include <Arduino.h>
#include <cstring>

#include "can_frame_types.h"

// 统一标识当前工程里的总线来源。
// 作用域：驱动层、解析层、控制层和页面层都通过它识别当前帧来自哪条总线。
enum class CanBusId : uint8_t
{
    A,
    B,
    Unknown,
};

// 把总线枚举转换成稳定的人类可读标签。
inline const char *canBusName(CanBusId bus)
{
    switch (bus)
    {
    case CanBusId::A:
        return "CAN_A";
    case CanBusId::B:
        return "CAN_B";
    default:
        return "UNKNOWN";
    }
}

// 单条总线的实时运行态。
// 这里不做业务解析，只存最通用的收发统计和最近一帧快照。
struct CanBusRuntime
{
    const char *name = "UNKNOWN";
    bool online = false;
    uint32_t rxFrames = 0;
    uint32_t txFrames = 0;
    uint32_t lastId = 0;
    uint8_t lastDlc = 0;
    uint8_t lastData[8] = {};
    unsigned long lastSeenMs = 0;
    unsigned long lastInjectedMs = 0;
};

// 双 CAN 汇总运行态。
// 设计目标：把“全局统计”和“分总线统计”收敛到一个结构里，后续 WiFi、蓝牙、脚本层都直接复用它。
struct DualCanRuntime
{
    unsigned long bootMs = 0;
    uint32_t totalRxFrames = 0;
    uint32_t totalTxFrames = 0;
    CanBusRuntime busA;
    CanBusRuntime busB;

    // 初始化运行态命名和启动时间。
    void begin()
    {
        bootMs = millis();
        busA.name = "CAN_A";
        busB.name = "CAN_B";
    }

    // 返回指定总线的可写状态视图。
    CanBusRuntime &bus(CanBusId id)
    {
        return id == CanBusId::A ? busA : busB;
    }

    // 返回指定总线的只读状态视图。
    const CanBusRuntime &bus(CanBusId id) const
    {
        return id == CanBusId::A ? busA : busB;
    }

    // 由初始化流程或恢复流程显式标记某条总线是否可用。
    void markOnline(CanBusId id, bool value)
    {
        bus(id).online = value;
    }

    // 记录一帧接收到的数据。
    // 数据会同时更新全局计数、分总线计数以及“最近一帧”快照，方便页面快速展示。
    void noteRx(CanBusId id, const CanFrame &frame)
    {
        totalRxFrames++;
        CanBusRuntime &target = bus(id);
        target.online = true;
        target.rxFrames++;
        target.lastId = frame.id;
        target.lastDlc = frame.dlc <= 8 ? frame.dlc : 8;
        memset(target.lastData, 0, sizeof(target.lastData));
        memcpy(target.lastData, frame.data, target.lastDlc);
        target.lastSeenMs = millis();
    }

    // 记录一帧主动发送出去的数据。
    // 这样页面和串口看到的发送统计，和功能注入层的真实行为保持一致。
    void noteTx(CanBusId id, const CanFrame &frame)
    {
        totalTxFrames++;
        CanBusRuntime &target = bus(id);
        target.online = true;
        target.txFrames++;
        target.lastInjectedMs = millis();
        target.lastId = frame.id;
        target.lastDlc = frame.dlc <= 8 ? frame.dlc : 8;
        memset(target.lastData, 0, sizeof(target.lastData));
        memcpy(target.lastData, frame.data, target.lastDlc);
    }
};