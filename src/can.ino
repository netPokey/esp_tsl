#include <Arduino.h>
#include <cstring>

#include "pin_config.h"

#if !defined(T_2Can) || defined(T_2Can_Fd)
#error "src/can.ino is for MCP2515 CAN_A + TWAI CAN_B only"
#endif

#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/can_driver.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"

static MCP2515Driver canADriver(
    MCP2515_CS,
    MCP2515_RST,
    MCP2515_SCLK,
    MCP2515_MISO,
    MCP2515_MOSI,
    &SPI,
    10000000);

static TWAIDriver canBDriver(
    static_cast<gpio_num_t>(CAN_TX),
    static_cast<gpio_num_t>(CAN_RX));

static uint32_t lastHeartbeatAtMs = 0;
static uint32_t canAReceived = 0;
static uint32_t canBReceived = 0;
static bool canAReady = false;
static bool canBReady = false;

void printFrame(const char *label, const CanFrame &frame, uint32_t count)
{
    Serial.printf("RX %-5s #%lu id=0x%03lX dlc=%u data=",
                  label,
                  static_cast<unsigned long>(count),
                  static_cast<unsigned long>(frame.id),
                  frame.dlc);
    for (uint8_t i = 0; i < frame.dlc && i < 8; ++i)
    {
        if (frame.data[i] < 16)
            Serial.print('0');
        Serial.print(frame.data[i], HEX);
        if (i + 1 < frame.dlc)
            Serial.print(' ');
    }
    Serial.println();
}

void drainBus(CanDriver &driver, const char *label, uint32_t &count)
{
    CanFrame frame;
    while (driver.read(frame))
    {
        ++count;
        printFrame(label, frame, count);
    }
}

bool initBus(CanDriver &driver, const char *label)
{
    if (!driver.init())
    {
        Serial.printf("%s init fail\n", label);
        return false;
    }

    driver.setBusMode(CanBusMode::Normal);
    driver.setFilters(nullptr, 0);
    Serial.printf("%s init ok: receive only app, normal mode ACK, accept all, 500kbps\n", label);
    return true;
}

void printHeartbeat()
{
    const uint32_t now = millis();
    if (now - lastHeartbeatAtMs < 3000)
        return;
    lastHeartbeatAtMs = now;

    const TWAIDriver::DiagInfo diag = canBDriver.getDiagnostics();
    Serial.printf("WAIT rx CAN_A=%lu CAN_B=%lu | CAN_B state=%s rx_err=%lu tx_err=%lu bus_err=%lu missed=%lu\n",
                  static_cast<unsigned long>(canAReceived),
                  static_cast<unsigned long>(canBReceived),
                  diag.state,
                  static_cast<unsigned long>(diag.rxErrors),
                  static_cast<unsigned long>(diag.txErrors),
                  static_cast<unsigned long>(diag.busErrors),
                  static_cast<unsigned long>(diag.rxMissed));
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("Dual CAN receive-only test starting");
    Serial.printf("CAN_A MCP2515 pins cs=%d rst=%d sck=%d miso=%d mosi=%d\n", MCP2515_CS, MCP2515_RST, MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI);
    Serial.printf("CAN_B TWAI pins tx=%d rx=%d\n", CAN_TX, CAN_RX);

    setCanTxEnabled(false);
    canAReady = initBus(canADriver, "CAN_A");
    canBReady = initBus(canBDriver, "CAN_B");

    Serial.println("No CAN frames will be sent by this firmware. Incoming standard frames are printed from both buses.");
}

void loop()
{
    if (canAReady)
        drainBus(canADriver, "CAN_A", canAReceived);
    if (canBReady)
        drainBus(canBDriver, "CAN_B", canBReceived);
    printHeartbeat();
    delay(2);
}
