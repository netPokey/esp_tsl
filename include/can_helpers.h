#pragma once

#include "can_frame_types.h"
#include "shared_types.h"

// 强制 FSD 运行态开关的全局存储。
// 设计成函数静态对象，是为了兼容当前头文件组织方式和工具链配置。
inline Shared<bool> &forceFSDRuntimeStorage()
{
    static Shared<bool> value(false);
    return value;
}

// CAN 总发送开关的全局存储。
// 默认值为 false，表示固件上电后先以只听/禁发姿态运行，避免未验证逻辑直接上总线。
inline Shared<bool> &canTxEnabledRuntimeStorage()
{
    static Shared<bool> value(false);
    return value;
}

// 从共享布尔量中读取当前值。
// 这里把 native build 和设备 build 的差异收口，避免业务层直接接触 atomic 细节。
inline bool loadSharedBool(const Shared<bool> &value)
{
#ifdef NATIVE_BUILD
    return value;
#else
    return value.load();
#endif
}

// 向共享布尔量写入当前值。
inline void storeSharedBool(Shared<bool> &target, bool value)
{
#ifdef NATIVE_BUILD
    target = value;
#else
    target.store(value);
#endif
}

// 读取运行时“强制打开 FSD”状态。
inline bool isForceFSDEnabled()
{
    return loadSharedBool(forceFSDRuntimeStorage());
}

// 设置运行时“强制打开 FSD”状态。
// WiFi 页面、串口桥、后续蓝牙控制都通过这个入口改同一份状态。
inline void setForceFSDEnabled(bool enabled)
{
    storeSharedBool(forceFSDRuntimeStorage(), enabled);
}

// 读取运行时 CAN 总发送开关。
inline bool isCanTxEnabled()
{
    return loadSharedBool(canTxEnabledRuntimeStorage());
}

// 设置运行时 CAN 总发送开关。
inline void setCanTxEnabled(bool enabled)
{
    storeSharedBool(canTxEnabledRuntimeStorage(), enabled);
}

// 判断当前是否允许把控制帧真正打到物理总线。
inline bool shouldAllowCanTx()
{
    return isCanTxEnabled();
}

// 读取复用报文里的 mux 编号。
// 当前约定：使用 data[0] 的低 3 位作为子报文类型路由。
inline uint8_t readMuxID(const CanFrame &frame)
{
    return frame.data[0] & 0x07;
}

// 判断当前 UI 是否已经选中了 FSD。
// 决策顺序：编译期开关优先，然后看运行时强制开关，最后才回退到原始报文字段。
inline bool isFSDSelectedInUI(const CanFrame &frame)
{
#if defined(FORCE_FSD)
    (void)frame;
    return true;
#else
    if (isForceFSDEnabled())
        return true;
    return (frame.data[4] >> 6) & 0x01;
#endif
}

// 给旧版本 V12/V13 风格速度档位写入位域。
// 虽然当前主流程没直接调用，但它保留了后续做多车型兼容时的复用入口。
inline void setSpeedProfileV12V13(CanFrame &frame, int profile)
{
    frame.data[6] &= static_cast<uint8_t>(~0x06);
    frame.data[6] |= static_cast<uint8_t>((profile & 0x03) << 1);
}

// 按整帧位序设置任意 bit。
// 这是消息注入层最基础的位操作原语，后续脚本引擎也可以直接复用。
inline void setBit(CanFrame &frame, int bit, bool value)
{
    if (bit < 0 || bit >= 64)
        return;

    const int byteIndex = bit / 8;
    const int bitIndex = bit % 8;
    const uint8_t bitMask = static_cast<uint8_t>(1U << bitIndex);

    if (value)
        frame.data[byteIndex] |= bitMask;
    else
        frame.data[byteIndex] &= static_cast<uint8_t>(~bitMask);
}