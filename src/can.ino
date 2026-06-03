#include <Arduino.h>
#include <cstring>

#include "pin_config.h"

#if !defined(T_2Can) || defined(T_2Can_Fd)
#error "src/can.ino is wrapped for MCP2515 Can_A + TWAI Can_B only"
#endif

#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"

static constexpr uint32_t kSendIntervalMs = 3000;
static constexpr uint8_t kFrameDataSize = 8;

static MCP2515Driver Can_A(
    MCP2515_CS,
    MCP2515_RST,
    MCP2515_SCLK,
    MCP2515_MISO,
    MCP2515_MOSI,
    &SPI,
    10000000);

static TWAIDriver Can_B(
    static_cast<gpio_num_t>(CAN_TX),
    static_cast<gpio_num_t>(CAN_RX));

static uint32_t cycleTime = 0;
static bool canABSendFlag = true;

static const uint8_t kCanATxData[kFrameDataSize] = {8, 7, 6, 5, 4, 3, 2, 1};
static const uint8_t kCanBTxData[kFrameDataSize] = {1, 2, 3, 4, 5, 6, 7, 8};

CanFrame makeFrame(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    CanFrame frame{};
    frame.id = id;
    frame.dlc = (dlc <= kFrameDataSize) ? dlc : kFrameDataSize;
    memcpy(frame.data, data, frame.dlc);
    return frame;
}

bool initBus(CanDriver &driver, const char *label)
{
    if (!driver.init())
    {
        Serial.printf("%s: init fail\n", label);
        return false;
    }
    Serial.printf("%s: init success, speed 500kbps\n", label);
    return true;
}

void printFrame(const char *label, const CanFrame &frame)
{
    Serial.printf("\n%s received data\n", label);
    Serial.printf("%s receive id: 0x%X\n", label, frame.id);
    Serial.printf("%s receive data length: %d\n", label, frame.dlc);
    for (uint8_t i = 0; i < frame.dlc; ++i)
    {
        Serial.printf("%s receive data [%d]: %d\n", label, i, frame.data[i]);
    }
    Serial.println();
}

void drainBus(CanDriver &driver, const char *label)
{
    CanFrame frame;
    while (driver.read(frame))
    {
        printFrame(label, frame);
        delay(10);
    }
}

void sendFrame(CanDriver &driver, const char *label, const CanFrame &frame)
{
    Serial.printf("%s: send data\n", label);
    driver.send(frame);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Ciallo");

    initBus(Can_A, "can a");
    initBus(Can_B, "can b");
}

void loop()
{
    drainBus(Can_A, "can a");
    drainBus(Can_B, "can b");

    if (millis() > cycleTime)
    {
        if (canABSendFlag)
        {
            sendFrame(Can_A, "can a", makeFrame(0xAA, kCanATxData, kFrameDataSize));
        }
        else
        {
            sendFrame(Can_B, "can b", makeFrame(0xBB, kCanBTxData, kFrameDataSize));
        }

        canABSendFlag = !canABSendFlag;
        cycleTime = millis() + kSendIntervalMs;
    }
}