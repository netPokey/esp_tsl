#pragma once

#include "../can_frame_types.h"
#include "can_driver.h"
#include <Arduino.h>
#include <SPI.h>
#include "mcp2515.h"

class MCP2515Driver : public CanDriver
{
public:
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
          spi_(spi)
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

        spi_->begin(sckPin_, misoPin_, mosiPin_, csPin_);

        if (controller_.reset() != MCP2515::ERROR_OK)
            return false;
        if (controller_.setBitrate(CAN_500KBPS) != MCP2515::ERROR_OK)
            return false;
        if (controller_.setNormalMode() != MCP2515::ERROR_OK)
            return false;

        controller_.clearInterrupts();
        controller_.clearRXnOVRFlags();
        driverOK_ = true;
        return true;
    }

    void setFilters(const uint32_t *ids, uint8_t count) override
    {
        if (!driverOK_)
            return;

        uint32_t base = 0;
        uint32_t mask = 0;
        if (count > 0)
        {
            uint32_t differ = 0;
            for (uint8_t i = 1; i < count; ++i)
            {
                differ |= ids[0] ^ ids[i];
            }
            mask = CAN_SFF_MASK & ~differ;
            base = ids[0] & mask;
        }

        if (controller_.setConfigMode() != MCP2515::ERROR_OK)
        {
            driverOK_ = false;
            return;
        }

        bool ok = true;
        ok = ok && controller_.setFilterMask(MCP2515::MASK0, false, mask) == MCP2515::ERROR_OK;
        ok = ok && controller_.setFilterMask(MCP2515::MASK1, false, mask) == MCP2515::ERROR_OK;
        ok = ok && controller_.setFilter(MCP2515::RXF0, false, base) == MCP2515::ERROR_OK;
        ok = ok && controller_.setFilter(MCP2515::RXF1, false, base) == MCP2515::ERROR_OK;
        ok = ok && controller_.setFilter(MCP2515::RXF2, false, base) == MCP2515::ERROR_OK;
        ok = ok && controller_.setFilter(MCP2515::RXF3, false, base) == MCP2515::ERROR_OK;
        ok = ok && controller_.setFilter(MCP2515::RXF4, false, base) == MCP2515::ERROR_OK;
        ok = ok && controller_.setFilter(MCP2515::RXF5, false, base) == MCP2515::ERROR_OK;
        ok = ok && controller_.setNormalMode() == MCP2515::ERROR_OK;

        if (!ok)
            driverOK_ = false;
    }

    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame &frame) override
    {
        if (!driverOK_ || !controller_.checkReceive())
            return false;

        struct can_frame raw = {};
        if (controller_.readMessage(&raw) != MCP2515::ERROR_OK)
        {
            if (controller_.checkError())
                controller_.clearRXnOVRFlags();
            return false;
        }

        const bool isExtended = (raw.can_id & CAN_EFF_FLAG) != 0;
        frame.id = raw.can_id & (isExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
        frame.dlc = (raw.can_dlc <= 8) ? raw.can_dlc : 8;
        memset(frame.data, 0, sizeof(frame.data));
        memcpy(frame.data, raw.data, frame.dlc);
        return true;
    }

    void send(const CanFrame &frame) override
    {
        if (!driverOK_)
            return;

        struct can_frame raw = {};
        raw.can_id = frame.id & CAN_SFF_MASK;
        raw.can_dlc = (frame.dlc <= 8) ? frame.dlc : 8;
        memcpy(raw.data, frame.data, raw.can_dlc);

        if (controller_.sendMessage(&raw) != MCP2515::ERROR_OK && controller_.checkError())
            controller_.clearTXInterrupts();
    }

private:
    MCP2515 controller_;
    uint8_t csPin_;
    uint8_t rstPin_;
    int8_t sckPin_;
    int8_t misoPin_;
    int8_t mosiPin_;
    SPIClass *spi_;
    bool driverOK_ = false;
};