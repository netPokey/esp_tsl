#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <memory>

#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_wifi.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/rx_task.h"
#include "can_helpers.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"
#include "pin_config.h"

namespace
{
constexpr uint16_t kQueueCapacity = 1024;
CapturedFrame g_queueStorage[kQueueCapacity];
FrameQueue g_queue;

IdTable g_table;

std::unique_ptr<MCP2515Driver> g_canA;
std::unique_ptr<TWAIDriver> g_canB;

void syncTxMode()
{
    const CanBusMode mode = isCanTxEnabled() ? CanBusMode::Normal : CanBusMode::ListenOnly;
    if (g_canA)
        g_canA->setBusMode(mode);
    if (g_canB)
        g_canB->setBusMode(mode);
}
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    setCanTxEnabled(false);

    if (!LittleFS.begin(true))
        Serial.println("LittleFS mount failed");

    void *tableMem = heap_caps_malloc(IdTable::kStorageBytes, MALLOC_CAP_SPIRAM);
    if (!tableMem)
    {
        Serial.println("PSRAM allocation failed for ID table");
        while (true)
            delay(1000);
    }
    g_table.init(static_cast<IdRecord *>(tableMem));

    g_queue.init(g_queueStorage, kQueueCapacity);

    g_canA.reset(new MCP2515Driver(MCP2515_CS, MCP2515_RST,
                                   MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI,
                                   &SPI, 10000000));
    g_canB.reset(new TWAIDriver(static_cast<gpio_num_t>(CAN_TX),
                                static_cast<gpio_num_t>(CAN_RX)));

    if (!g_canA->init())
        Serial.println("CAN_A init failed");
    if (!g_canB->init())
        Serial.println("CAN_B init failed");

    syncTxMode();
    g_canA->setFilters(nullptr, 0);
    g_canB->setFilters(nullptr, 0);

    rxTaskStart(g_canA.get(), g_canB.get(), &g_queue);

    const String ip = analyzerWifiBegin();
    analyzerWebSetContext(&g_queue, &g_table);
    analyzerWebBegin();

    Serial.print("CAN analyzer ready (listen-only): http://");
    Serial.println(ip);
}

void loop()
{
    syncTxMode();
    analyzerWebLoop();
    delay(1);
}
