#pragma once

#include "../can_frame_types.h"
#include "can_driver.h"
#include "../can_helpers.h"
#include <driver/twai.h>

// ESP32 内建 TWAI 驱动封装。
// 作用：把 ESP-IDF 的 TWAI API 适配为统一 CanDriver 接口，并收口恢复逻辑。
class TWAIDriver : public CanDriver
{
public:
    // 当前实现未启用中断式收包。
    static constexpr bool kSupportsISR = false;

    TWAIDriver(gpio_num_t txPin, gpio_num_t rxPin)
        : txPin_(txPin), rxPin_(rxPin) {}

    bool init() override
    {
        generalConfig_ = TWAI_GENERAL_CONFIG_DEFAULT(
            txPin_,
            rxPin_,
            shouldAllowCanTx() ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
        generalConfig_.rx_queue_len = 32;
        generalConfig_.tx_queue_len = 16;

        timingConfig_ = TWAI_TIMING_CONFIG_500KBITS();
        filterConfig_ = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        if (twai_driver_install(&generalConfig_, &timingConfig_, &filterConfig_) != ESP_OK)
            return false;
        if (twai_start() != ESP_OK)
            return false;
        driverReady_ = true;
        return true;
    }

    void setFilters(const uint32_t *trackedIds, uint8_t trackedIdCount) override
    {
        if (!driverReady_)
            return;

        uint32_t differingBits = 0;
        for (uint8_t index = 1; index < trackedIdCount; index++)
        {
            differingBits |= trackedIds[0] ^ trackedIds[index];
        }

        uint32_t filterBaseId = trackedIds[0] & ~differingBits;
        filterConfig_.acceptance_code = filterBaseId << 21;
        filterConfig_.acceptance_mask = (differingBits << 21) | 0x001FFFFF;
        filterConfig_.single_filter = true;

        twai_stop();
        twai_driver_uninstall();
        if (twai_driver_install(&generalConfig_, &timingConfig_, &filterConfig_) != ESP_OK ||
            twai_start() != ESP_OK)
        {
            driverReady_ = false;
        }
    }

    bool setBusMode(CanBusMode mode) override
    {
        currentMode_ = mode;
        generalConfig_.mode = (mode == CanBusMode::ListenOnly) ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL;

        if (!driverReady_)
        {
            return true;
        }

        twai_stop();
        twai_driver_uninstall();
        if (twai_driver_install(&generalConfig_, &timingConfig_, &filterConfig_) != ESP_OK ||
            twai_start() != ESP_OK)
        {
            driverReady_ = false;
            return false;
        }

        driverReady_ = true;
        currentMode_ = mode;
        return true;
    }

    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame &frame) override
    {
        if (!driverReady_)
        {
            tryRecover();
            return false;
        }

        twai_message_t rawMessage;
        if (twai_receive(&rawMessage, 0) != ESP_OK)
        {
            if (isBusOff())
                recoverWithCooldown();
            return false;
        }
        frame.id = rawMessage.identifier;
        frame.dlc = (rawMessage.data_length_code <= 8) ? rawMessage.data_length_code : 8;
        memset(frame.data, 0, 8);
        memcpy(frame.data, rawMessage.data, frame.dlc);
        return true;
    }

    void send(const CanFrame &frame) override
    {
        if (!driverReady_ || currentMode_ != CanBusMode::Normal || !shouldAllowCanTx())
            return;

        twai_message_t rawMessage = {};
        uint8_t dlc = (frame.dlc <= 8) ? frame.dlc : 8;
        rawMessage.identifier = frame.id;
        rawMessage.data_length_code = dlc;
        memcpy(rawMessage.data, frame.data, dlc);

        if (twai_transmit(&rawMessage, pdMS_TO_TICKS(2)) != ESP_OK)
        {
            if (isBusOff())
                recoverWithCooldown();
        }
    }

    // TWAI 总线诊断快照。
    // 作用：把底层驱动状态裁剪成页面和调试接口可直接消费的摘要。
    struct DiagInfo
    {
        // 当前 TWAI 状态名称。
        const char *state;

        // 接收错误计数器。
        uint32_t rxErrors;

        // 发送错误计数器。
        uint32_t txErrors;

        // 总线累计错误次数。
        uint32_t busErrors;

        // 接收丢帧次数。
        uint32_t rxMissed;
    };

    DiagInfo getDiagnostics()
    {
        DiagInfo info = {"UNKNOWN", 0, 0, 0, 0};
        twai_status_info_t statusInfo;
        if (twai_get_status_info(&statusInfo) == ESP_OK)
        {
            switch (statusInfo.state)
            {
            case TWAI_STATE_RUNNING: info.state = "RUNNING"; break;
            case TWAI_STATE_BUS_OFF: info.state = "BUS_OFF"; break;
            case TWAI_STATE_RECOVERING: info.state = "RECOVERING"; break;
            case TWAI_STATE_STOPPED: info.state = "STOPPED"; break;
            }
            info.rxErrors = statusInfo.rx_error_counter;
            info.txErrors = statusInfo.tx_error_counter;
            info.busErrors = statusInfo.bus_error_count;
            info.rxMissed = statusInfo.rx_missed_count;
        }
        return info;
    }

private:
    // 总线离线恢复之间的最小冷却时间。
    static constexpr uint32_t kBusOffCooldownMs = 1000;

    bool isBusOff()
    {
        twai_status_info_t statusInfo;
        if (twai_get_status_info(&statusInfo) != ESP_OK)
            return false;
        return statusInfo.state == TWAI_STATE_BUS_OFF;
    }

    void recoverWithCooldown()
    {
        uint32_t now = millis();
        if (now - lastRecoveryAtMs_ < kBusOffCooldownMs)
            return;
        lastRecoveryAtMs_ = now;

        twai_stop();
        twai_driver_uninstall();
        if (twai_driver_install(&generalConfig_, &timingConfig_, &filterConfig_) != ESP_OK ||
            twai_start() != ESP_OK)
        {
            driverReady_ = false;
        }
    }

    void tryRecover()
    {
        uint32_t now = millis();
        if (now - lastRecoveryAtMs_ < kBusOffCooldownMs * 10)
            return;
        lastRecoveryAtMs_ = now;

        if (twai_driver_install(&generalConfig_, &timingConfig_, &filterConfig_) == ESP_OK &&
            twai_start() == ESP_OK)
        {
            driverReady_ = true;
        }
    }

    // TWAI 发送引脚。
    gpio_num_t txPin_;

    // TWAI 接收引脚。
    gpio_num_t rxPin_;

    // TWAI 通用配置。
    twai_general_config_t generalConfig_;

    // TWAI 时序配置。
    twai_timing_config_t timingConfig_;

    // TWAI 过滤器配置。
    twai_filter_config_t filterConfig_;

    // 当前总线工作模式。
    // 需要缓存这个状态，便于重装 TWAI 驱动后恢复原本的只听/正常语义。
    CanBusMode currentMode_ = CanBusMode::ListenOnly;

    // 当前驱动是否已成功初始化。
    bool driverReady_ = false;

    // 最近一次执行恢复动作的时间戳。
    uint32_t lastRecoveryAtMs_ = 0;
};