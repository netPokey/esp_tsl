#pragma once
#include "../can_frame_types.h"

// CAN 驱动统一抽象层。
// 上层逻辑只依赖这组能力，不关心底层是 MCP2515 还是 ESP32 内建 TWAI。
class CanDriver
{
public:
    virtual ~CanDriver() = default;

    // 初始化底层总线控制器，并让驱动进入可读写状态。
    // 返回 true 表示底层资源、速率、模式都已准备完成。
    virtual bool init() = 0;

    // 按一组目标 ID 配置硬件过滤器。
    // 调用方给的是关注 ID 集，驱动负责尽量把它收敛成底层控制器支持的过滤规则。
    virtual void setFilters(const uint32_t *ids, uint8_t count) = 0;

    // 尝试启用中断式收包。
    // 当前两个驱动都没有真正启用该能力，因此这里保留为统一接口。
    virtual bool enableInterrupt(void (*onReady)()) = 0;

    // 读取一帧 CAN 数据。
    // 返回 true 表示 `frame` 已被填充；返回 false 表示当前没有可读数据或驱动不可用。
    virtual bool read(CanFrame &frame) = 0;

    // 发送一帧 CAN 数据。
    // 是否真正发出由底层控制器状态决定；调用方不直接处理底层寄存器细节。
    virtual void send(const CanFrame &frame) = 0;
};