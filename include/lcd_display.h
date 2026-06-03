#pragma once

#include <Arduino.h>

#include "handlers.h"
#include "runtime_state.h"

// 当前 LCD 模块是一个“显示占位层”。
// 作用：先把展示接口稳定下来，让主流程、解析层和控制层都能按统一方式更新显示。
// 现阶段通过串口输出模拟显示内容，后续替换成真实屏幕驱动时只需要改这个文件。
class LCDDisplay
{
public:
    // 初始化显示层。
    // 当前版本只做刷新节流状态复位，不绑定具体硬件库。
    void init()
    {
        lastRefreshMs_ = 0;
    }

    // 刷新显示内容。
    // 这里展示的是“当前控制总线 + FSD 状态 + 双 CAN 统计 + 预热状态”的核心运行面板。
    void update(const CarManagerBase *handler, const DualCanRuntime *runtime)
    {
        if (!handler || !runtime)
            return;

        const unsigned long now = millis();
        if (now - lastRefreshMs_ < 1200)
            return;
        lastRefreshMs_ = now;

        Serial.printf(
            "[LCD] ctrl=%s fsd=%d profile=%s soc=%.1f%% power=%.2fkW A(rx=%lu tx=%lu) B(rx=%lu tx=%lu) precond=%d\n",
            handler->controlBusName(),
            handler->fsdEnabled ? 1 : 0,
            handler->speedProfileName(),
            handler->socPercent,
            handler->packPowerKW,
            static_cast<unsigned long>(runtime->busA.rxFrames),
            static_cast<unsigned long>(runtime->busA.txFrames),
            static_cast<unsigned long>(runtime->busB.rxFrames),
            static_cast<unsigned long>(runtime->busB.txFrames),
            handler->precondActive ? 1 : 0);
    }

    // 展示一条短消息。
    // 当前通过串口模拟状态条，后续接真实屏幕时仍保留这个入口给启动提示和告警使用。
    void showMessage(const char *msg, uint16_t color = 0xFFFF)
    {
        (void)color;
        if (!msg)
            return;
        Serial.printf("[LCD] %s\n", msg);
    }

private:
    unsigned long lastRefreshMs_ = 0;
};