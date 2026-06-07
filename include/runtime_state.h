#pragma once

#include <Arduino.h>
#include <cstring>

#include "can_frame_types.h"

// 统一标识当前工程里的总线来源。
// 作用域：驱动层、解析层、控制层和页面层都通过它识别当前帧来自哪条总线。
enum class CanBusId : uint8_t
{
    // 外接 MCP2515 所在总线。
    A,

    // ESP32 内建 TWAI 所在总线。
    B,

    // 未知来源或当前没有明确归属的总线。
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

// 单条 CAN 抓包记录。
// 固定字段、固定数据长度，避免抓包功能把内存或 JSON 输出撑爆。
struct CanCaptureEntry
{
    uint32_t sequence = 0;
    CanBusId bus = CanBusId::Unknown;
    unsigned long timestampMs = 0;
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

// 单条总线的实时运行态。
// 这里不做业务解析，只存最通用的收发统计和最近一帧快照。
struct CanBusRuntime
{
    // 展示层使用的稳定总线名称。
    const char *name = "UNKNOWN";

    // 当前是否认为这条总线在线可用。
    bool online = false;

    // 累计接收帧数。
    uint32_t rxFrames = 0;

    // 累计主动发送帧数。
    uint32_t txFrames = 0;

    // 最近一次收发涉及的帧 ID。
    uint32_t lastId = 0;

    // 最近一次收发涉及的有效数据长度。
    uint8_t lastDlc = 0;

    // 最近一次收发的载荷快照。
    uint8_t lastData[8] = {};

    // 最近一次观察到该总线有输入流量的时间戳。
    unsigned long lastSeenMs = 0;

    // 最近一次主动向该总线注入报文的时间戳。
    unsigned long lastInjectedMs = 0;
};

// 双 CAN 汇总运行态。
// 设计目标：把“全局统计”和“分总线统计”收敛到一个结构里，后续 WiFi、蓝牙、脚本层都直接复用它。
struct DualCanRuntime
{
    // 运行态初始化时记录的启动时间基准。
    unsigned long bootMs = 0;

    // 全局累计接收帧数。
    uint32_t totalRxFrames = 0;

    // 全局累计发送帧数。
    uint32_t totalTxFrames = 0;

    // CAN 抓包缓冲容量。
    // Web 页面只读取最近这部分，防止长时间运行后响应体无限增长。
    static constexpr uint16_t kCaptureCapacity = 200;

    // 当前是否写入 CAN 抓包缓冲。
    bool captureEnabled = false;

    // 抓包序号，单调递增，前端可用来判断是否有新帧。
    uint32_t captureSequence = 0;

    // 抓包环形缓冲当前写入位置。
    uint16_t captureWriteIndex = 0;

    // 抓包环形缓冲当前有效条数。
    uint16_t captureCount = 0;

    // 最近接收的 CAN 帧快照。
    CanCaptureEntry capture[kCaptureCapacity];

    // 外接 MCP2515 总线运行态。
    CanBusRuntime busA;

    // 内建 TWAI 总线运行态。
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
        CanBusRuntime &targetBusRuntime = bus(id);
        targetBusRuntime.online = true;
        targetBusRuntime.rxFrames++;
        targetBusRuntime.lastId = frame.id;
        targetBusRuntime.lastDlc = frame.dlc <= 8 ? frame.dlc : 8;
        memset(targetBusRuntime.lastData, 0, sizeof(targetBusRuntime.lastData));
        memcpy(targetBusRuntime.lastData, frame.data, targetBusRuntime.lastDlc);
        targetBusRuntime.lastSeenMs = millis();

        if (!captureEnabled)
            return;

        CanCaptureEntry &entry = capture[captureWriteIndex];
        entry.sequence = ++captureSequence;
        entry.bus = id;
        entry.timestampMs = targetBusRuntime.lastSeenMs;
        entry.id = frame.id;
        entry.dlc = targetBusRuntime.lastDlc;
        memset(entry.data, 0, sizeof(entry.data));
        memcpy(entry.data, frame.data, entry.dlc);
        captureWriteIndex = static_cast<uint16_t>((captureWriteIndex + 1) % kCaptureCapacity);
        if (captureCount < kCaptureCapacity)
            captureCount++;
    }

    // 记录一帧主动发送出去的数据。
    // 这样页面和串口看到的发送统计，和功能注入层的真实行为保持一致。
    void noteTx(CanBusId id, const CanFrame &frame)
    {
        totalTxFrames++;
        CanBusRuntime &targetBusRuntime = bus(id);
        targetBusRuntime.online = true;
        targetBusRuntime.txFrames++;
        targetBusRuntime.lastInjectedMs = millis();
        targetBusRuntime.lastId = frame.id;
        targetBusRuntime.lastDlc = frame.dlc <= 8 ? frame.dlc : 8;
        memset(targetBusRuntime.lastData, 0, sizeof(targetBusRuntime.lastData));
        memcpy(targetBusRuntime.lastData, frame.data, targetBusRuntime.lastDlc);
    }
};