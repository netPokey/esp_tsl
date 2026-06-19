#include "analyzer/rx_task.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
// Core1 高优先级采集任务：唯一职责是把两路 CAN 驱动收到的帧打上时间戳/通道后入队。
// Core0 尽量留给 WiFi/TCPIP 等系统网络任务，避免高频 CAN 轮询影响 WiFi 握手和看门狗。
// 所有权/并发约定：本任务是 FrameQueue 的唯一生产者；Arduino loop 同核低优先级消费。
// ============================================================================

namespace
{
// 任务上下文：两路驱动 + 目标队列。以静态全局持有，地址传给 FreeRTOS 任务。
struct RxTaskContext
{
    CanDriver *driverA = nullptr;
    CanDriver *driverB = nullptr;
    FrameQueue *queue = nullptr;
};

RxTaskContext g_ctx;

// 把指定通道驱动里已到达的帧全部读空并逐帧入队。channel: 0=A, 1=B。
// 在 Core1 采集任务循环中调用。driver 为空（通道未启用）时直接返回。
void drainInto(FrameQueue *queue, CanDriver *driver, uint8_t channel)
{
    if (!driver)
        return;

    CanFrame frame;
    // 循环直到驱动 read 返回 false，确保把驱动缓冲一次排空，降低硬件层溢出风险。
    while (driver->read(frame))
    {
        CapturedFrame cap;
        cap.id = frame.id;
        cap.dlc = frame.dlc <= 8 ? frame.dlc : 8;  // 钳制 dlc 到 0..8，防越界。
        cap.channel = channel;
        // 入队侧统一打微秒时间戳，作为后续周期/抖动估计的时间基准。
        cap.ts_us = static_cast<uint64_t>(esp_timer_get_time());
        for (uint8_t i = 0; i < 8; ++i)
            cap.data[i] = frame.data[i];
        queue->push(cap);  // 队满则 push 内部丢帧并计数，这里不阻塞。
    }
}

// Core1 任务主循环：轮询两路通道后让出 1 tick，让同核 loopTask 有机会消费队列。
void rxTaskLoop(void *arg)
{
    RxTaskContext *ctx = static_cast<RxTaskContext *>(arg);
    for (;;)
    {
        drainInto(ctx->queue, ctx->driverA, 0);
        drainInto(ctx->queue, ctx->driverB, 1);
        vTaskDelay(1);
    }
}
}

void rxTaskStart(CanDriver *driverA, CanDriver *driverB, FrameQueue *queue)
{
    g_ctx.driverA = driverA;
    g_ctx.driverB = driverB;
    g_ctx.queue = queue;
    // 固定到 Core1，并以高于默认 loopTask 的优先级运行；Core0 保留给 WiFi/TCPIP 底层任务。
    xTaskCreatePinnedToCore(rxTaskLoop, "can_rx", 4096, &g_ctx, 10, nullptr, kRxTaskCore);
}
