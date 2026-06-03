#pragma once

#include <Arduino.h>

#include "handlers.h"
#include "runtime_state.h"

class LCDDisplay
{
public:
    void init()
    {
        lastRefreshMs_ = 0;
    }

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