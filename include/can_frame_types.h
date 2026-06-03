#pragma once
#include <cstdint>
#include <cstring>

// 统一的 CAN 帧载体。
// 作用域：在驱动层、解析层、显示层、Web 层之间传递一帧标准化数据。
// 约束：`data` 固定保留 8 字节，`dlc` 表示当前有效载荷长度。
struct CanFrame
{
    uint32_t id = 0;
    uint8_t dlc = 8;
    uint8_t data[8] = {};
};