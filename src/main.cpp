#include <Arduino.h>
#include <memory>

#include "drivers/can_driver.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"
#include "handlers.h"
#include "lcd_display.h"
#include "log_buffer.h"
#include "pin_config.h"
#include "runtime_state.h"
#include "uart_bridge.h"
#include "web/web_server.h"

namespace
{
struct CanEndpoint
{
    CanBusId busId = CanBusId::Unknown;
    const char *name = "UNKNOWN";
    CanDriver *driver = nullptr;
};

std::unique_ptr<MCP2515Driver> canADriver;
std::unique_ptr<TWAIDriver> canBDriver;
std::unique_ptr<HW4DualCanHandler> vehicleHandler;
LCDDisplay lcd;
DualCanRuntime runtimeState;
UartBridge uartBridge;

CanEndpoint makeCanAEndpoint()
{
    CanEndpoint endpoint;
    endpoint.busId = CanBusId::A;
    endpoint.name = "CAN_A";
    endpoint.driver = canADriver.get();
    return endpoint;
}

CanEndpoint makeCanBEndpoint()
{
    CanEndpoint endpoint;
    endpoint.busId = CanBusId::B;
    endpoint.name = "CAN_B";
    endpoint.driver = canBDriver.get();
    return endpoint;
}

void logBootBanner()
{
    Serial.println();
    Serial.println("TeslaCAN dual-CAN runtime starting");
    Serial.println("- CAN_A: MCP2515 external controller");
    Serial.println("- CAN_B: ESP32 TWAI internal controller");
    Serial.println("- Control plane: WiFi today, Bluetooth/script hooks later");
}

bool initCanA()
{
    canADriver.reset(new MCP2515Driver(
        MCP2515_CS,
        MCP2515_RST,
        MCP2515_SCLK,
        MCP2515_MISO,
        MCP2515_MOSI,
        &SPI,
        10000000));

    if (!canADriver->init())
    {
        globalLog.add("CAN_A init failed");
        runtimeState.markOnline(CanBusId::A, false);
        return false;
    }

    runtimeState.markOnline(CanBusId::A, true);
    globalLog.add("CAN_A init ok at 500kbps");
    return true;
}

bool initCanB()
{
    canBDriver.reset(new TWAIDriver(
        static_cast<gpio_num_t>(CAN_TX),
        static_cast<gpio_num_t>(CAN_RX)));

    if (!canBDriver->init())
    {
        globalLog.add("CAN_B init failed");
        runtimeState.markOnline(CanBusId::B, false);
        return false;
    }

    runtimeState.markOnline(CanBusId::B, true);
    globalLog.add("CAN_B init ok at 500kbps");
    return true;
}

void attachFilters()
{
    if (!vehicleHandler)
        return;

    const uint32_t *ids = vehicleHandler->filterIds();
    const uint8_t count = vehicleHandler->filterIdCount();

    if (canADriver)
        canADriver->setFilters(ids, count);
    if (canBDriver)
        canBDriver->setFilters(ids, count);
}

CanDriver *driverForBus(CanBusId bus)
{
    switch (bus)
    {
    case CanBusId::A:
        return canADriver.get();
    case CanBusId::B:
        return canBDriver.get();
    default:
        return nullptr;
    }
}

void logFrameIfEnabled(const CanEndpoint &endpoint, const CanFrame &frame)
{
    if (!webServerSerialLoggingEnabled())
        return;

    Serial.printf("[%s] id=0x%03lX dlc=%u data=",
                  endpoint.name,
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

void processBusTraffic(const CanEndpoint &endpoint)
{
    if (!endpoint.driver || !vehicleHandler)
        return;

    CanFrame frame;
    while (endpoint.driver->read(frame))
    {
        digitalWrite(PIN_LED, LOW);
        runtimeState.noteRx(endpoint.busId, frame);
        logFrameIfEnabled(endpoint, frame);
        vehicleHandler->handleFrame(endpoint.busId, frame, endpoint.driver, runtimeState);
    }
}

void runVehicleBackgroundTasks()
{
    if (!vehicleHandler)
        return;

    const CanBusId controlBus = vehicleHandler->controlBus();
    vehicleHandler->runPeriodicTasks(controlBus, driverForBus(controlBus), runtimeState);
}

void updateOutputs()
{
    lcd.update(vehicleHandler.get(), &runtimeState);
    webServerLoop();
    digitalWrite(PIN_LED, HIGH);
}

void initRuntimeModules()
{
    runtimeState.begin();
    lcd.init();
    lcd.showMessage("Initializing...", 0xFFE0);
    globalLog.add("Runtime initialized");

    vehicleHandler.reset(new HW4DualCanHandler());
    webServerSetContext(vehicleHandler.get(), &runtimeState);
}

void initControlPlane()
{
    webServerInit();
    lcd.showMessage("WiFi: TeslaCAN", 0x07FF);
    globalLog.add("WiFi control plane ready");

}
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);

    logBootBanner();
    initRuntimeModules();

    const bool canAOk = initCanA();
    const bool canBOk = initCanB();
    attachFilters();

    if (canAOk && canBOk)
        lcd.showMessage("Dual CAN ready", 0x07E0);
    else if (canAOk || canBOk)
        lcd.showMessage("Single CAN degraded", 0xFFE0);
    else
        lcd.showMessage("CAN init failed", 0xF800);

    initControlPlane();
}

void loop()
{
    processBusTraffic(makeCanAEndpoint());
    processBusTraffic(makeCanBEndpoint());
    runVehicleBackgroundTasks();
    updateOutputs();
}