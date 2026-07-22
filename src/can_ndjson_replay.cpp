// can_ndjson_replay.cpp
// 从 LittleFS 中的 can_batches.ndjson 读取 CAN 帧，
// 根据 BUS 字段分别通过 CAN_A (MCP2515) 和 CAN_B (TWAI) 循环回放，
// 同时读取并打印接收到的所有 CAN 消息。
// 优化：提高串口波特率至 921600，使用 ArduinoJson 过滤器加速解析，并将帧发送延迟降至最低。

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/mcp2515_driver.h"
#include "drivers/twai_driver.h"
#include "pin_config.h"

// ─── CAN_A 驱动 (MCP2515, SPI) ───
static MCP2515Driver canA(
    MCP2515_CS,
    MCP2515_RST,
    MCP2515_SCLK,
    MCP2515_MISO,
    MCP2515_MOSI,
    &SPI,
    10000000);

// ─── CAN_B 驱动 (TWAI, 内置) ───
static TWAIDriver canB(
    static_cast<gpio_num_t>(CAN_TX),
    static_cast<gpio_num_t>(CAN_RX));

// ─── 回放状态 ───
static constexpr const char *NDJSON_PATH = "/can_batches.ndjson";
static constexpr uint32_t FRAME_INTERVAL_US = 100; // 帧间微秒级间隔 (设置为 0 则不延时)
static uint32_t lastSendUs = 0;
static uint32_t totalFramesSent = 0;
static uint32_t canAFramesSent = 0;
static uint32_t canBFramesSent = 0;
static uint32_t loopCount = 0;
static bool canAReady = false;
static bool canBReady = false;

// ArduinoJson 过滤器，只解析需要的字段，大幅提升大 JSON 行的解析速度
static StaticJsonDocument<128> filterDoc;

// 文件流式读取
static File ndjsonFile;

// ─── 打印接收到的帧 ───
static void printReceivedFrame(const char *label, const CanFrame &frame)
{
    Serial.printf("RX %-5s id=0x%03lX dlc=%u data=",
                  label,
                  static_cast<unsigned long>(frame.id),
                  frame.dlc);
    for (uint8_t i = 0; i < frame.dlc && i < 8; ++i)
    {
        Serial.printf("%02X ", frame.data[i]);
    }
    Serial.println();
}

// ─── 读取并打印接收队列 ───
static void readAndPrintIncoming()
{
    CanFrame rxFrame;
    if (canAReady)
    {
        while (canA.read(rxFrame))
        {
            printReceivedFrame("CAN_A", rxFrame);
        }
    }
    if (canBReady)
    {
        while (canB.read(rxFrame))
        {
            printReceivedFrame("CAN_B", rxFrame);
        }
    }
}

// ─── 解析 "AA BB CC" 格式的十六进制数据字符串 ───
static uint8_t parseHexData(const char *hexStr, uint8_t *outBuf, uint8_t maxLen)
{
    uint8_t count = 0;
    const char *p = hexStr;
    while (*p && count < maxLen)
    {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;

        char hi = *p++;
        char lo = *p ? *p++ : '0';

        auto hexVal = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'A' && c <= 'F')
                return 10 + c - 'A';
            if (c >= 'a' && c <= 'f')
                return 10 + c - 'a';
            return 0;
        };

        outBuf[count++] = (hexVal(hi) << 4) | hexVal(lo);
    }
    return count;
}

// ─── 打开/重置 ndjson 文件 ───
static bool openNdjsonFile()
{
    if (ndjsonFile)
        ndjsonFile.close();

    ndjsonFile = LittleFS.open(NDJSON_PATH, "r");
    if (!ndjsonFile)
    {
        Serial.printf("无法打开 %s\n", NDJSON_PATH);
        return false;
    }
    return true;
}

// ─── 处理单行 ndjson，解析并发送其中所有帧 ───
static void processLine(const String &line)
{
    // 使用预先配置的 filter 只解析我们需要的字段，大幅节省 CPU 和内存
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line, DeserializationOption::Filter(filterDoc));
    if (err)
    {
        Serial.printf("JSON 解析失败: %s\n", err.c_str());
        return;
    }

    JsonArray frames = doc["payload"]["FRAMES"];
    if (frames.isNull())
        return;

    for (JsonObject f : frames)
    {
        // 帧间间隔控制 (微秒级)
        if (FRAME_INTERVAL_US > 0)
        {
            while (micros() - lastSendUs < FRAME_INTERVAL_US)
            {
                readAndPrintIncoming();
                yield();
            }
        }
        else
        {
            readAndPrintIncoming();
        }

        CanFrame frame;
        frame.id = f["ID"].as<uint32_t>();
        frame.dlc = f["DLC"].as<uint8_t>();
        if (frame.dlc > 8)
            frame.dlc = 8;

        const char *dataStr = f["DATA"];
        if (dataStr)
        {
            memset(frame.data, 0, sizeof(frame.data));
            parseHexData(dataStr, frame.data, frame.dlc);
        }

        // 根据 BUS 字段路由到对应总线
        const char *bus = f["BUS"];
        if (bus && strcmp(bus, "CAN_A") == 0)
        {
            if (canAReady)
            {
                canA.send(frame);
                canAFramesSent++;
            }
        }
        else
        {
            // 默认发 CAN_B
            if (canBReady)
            {
                canB.send(frame);
                canBFramesSent++;
            }
        }

        totalFramesSent++;
        lastSendUs = micros();
    }
}

// ─── 串口打印心跳状态 ───
static uint32_t lastHeartbeatMs = 0;
static void printHeartbeat()
{
    uint32_t now = millis();
    if (now - lastHeartbeatMs < 5000)
        return;
    lastHeartbeatMs = now;

    TWAIDriver::DiagInfo diag = canB.getDiagnostics();
    Serial.printf("[REPLAY] loop=%lu total=%lu A=%lu B=%lu | CAN_B %s rx_err=%lu tx_err=%lu\n",
                  static_cast<unsigned long>(loopCount),
                  static_cast<unsigned long>(totalFramesSent),
                  static_cast<unsigned long>(canAFramesSent),
                  static_cast<unsigned long>(canBFramesSent),
                  diag.state,
                  static_cast<unsigned long>(diag.rxErrors),
                  static_cast<unsigned long>(diag.txErrors));
}

// ─── Arduino 入口 ───
void setup()
{
    // 将波特率提升至 921600，防止大量的 RX 打印阻塞 CAN 发送
    Serial.begin(921600);
    delay(1000);
    Serial.println();
    Serial.println("=== CAN Dual-Bus NDJSON Replay (Optimized) ===");

    // 初始化 JSON 过滤器
    filterDoc["payload"]["FRAMES"][0]["BUS"] = true;
    filterDoc["payload"]["FRAMES"][0]["ID"] = true;
    filterDoc["payload"]["FRAMES"][0]["DLC"] = true;
    filterDoc["payload"]["FRAMES"][0]["DATA"] = true;

    // 初始化 LittleFS
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS 挂载失败!");
        return;
    }
    Serial.println("LittleFS 挂载成功");

    // 检查 ndjson 文件
    if (!LittleFS.exists(NDJSON_PATH))
    {
        Serial.printf("文件不存在: %s\n", NDJSON_PATH);
        Serial.println("请先执行 'pio run -t uploadfs' 上传 data 目录");
        return;
    }

    File f = LittleFS.open(NDJSON_PATH, "r");
    Serial.printf("NDJSON 文件大小: %lu bytes\n", static_cast<unsigned long>(f.size()));
    f.close();

    // 启用发送
    setCanTxEnabled(true);

    // 初始化 CAN_A (MCP2515)
    Serial.printf("CAN_A MCP2515 pins cs=%d rst=%d sck=%d miso=%d mosi=%d\n",
                  MCP2515_CS, MCP2515_RST, MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI);
    if (canA.init())
    {
        canA.setBusMode(CanBusMode::Normal);
        canA.setFilters(nullptr, 0);
        canAReady = true;
        Serial.println("CAN_A 初始化成功");
    }
    else
    {
        Serial.println("CAN_A 初始化失败!");
    }

    // 初始化 CAN_B (TWAI)
    Serial.printf("CAN_B TWAI pins tx=%d rx=%d\n", CAN_TX, CAN_RX);
    if (canB.init())
    {
        canB.setBusMode(CanBusMode::Normal);
        canB.setFilters(nullptr, 0);
        canBReady = true;
        Serial.println("CAN_B 初始化成功");
    }
    else
    {
        Serial.println("CAN_B 初始化失败!");
    }

    Serial.println("开始双总线循环回放...");
}

void loop()
{
    // 读取并打印接收到的消息
    readAndPrintIncoming();

    // 文件未打开或已读完 → 重新打开（循环回放）
    if (!ndjsonFile || !ndjsonFile.available())
    {
        if (ndjsonFile)
        {
            ndjsonFile.close();
            Serial.printf("[REPLAY] 第 %lu 轮完成，A=%lu B=%lu 总计=%lu\n",
                          static_cast<unsigned long>(loopCount + 1),
                          static_cast<unsigned long>(canAFramesSent),
                          static_cast<unsigned long>(canBFramesSent),
                          static_cast<unsigned long>(totalFramesSent));
        }
        loopCount++;

        if (!openNdjsonFile())
        {
            delay(5000);
            return;
        }
        Serial.printf("[REPLAY] 开始第 %lu 轮回放\n", static_cast<unsigned long>(loopCount));
    }

    // 逐行读取处理
    if (ndjsonFile.available())
    {
        String line = ndjsonFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 0)
        {
            processLine(line);
        }
    }

    // 心跳
    printHeartbeat();
}
