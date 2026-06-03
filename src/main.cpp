#include <Arduino.h>
#include <memory>

#include "ble_ota_service.h"
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
// 统一描述一条总线端点。
// 作用：把“总线 ID / 显示名称 / 驱动实例”绑定在一起，便于主循环做统一调度。
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
BleOtaService bleOta;

// 构造 CAN_A 端点视图。
CanEndpoint makeCanAEndpoint()
{
    CanEndpoint endpoint;
    endpoint.busId = CanBusId::A;
    endpoint.name = "CAN_A";
    endpoint.driver = canADriver.get();
    return endpoint;
}

// 构造 CAN_B 端点视图。
CanEndpoint makeCanBEndpoint()
{
    CanEndpoint endpoint;
    endpoint.busId = CanBusId::B;
    endpoint.name = "CAN_B";
    endpoint.driver = canBDriver.get();
    return endpoint;
}

// 输出系统启动横幅。
// 这里集中说明当前固件启用了哪些控制面，方便串口排查启动配置是否符合预期。
void logBootBanner()
{
    Serial.println();
    Serial.println("TeslaCAN dual-CAN runtime starting");
    Serial.println("- CAN_A: MCP2515 external controller");
    Serial.println("- CAN_B: ESP32 TWAI internal controller");
    Serial.println("- Control plane: WiFi dashboard + BLE OTA + future scripting");
}

// 初始化外置 MCP2515 总线。
// 这里负责把 SPI 外挂控制器接入统一驱动抽象，并把上线结果写回共享运行态。
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

// 初始化 ESP32 内建 TWAI 总线。
// 这条总线通常承担主控侧注入与解析，初始化结果同样会回写到共享运行态。
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

// 把业务层关注的过滤 ID 同步给两条总线。
// 目标：让总线层尽早裁掉无关流量，降低解析层的干扰和压力。
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

// 按总线 ID 找到对应驱动。
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

// 在串口侧输出一帧 CAN 快照。
// 是否打印由 Web 页面控制开关决定，这样排查时能临时打开，平时不刷屏。
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

// 扫描并处理某一条总线上的所有待处理报文。
// 数据流：驱动读取 → 运行态记账 → 可选串口日志 → 业务功能注入层。
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

// 执行与业务功能相关的后台任务。
// 当前主要承担预热报文的周期注入，后续也适合扩展脚本调度器。
void runVehicleBackgroundTasks()
{
    if (!vehicleHandler)
        return;

    const CanBusId controlBus = vehicleHandler->controlBus();
    vehicleHandler->runPeriodicTasks(controlBus, driverForBus(controlBus), runtimeState);
}

// 推进所有非 CAN 输出层。
// 包括显示、WiFi 页面、串口桥和 BLE OTA 状态机。
void updateOutputs()
{
    lcd.update(vehicleHandler.get(), &runtimeState);
    webServerLoop();
    uartBridge.loop();
    bleOta.loop();
    digitalWrite(PIN_LED, HIGH);
}

// 初始化运行时模块骨架。
// 这里只创建状态容器和业务处理器，不碰具体总线和控制协议。
void initRuntimeModules()
{
    runtimeState.begin();
    lcd.init();
    lcd.showMessage("Initializing...", 0xFFE0);
    globalLog.add("Runtime initialized");

    vehicleHandler.reset(new HW4DualCanHandler());
    webServerSetContext(vehicleHandler.get(), &runtimeState);
    uartBridge.begin(vehicleHandler.get(), &runtimeState);
}

// 初始化用户控制面。
// 当前包含 WiFi 仪表盘和 BLE OTA，两者共存但职责分离。
void initControlPlane()
{
    webServerInit();
    bleOta.begin("TeslaCAN-BLEOTA", "TeslaCAN Dual CAN", "0.4.0", "csk");
    lcd.showMessage("WiFi + BLE OTA ready", 0x07FF);
    globalLog.add("WiFi dashboard ready");
    globalLog.add("BLE OTA control ready");
}
}

// 设备启动入口。
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

// 主循环入口。
// 执行顺序：双 CAN 收包 → 后台业务注入 → 输出层刷新。
void loop()
{
    processBusTraffic(makeCanAEndpoint());
    processBusTraffic(makeCanBEndpoint());
    runVehicleBackgroundTasks();
    updateOutputs();
}