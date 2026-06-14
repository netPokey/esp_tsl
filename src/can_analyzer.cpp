#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <memory>

#include "analyzer/analyzer_control.h"
#include "analyzer/analyzer_web.h"
#include "analyzer/analyzer_wifi.h"
#include "analyzer/bus_stats.h"
#include "analyzer/frame_queue.h"
#include "analyzer/id_table.h"
#include "analyzer/label_store.h"
#include "analyzer/pretrigger_buffer.h"
#include "analyzer/rx_task.h"
#include "analyzer/snapshot_store.h"
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
BusStatsTracker g_stats;

constexpr size_t kPretriggerCapacity = 16384;
CapturedFrame *g_pretriggerStorage = nullptr;
PretriggerBuffer g_pretrigger;
SnapshotRecord *g_snapshotA = nullptr;
SnapshotRecord *g_snapshotB = nullptr;
SnapshotStore g_snapshots;
LabelStore g_labels;

std::unique_ptr<MCP2515Driver> g_canA;
std::unique_ptr<TWAIDriver> g_canB;

void syncTxMode()
{
    if (g_canA && isAnalyzerChannelOnline(0))
        g_canA->setBusMode(shouldAllowAnalyzerChannelTx(0) ? CanBusMode::Normal : CanBusMode::ListenOnly);
    if (g_canB && isAnalyzerChannelOnline(1))
        g_canB->setBusMode(shouldAllowAnalyzerChannelTx(1) ? CanBusMode::Normal : CanBusMode::ListenOnly);
}
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    setCanTxEnabled(false);
    setAnalyzerChannelTxEnabled(0, false);
    setAnalyzerChannelTxEnabled(1, false);

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
    g_stats.begin(millis());

    g_pretriggerStorage = static_cast<CapturedFrame *>(ps_malloc(sizeof(CapturedFrame) * kPretriggerCapacity));
    if (g_pretriggerStorage)
        g_pretrigger.init(g_pretriggerStorage, kPretriggerCapacity);
    else
        Serial.println("PSRAM allocation failed for pretrigger buffer");

    const size_t snapshotBytes = sizeof(SnapshotRecord) * kChannelCount * kStdIdCount;
    g_snapshotA = static_cast<SnapshotRecord *>(ps_malloc(snapshotBytes));
    g_snapshotB = static_cast<SnapshotRecord *>(ps_malloc(snapshotBytes));
    if (g_snapshotA && g_snapshotB)
        g_snapshots.init(g_snapshotA, g_snapshotB);
    else
        Serial.println("PSRAM allocation failed for snapshot buffers");

    g_labels.begin();

    g_canA.reset(new MCP2515Driver(MCP2515_CS, MCP2515_RST,
                                   MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI,
                                   &SPI, 10000000));
    g_canB.reset(new TWAIDriver(static_cast<gpio_num_t>(CAN_TX),
                                static_cast<gpio_num_t>(CAN_RX)));

    const bool canAOk = g_canA->init();
    const bool canBOk = g_canB->init();
    markAnalyzerChannelOnline(0, canAOk);
    markAnalyzerChannelOnline(1, canBOk);

    if (!canAOk)
        Serial.println("CAN_A init failed");
    if (!canBOk)
        Serial.println("CAN_B init failed");

    syncTxMode();
    if (canAOk)
        g_canA->setFilters(nullptr, 0);
    if (canBOk)
        g_canB->setFilters(nullptr, 0);

    rxTaskStart(canAOk ? g_canA.get() : nullptr,
                canBOk ? g_canB.get() : nullptr,
                &g_queue);

    const String ip = analyzerWifiBegin();
    analyzerWebSetContext(&g_queue, &g_table, &g_stats,
                          g_pretriggerStorage ? &g_pretrigger : nullptr,
                          (g_snapshotA && g_snapshotB) ? &g_snapshots : nullptr,
                          &g_labels);
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
