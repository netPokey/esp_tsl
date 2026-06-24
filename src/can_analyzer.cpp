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
#include "analyzer/rx_task.h"
#include "can_helpers.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"
#include "pin_config.h"

// ============================================================================
// 只监听 CAN 分析仪的总装配点。
// 数据流：Core1 高优先级 rx_task 从两路 CAN（A=MCP2515 / B=TWAI）采集 ->
//         FrameQueue（SPSC 无锁，rx_task 单生产者）-> Core1 低优先级 Web/loop 消费 ->
//         IdTable 更新每 (channel,id) 的最新数据/周期估计/抖动/变化活跃度。
// Core0 尽量留给 WiFi/TCPIP 底层；本文件只负责对象生命周期与接线，不参与实时采集逻辑。
// ============================================================================

namespace
{
// 队列容量取 1024，覆盖突发流量；超出即丢帧并计数（见 FrameQueue::dropped）。
constexpr uint16_t kQueueCapacity = 1024;
// 队列底层存储与队列对象都用静态全局，避免运行期堆分配，地址在两核间稳定共享。
CapturedFrame g_queueStorage[kQueueCapacity];
FrameQueue g_queue;

IdTable g_table;          // 每 ID 状态表，实际存储位于 PSRAM（见 setup 中分配）。
BusStatsTracker g_stats;  // 总线级统计（负载/帧率等）。

// 两路 CAN 驱动用 unique_ptr 延迟构造：构造需引脚参数，故放到 setup 内 new。
std::unique_ptr<MCP2515Driver> g_canA;
std::unique_ptr<TWAIDriver> g_canB;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    setCanTxEnabled(false);  // 只监听模式：全局禁止任何 CAN 发送，确保不打扰被测总线。
    analyzerWebLogInit(); 

    if (!LittleFS.begin(true))
        analyzerWebLogPrintf("LittleFS mount failed");

    // IdTable 定长 [2 通道][2048 ID]，体量大故放 PSRAM（SPIRAM），不占内置 RAM。
    // 分配失败则无法工作，直接死循环停机而非带病运行。
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

    // CAN_A 走 SPI 上的 MCP2515；CAN_B 走 ESP32 内置 TWAI 控制器。
    g_canA.reset(new MCP2515Driver(MCP2515_CS, MCP2515_RST,
                                   MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI,
                                   &SPI, 10000000, MCP2515_INT));
    g_canB.reset(new TWAIDriver(static_cast<gpio_num_t>(CAN_TX),
                                static_cast<gpio_num_t>(CAN_RX)));

    const bool canAOk = g_canA->init();
    const bool canBOk = g_canB->init();
    markAnalyzerChannelOnline(0, canAOk);  // 上报通道在线状态，供 Web/UI 显示。
    markAnalyzerChannelOnline(1, canBOk);

    if (!canAOk)
        analyzerWebLogPrintf("CAN_A init failed");
    if (!canBOk)
        analyzerWebLogPrintf("CAN_B init failed");

    // 空过滤器 = 接收全部 ID，符合分析仪"全量监听"定位。
    if (canAOk)
        g_canA->setFilters(nullptr, 0);
    if (canBOk)
        g_canB->setFilters(nullptr, 0);

    // 启动 Core1 高优先级采集任务，只把初始化成功的通道传入（失败通道传 nullptr）。
    rxTaskStart(canAOk ? g_canA.get() : nullptr,
                canBOk ? g_canB.get() : nullptr,
                &g_queue);

    // WiFi + Web：把队列/状态表/统计的指针交给 Web 层（loopTask 消费侧）。
    const String ip = analyzerWifiBegin();
    analyzerWebSetContext(&g_queue, &g_table, &g_stats);
    analyzerWebBegin();

    analyzerWebLogPrintf("CAN analyzer ready (listen-only): http://%s", ip.c_str());
}

// Arduino 主循环负责 Web 服务与队列消费；高优先级 rx_task 会抢占它完成 CAN 入队。
void loop()
{
    // 把驱动层累计的硬件丢帧喂给统计：CAN_A=MCP2515 真实接收溢出，CAN_B=TWAI rx_missed。
    g_stats.setHwDrops(
    g_canA ? g_canA->rxOverflowCount() : 0,
    g_canB ? g_canB->getDiagnostics().rxMissed : 0);

    // 每秒打一行 A 路诊断，区分"缓冲溢出"还是"总线/位定时错误"：
    //   真溢出持续涨 = 收取跟不上(中断收包应能压到 ~0)；
    //   REC>127 = 错误被动，多为 500k 位定时/8M-16M 晶振/接线/终端问题，中断救不了。
    static uint32_t lastDiagMs = 0;
    static uint32_t lastRawRx = 0;
    static uint32_t lastExtRx = 0;
    const uint32_t nowMs = millis();
    if (g_canA && nowMs - lastDiagMs >= 1000)
    {
        lastDiagMs = nowMs;
        const uint8_t recA = g_canA->rxErrorCounter();
        const uint32_t rawNow = g_canA->rxFrameCount();
        const uint32_t extNow = g_canA->rxExtCount();
        const uint32_t hwRate = rawNow - lastRawRx;   // 过去≈1s 硬件实际读出的帧数(过滤前)
        const uint32_t extRate = extNow - lastExtRx;  // 其中扩展帧(会被分析层 id>=2048 过滤掉)
        lastRawRx = rawNow;
        lastExtRx = extNow;
        analyzerWebLogPrintf("[diag] A 硬件收=%lu/s 扩展=%lu/s 真溢出=%lu REC=%u TEC=%u%s",
            static_cast<unsigned long>(hwRate), static_cast<unsigned long>(extRate),
            static_cast<unsigned long>(g_canA->rxOverflowCount()),
            static_cast<unsigned>(recA), static_cast<unsigned>(g_canA->txErrorCounter()),
            recA > 127 ? "  <- 错误被动: 查位定时/晶振/接线" : "");
    }

    analyzerWebLoop();
    delay(1);
}
