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
    // 通过 INT 引脚支持中断式收包(见 enableInterrupt)；未配置 INT 时上层回退轮询。
    static constexpr bool kSupportsISR = true;

    MCP2515Driver(uint8_t csPin,
                  uint8_t rstPin,
                  int8_t sckPin,
                  int8_t misoPin,
                  int8_t mosiPin,
                  SPIClass *spi = &SPI,
                  uint32_t spiClock = 10000000,
                  int8_t intPin = -1)
        : controller_(csPin, spiClock, spi),
          csPin_(csPin),
          rstPin_(rstPin),
          sckPin_(sckPin),
          misoPin_(misoPin),
          mosiPin_(mosiPin),
          spiBus_(spi),
          intPin_(intPin)
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

    // 启用中断式收包：把外部 ISR 挂到 MCP2515 的 INT 引脚(低有效)。
    // reset() 已使能 CANINTE 的 RX0IF/RX1IF，收到帧时 INT 自动拉低，这里只接 GPIO 中断。
    // 未配置 INT 引脚(intPin_<0)时返回 false，调用方据此回退到轮询，无副作用。
    bool enableInterrupt(void (*onReady)()) override
    {
        if (intPin_ < 0 || !onReady)
            return false;
        pinMode(intPin_, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(intPin_), onReady, FALLING);
        return true;
    }

    bool read(CanFrame &frame) override
    {
        if (!driverReady_)
            return false;
        if (!controller_.checkReceive())
        {
            noteRxError();          // 缓冲已排空，顺手检查一次溢出/错误
            return false;
        }
    
        struct can_frame rawFrame = {};
        if (controller_.readMessage(&rawFrame) != MCP2515::ERROR_OK)
        {
            noteRxError();
            return false;
        }
        // …以下 parse 部分保持原样…
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

    // 累计"真正的接收 FIFO 溢出"次数(只统计 RX0OVR/RX1OVR 上升沿)。
    // rx_task 写、loop 读：单个对齐 uint32_t 在 32 位上原子读写，对显示用计数足够。
    uint32_t rxOverflowCount() const { return rxOverflowCount_; }
    // 缓存的 MCP2515 接收/发送错误计数器(REC/TEC)。由 rx_task 采样(独占 SPI)，loop 只读缓存，
    // 避免跨任务同时操作 SPI。REC>127 = 错误被动、接近 255 = 总线关闭；
    // 若同时出现"低 fps + 高 REC"，多半是位定时(8M/16M 晶振)、接线或终端问题，而非缓冲溢出。
    uint8_t rxErrorCounter() const { return recCached_; }
    uint8_t txErrorCounter() const { return tecCached_; }

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

    // MCP2515 INT 引脚(低有效)；-1 表示未接，此时 enableInterrupt 返回 false，上层回退轮询。
    int8_t intPin_;

    // 当前总线工作模式。
    // 需要单独缓存下来，避免设置过滤器后丢失“只听 / 正常”运行语义。
    CanBusMode currentMode_ = CanBusMode::ListenOnly;

    // 当前驱动是否已经初始化成功并可继续读写。
    bool driverReady_ = false;


    void noteRxError()
    {
        // 只在真正的接收 FIFO 溢出(RX0OVR/RX1OVR)上升沿计数。原实现用 checkError()，
        // 其掩码还含 RXEP/TXEP/TXBO 等总线错误，会把位定时/接线问题误计成"丢帧"。
        const uint8_t eflg = controller_.getErrorFlags();
        const bool ovr = (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) != 0;
        if (ovr && !lastRxOverflow_)
            ++rxOverflowCount_;
            lastRxOverflow_ = ovr;
            if (ovr)
                controller_.clearRXnOVRFlags();
    
            // REC/TEC 限速采样(≤10Hz)，避免每次空轮询都多打 SPI；用于区分溢出 vs 总线错误。
            const uint32_t now = millis();
            if (now - lastErrSampleMs_ >= 100)
            {
                lastErrSampleMs_ = now;
                recCached_ = controller_.errorCountRX();
                tecCached_ = controller_.errorCountTX();
            }
    }
    uint32_t rxOverflowCount_ = 0;
    bool lastRxOverflow_ = false;
    uint32_t lastErrSampleMs_ = 0;
    uint8_t recCached_ = 0;
    uint8_t tecCached_ = 0;
};