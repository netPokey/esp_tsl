#pragma once
#include <cstdint>
#include <cstring>

// 统一的 CAN 帧载体。
// 作用域：在驱动层、解析层、显示层、Web 层之间传递一帧标准化数据。
// 约束：`data` 固定保留 8 字节，`dlc` 表示当前有效载荷长度。
struct CanFrame
{
    // 标准 11-bit 或扩展帧去旗标后的 CAN ID。
    uint32_t id = 0;

    // 当前有效载荷长度，运行时会被限制在 0-8 范围内。
    uint8_t dlc = 8;

    // 固定长度的数据区。
    // 即使实际长度不足 8，也保留完整缓冲，便于统一复制、打印和注入位操作。
    uint8_t data[8] = {};
};