#pragma once
#include "analyzer/frame_queue.h"
#include "drivers/can_driver.h"

constexpr uint8_t kRxTaskCore = 1;

// 启动 Core1 专用采集任务。
// driverA / driverB 为两路 CAN 驱动（任一可为 nullptr，表示该通道未启用），
// queue 为 SPSC 队列，本任务是其唯一生产者。调用方在 setup() 中调用一次。
void rxTaskStart(CanDriver *driverA, CanDriver *driverB, FrameQueue *queue);
