#pragma once
#include <cstdint>

// CAN 采集任务入队的最小载体。
// 在底层 CanFrame 之上补通道号与微秒时间戳，既让分析层知道来源总线，
// 又不改动 drivers/ 的通用 CanFrame 定义，保持驱动层零侵入。
struct CapturedFrame
{
    uint32_t id = 0;        // 标准 11-bit ID
    uint8_t  dlc = 0;       // 0..8
    uint8_t  data[8] = {};
    uint8_t  channel = 0;   // 0 = A, 1 = B
    uint64_t ts_us = 0;     // esp_timer_get_time() 微秒时间戳
};
