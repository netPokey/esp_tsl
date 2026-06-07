#pragma once

#include "../can_frame_types.h"
#include "can_driver.h"
#include "../can_helpers.h"
#include <Arduino.h>
#include <SPI.h>
#include "mcp2515.h"

// MCP2515 外置控制器驱动。
// 作用：把 SPI 挂载的 MCP2515 适配为统一 CanDriver 接口，供主流程透明调用。
class MCP2515Driver : public CanDriver
{
public:
    // 当前实现未启用中断式收包。
    static constexpr bool kSupportsISR = false;

    MCP2515Driver(uint8_t csPin,
                  uint8_t rstPin,
                  int8_t sckPin,
                  int8_t misoPin,
                  int8_t mosiPin,
                  SPIClass *spi = &SPI,
                  uint32_t spiClock = 10000000)
        : controller_(csPin, spiClock, spi),
          csPin_(csPin),
          rstPin_(rstPin),
          sckPin_(sckPin),
          misoPin_(misoPin),
          mosiPin_(mosiPin),
          spiBus_(spi)
    {
    }

    bool init() override
    {
        pinMode(rstPin_, OUTPUT);
        digitalWrite(rstPin_, HIGH);
        delay(100);
        digitalWrite(rstPin_, LOW);
        delay(100);
        digitalWrite(rstPin_, HIGH);
        delay(100);

        spiBus_->begin(sckPin_, misoPin_, mosiPin_, csPin_);

        if (controller_.reset() != MCP2515::ERROR_OK)
            return false;
        if (controller_.setBitrate(CAN_500KBPS) != MCP2515::ERROR_OK)
            return false;
        if (!setBusMode(shouldAllowCanTx() ? CanBusMode::Normal : CanBusMode::ListenOnly))
            return false;

        controller_.clearInterrupts();
        controller_.clearRXnOVRFlags();
        driverReady_ = true;
        return true;
    }

    void setFilters(const uint32_t *trackedIds, uint8_t trackedIdCount) override
    {
        if (!driverReady_)
            return;

        uint32_t filterBaseId = 0;
        uint32_t filterMask = 0;
        if (trackedIdCount > 0 && trackedIds)
        {
            uint32_t differingBits = 0;
            for (uint8_t index = 1; index < trackedIdCount; ++index)
            {
                differingBits |= trackedIds[0] ^ trackedIds[index];
            }
            filterMask = CAN_SFF_MASK & ~differingBits;
            filterBaseId = trackedIds[0] & filterMask;
        }

        if (controller_.setConfigMode() != MCP2515::ERROR_OK)
        {
            driverReady_ = false;
            return;
        }

        bool configureOk = true;
        configureOk = configureOk && controller_.setFilterMask(MCP2515::MASK0, false, filterMask) == MCP2515::ERROR_OK;
        configureOk = configureOk && controller_.setFilterMask(MCP2515::MASK1, false, filterMask) == MCP2515::ERROR_OK;
        configureOk = configureOk && controller_.setFilter(MCP2515::RXF0, false, filterBaseId) == MCP2515::ERROR_OK;
        configureOk = configureOk && controller_.setFilter(MCP2515::RXF1, false, filterBaseId) == MCP2515::ERROR_OK;
        configureOk = configureOk && controller_.setFilter(MCP2515::RXF2, false, filterBaseId) == MCP2515::ERROR_OK;
        configureOk = configureOk && controller_.setFilter(MCP2515::RXF3, false, filterBaseId) == MCP2515::ERROR_OK;
        configureOk = configureOk && controller_.setFilter(MCP2515::RXF4, false, filterBaseId) == MCP2515::ERROR_OK;
        configureOk = configureOk && controller_.setFilter(MCP2515::RXF5, false, filterBaseId) == MCP2515::ERROR_OK;
        configureOk = configureOk && ((currentMode_ == CanBusMode::ListenOnly)
                                          ? controller_.setListenOnlyMode()
                                          : controller_.setNormalMode()) == MCP2515::ERROR_OK;

        if (!configureOk)
            driverReady_ = false;
    }

    bool setBusMode(CanBusMode mode) override
    {
        if (!driverReady_ && controller_.setConfigMode() != MCP2515::ERROR_OK)
            return false;

        const MCP2515::ERROR result = (mode == CanBusMode::ListenOnly)
                                          ? controller_.setListenOnlyMode()
                                          : controller_.setNormalMode();
        if (result != MCP2515::ERROR_OK)
            return false;

        currentMode_ = mode;
        return true;
    }

    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame &frame) override
    {
        if (!driverReady_ || !controller_.checkReceive())
            return false;

        struct can_frame rawFrame = {};
        if (controller_.readMessage(&rawFrame) != MCP2515::ERROR_OK)
        {
            if (controller_.checkError())
                controller_.clearRXnOVRFlags();
            return false;
        }

        const bool isExtendedFrame = (rawFrame.can_id & CAN_EFF_FLAG) != 0;
        frame.id = rawFrame.can_id & (isExtendedFrame ? CAN_EFF_MASK : CAN_SFF_MASK);
        frame.dlc = (rawFrame.can_dlc <= 8) ? rawFrame.can_dlc : 8;
        memset(frame.data, 0, sizeof(frame.data));
        memcpy(frame.data, rawFrame.data, frame.dlc);
        return true;
    }

    void send(const CanFrame &frame) override
    {
        if (!driverReady_ || currentMode_ != CanBusMode::Normal || !shouldAllowCanTx())
            return;

        struct can_frame rawFrame = {};
        rawFrame.can_id = frame.id & CAN_SFF_MASK;
        rawFrame.can_dlc = (frame.dlc <= 8) ? frame.dlc : 8;
        memcpy(rawFrame.data, frame.data, rawFrame.can_dlc);

        if (controller_.sendMessage(&rawFrame) != MCP2515::ERROR_OK && controller_.checkError())
            controller_.clearTXInterrupts();
    }

private:
    // 第三方库提供的 MCP2515 控制器对象。
    MCP2515 controller_;

    // SPI 片选引脚。
    uint8_t csPin_;

    // MCP2515 复位引脚。
    uint8_t rstPin_;

    // SPI 时钟引脚。
    int8_t sckPin_;

    // SPI 主入从出引脚。
    int8_t misoPin_;

    // SPI 主出从入引脚。
    int8_t mosiPin_;

    // 当前驱动绑定的 SPI 总线实例。
    SPIClass *spiBus_;

    // 当前总线工作模式。
    // 需要单独缓存下来，避免设置过滤器后丢失“只听 / 正常”运行语义。
    CanBusMode currentMode_ = CanBusMode::ListenOnly;

    // 当前驱动是否已经初始化成功并可继续读写。
    bool driverReady_ = false;
};