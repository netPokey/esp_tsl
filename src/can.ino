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

// 参考发包间隔。
// 这个文件仅作为双 CAN 最小链路验证，不参与正式业务逻辑。
static constexpr uint32_t kSendIntervalMs = 3000;

// 参考报文固定载荷长度。
static constexpr uint8_t kFrameDataSize = 8;

// CAN_A 参考驱动实例，对应外接 MCP2515。
static MCP2515Driver canADemoDriver(
    MCP2515_CS,
    MCP2515_RST,
    MCP2515_SCLK,
    MCP2515_MISO,
    MCP2515_MOSI,
    &SPI,
    10000000);

// CAN_B 参考驱动实例，对应 ESP32 内建 TWAI。
static TWAIDriver canBDemoDriver(
    static_cast<gpio_num_t>(CAN_TX),
    static_cast<gpio_num_t>(CAN_RX));

// 下一次允许发送参考报文的时间点。
static uint32_t nextSendAtMs = 0;

// 轮流在 CAN_A 和 CAN_B 之间切换发送目标。
static bool sendOnCanANext = true;

// 发往 CAN_A 的参考载荷。
static const uint8_t kCanATxData[kFrameDataSize] = {8, 7, 6, 5, 4, 3, 2, 1};

// 发往 CAN_B 的参考载荷。
static const uint8_t kCanBTxData[kFrameDataSize] = {1, 2, 3, 4, 5, 6, 7, 8};

// 构造一帧最小 CAN 示例报文。
CanFrame makeFrame(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    CanFrame frame{};
    frame.id = id;
    frame.dlc = (dlc <= kFrameDataSize) ? dlc : kFrameDataSize;
    memcpy(frame.data, data, frame.dlc);
    return frame;
}

// 初始化单条参考总线，并在串口输出初始化结果。
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

// 把接收到的一帧参考报文展开打印到串口。
void printFrame(const char *label, const CanFrame &frame)
{
    Serial.printf("\n%s received data\n", label);
    Serial.printf("%s receive id: 0x%X\n", label, frame.id);
    Serial.printf("%s receive data length: %d\n", label, frame.dlc);
    for (uint8_t index = 0; index < frame.dlc; ++index)
    {
        Serial.printf("%s receive data [%d]: %d\n", label, index, frame.data[index]);
    }
    Serial.println();
}

// 持续读空某条总线上的参考流量。
void drainBus(CanDriver &driver, const char *label)
{
    CanFrame frame;
    while (driver.read(frame))
    {
        printFrame(label, frame);
        delay(10);
    }
}

// 发送一帧参考测试报文。
void sendFrame(CanDriver &driver, const char *label, const CanFrame &frame)
{
    Serial.printf("%s: send data\n", label);
    driver.send(frame);
}

// 这个文件保留为最小双 CAN 收发参考，不再作为固件主入口。
// 当前正式运行链路统一收口在 src/main.cpp，避免出现两套 setup/loop 并存。
// 保留这组参考函数的目的，是方便后续核对底层驱动、波特率和最小发收路径。
void canLegacyDemoSetup()
{
    Serial.begin(115200);
    Serial.println("Ciallo");

    initBus(canADemoDriver, "can a");
    initBus(canBDemoDriver, "can b");
}

void canLegacyDemoLoop()
{
    drainBus(canADemoDriver, "can a");
    drainBus(canBDemoDriver, "can b");

    if (millis() > nextSendAtMs)
    {
        if (sendOnCanANext)
        {
            sendFrame(canADemoDriver, "can a", makeFrame(0xAA, kCanATxData, kFrameDataSize));
        }
        else
        {
            sendFrame(canBDemoDriver, "can b", makeFrame(0xBB, kCanBTxData, kFrameDataSize));
        }

        sendOnCanANext = !sendOnCanANext;
        nextSendAtMs = millis() + kSendIntervalMs;
    }
}