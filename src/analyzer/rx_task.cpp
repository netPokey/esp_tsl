#include "analyzer/rx_task.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
struct RxTaskContext
{
    CanDriver *driverA = nullptr;
    CanDriver *driverB = nullptr;
    FrameQueue *queue = nullptr;
};

RxTaskContext g_ctx;

void drainInto(FrameQueue *queue, CanDriver *driver, uint8_t channel)
{
    if (!driver)
        return;

    CanFrame frame;
    while (driver->read(frame))
    {
        CapturedFrame cap;
        cap.id = frame.id;
        cap.dlc = frame.dlc <= 8 ? frame.dlc : 8;
        cap.channel = channel;
        cap.ts_us = static_cast<uint64_t>(esp_timer_get_time());
        for (uint8_t i = 0; i < 8; ++i)
            cap.data[i] = frame.data[i];
        queue->push(cap);
    }
}

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
    xTaskCreatePinnedToCore(rxTaskLoop, "can_rx", 4096, &g_ctx, 10, nullptr, 0);
}
