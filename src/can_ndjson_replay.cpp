// can_ndjson_replay.cpp
// 启动时从 LittleFS 中的 can_batches.ndjson 预加载 CAN 帧，
// 根据 BUS 字段分别通过 CAN_A (MCP2515) 和 CAN_B (TWAI) 循环回放，
// 同时统计另一端回发帧，用于观察发送量与回包量比例。
// 优化：运行态不再读文件/解析 JSON，只从内存帧表按目标 fps 发送。

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

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
static constexpr uint16_t START_TARGET_FPS = 500;
static constexpr uint16_t MAX_TARGET_FPS = 5000;
static constexpr uint16_t TARGET_FPS_STEP = 500;
static constexpr uint32_t RATE_STEP_INTERVAL_MS = 2000;
static constexpr uint8_t ECHO_DATA_BYTE = 0xAB;
static constexpr uint8_t MAX_SEND_BURST_PER_LOOP = 32;

struct ReplayFrame
{
    CanFrame frame;
    bool toCanA = false;
};

static std::vector<ReplayFrame> replayFrames;
static size_t replayIndex = 0;
static uint32_t nextSendUs = 0;
static uint32_t totalFramesAttempted = 0;
static uint32_t totalFramesSent = 0;
static uint32_t totalFramesFailed = 0;
static uint32_t canAFramesSent = 0;
static uint32_t canBFramesSent = 0;
static uint32_t canAFramesFailed = 0;
static uint32_t canBFramesFailed = 0;
static uint32_t totalEchoFramesReceived = 0;
static uint32_t canAEchoFramesReceived = 0;
static uint32_t canBEchoFramesReceived = 0;
static uint32_t windowFramesAttempted = 0;
static uint32_t windowFramesSent = 0;
static uint32_t windowFramesFailed = 0;
static uint32_t windowCanAFramesSent = 0;
static uint32_t windowCanBFramesSent = 0;
static uint32_t windowCanAFramesFailed = 0;
static uint32_t windowCanBFramesFailed = 0;
static uint32_t windowEchoFramesReceived = 0;
static uint32_t windowCanAEchoFramesReceived = 0;
static uint32_t windowCanBEchoFramesReceived = 0;
static uint32_t rateWindowStartMs = 0;
static uint16_t targetFps = START_TARGET_FPS;
static bool canAReady = false;
static bool canBReady = false;
static bool replayReady = false;

// ArduinoJson 过滤器，只解析需要的字段，大幅提升大 JSON 行的解析速度
static StaticJsonDocument<128> filterDoc;

static uint32_t targetFrameIntervalUs()
{
    return 1000000UL / targetFps;
}

static bool isEchoFrame(const CanFrame &frame)
{
    if (frame.dlc == 0)
        return false;

    for (uint8_t i = 0; i < frame.dlc && i < 8; ++i)
    {
        if (frame.data[i] != ECHO_DATA_BYTE)
            return false;
    }
    return true;
}

static void noteEchoFrame(bool fromCanA)
{
    ++totalEchoFramesReceived;
    ++windowEchoFramesReceived;
    if (fromCanA)
    {
        ++canAEchoFramesReceived;
        ++windowCanAEchoFramesReceived;
    }
    else
    {
        ++canBEchoFramesReceived;
        ++windowCanBEchoFramesReceived;
    }
}

// ─── 读取接收队列并统计另一端回发帧 ───
static void readAndCountIncoming()
{
    CanFrame rxFrame;
    if (canAReady)
    {
        while (canA.read(rxFrame))
        {
            if (isEchoFrame(rxFrame))
                noteEchoFrame(true);
        }
    }
    if (canBReady)
    {
        while (canB.read(rxFrame))
        {
            if (isEchoFrame(rxFrame))
                noteEchoFrame(false);
        }
    }
}

static void printAndAdvancePerfWindow()
{
    const uint32_t elapsedMs = millis() - rateWindowStartMs;
    const float seconds = elapsedMs / 1000.0f;
    const float actualAttemptFps = seconds > 0.0f ? windowFramesAttempted / seconds : 0.0f;
    const float actualTxFps = seconds > 0.0f ? windowFramesSent / seconds : 0.0f;
    const float actualRxFps = seconds > 0.0f ? windowEchoFramesReceived / seconds : 0.0f;
    const float windowRatio = windowFramesSent > 0 ? (windowEchoFramesReceived * 100.0f) / windowFramesSent : 0.0f;
    const float totalRatio = totalFramesSent > 0 ? (totalEchoFramesReceived * 100.0f) / totalFramesSent : 0.0f;

    Serial.printf("[PERF] target=%ufps attempt=%lu tx_ok=%lu tx_fail=%lu rx=%lu ratio=%.1f%% attempt_fps=%.1f tx_fps=%.1f rx_fps=%.1f | A ok=%lu fail=%lu rx=%lu B ok=%lu fail=%lu rx=%lu | total ok=%lu fail=%lu rx=%lu ratio=%.1f%%\n",
                  targetFps,
                  static_cast<unsigned long>(windowFramesAttempted),
                  static_cast<unsigned long>(windowFramesSent),
                  static_cast<unsigned long>(windowFramesFailed),
                  static_cast<unsigned long>(windowEchoFramesReceived),
                  windowRatio,
                  actualAttemptFps,
                  actualTxFps,
                  actualRxFps,
                  static_cast<unsigned long>(windowCanAFramesSent),
                  static_cast<unsigned long>(windowCanAFramesFailed),
                  static_cast<unsigned long>(windowCanAEchoFramesReceived),
                  static_cast<unsigned long>(windowCanBFramesSent),
                  static_cast<unsigned long>(windowCanBFramesFailed),
                  static_cast<unsigned long>(windowCanBEchoFramesReceived),
                  static_cast<unsigned long>(totalFramesSent),
                  static_cast<unsigned long>(totalFramesFailed),
                  static_cast<unsigned long>(totalEchoFramesReceived),
                  totalRatio);

    windowFramesAttempted = 0;
    windowFramesSent = 0;
    windowFramesFailed = 0;
    windowCanAFramesSent = 0;
    windowCanBFramesSent = 0;
    windowCanAFramesFailed = 0;
    windowCanBFramesFailed = 0;
    windowEchoFramesReceived = 0;
    windowCanAEchoFramesReceived = 0;
    windowCanBEchoFramesReceived = 0;
    rateWindowStartMs = millis();

    if (targetFps < MAX_TARGET_FPS)
    {
        targetFps += TARGET_FPS_STEP;
        if (targetFps > MAX_TARGET_FPS)
            targetFps = MAX_TARGET_FPS;
    }
}

static void updatePerfWindow()
{
    if (rateWindowStartMs == 0)
    {
        rateWindowStartMs = millis();
        return;
    }

    if (millis() - rateWindowStartMs >= RATE_STEP_INTERVAL_MS)
        printAndAdvancePerfWindow();
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

static bool appendReplayFrame(JsonObject f)
{
    ReplayFrame replayFrame;
    replayFrame.frame.id = f["ID"].as<uint32_t>();
    replayFrame.frame.dlc = f["DLC"].as<uint8_t>();
    if (replayFrame.frame.dlc > 8)
        replayFrame.frame.dlc = 8;

    const char *dataStr = f["DATA"];
    if (dataStr)
    {
        memset(replayFrame.frame.data, 0, sizeof(replayFrame.frame.data));
        parseHexData(dataStr, replayFrame.frame.data, replayFrame.frame.dlc);
    }

    const char *bus = f["BUS"];
    replayFrame.toCanA = bus && strcmp(bus, "CAN_A") == 0;
    replayFrames.push_back(replayFrame);
    return true;
}

static bool loadReplayFrames()
{
    replayFrames.clear();

    File ndjsonFile = LittleFS.open(NDJSON_PATH, "r");
    if (!ndjsonFile)
    {
        Serial.printf("无法打开 %s\n", NDJSON_PATH);
        return false;
    }

    uint32_t lineNumber = 0;
    while (ndjsonFile.available())
    {
        String line = ndjsonFile.readStringUntil('\n');
        line.trim();
        ++lineNumber;
        if (line.length() == 0)
            continue;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, line, DeserializationOption::Filter(filterDoc));
        if (err)
        {
            Serial.printf("JSON 解析失败 line=%lu: %s\n",
                          static_cast<unsigned long>(lineNumber),
                          err.c_str());
            continue;
        }

        JsonArray frames = doc["payload"]["FRAMES"];
        if (frames.isNull())
            continue;

        for (JsonObject f : frames)
            appendReplayFrame(f);
    }

    ndjsonFile.close();
    Serial.printf("预加载回放帧: %lu\n", static_cast<unsigned long>(replayFrames.size()));
    return !replayFrames.empty();
}

static void noteSentFrame(bool toCanA, bool sent)
{
    ++totalFramesAttempted;
    ++windowFramesAttempted;

    if (sent)
    {
        ++totalFramesSent;
        ++windowFramesSent;
        if (toCanA)
        {
            ++canAFramesSent;
            ++windowCanAFramesSent;
        }
        else
        {
            ++canBFramesSent;
            ++windowCanBFramesSent;
        }
        return;
    }

    ++totalFramesFailed;
    ++windowFramesFailed;
    if (toCanA)
    {
        ++canAFramesFailed;
        ++windowCanAFramesFailed;
    }
    else
    {
        ++canBFramesFailed;
        ++windowCanBFramesFailed;
    }
}

static void sendReplayFrame(const ReplayFrame &replayFrame)
{
    bool sent = false;
    if (replayFrame.toCanA)
    {
        if (canAReady)
            sent = canA.trySend(replayFrame.frame);
    }
    else
    {
        if (canBReady)
            sent = canB.trySend(replayFrame.frame, 0);
    }

    noteSentFrame(replayFrame.toCanA, sent);
}

static void sendDueReplayFrames()
{
    if (!replayReady || replayFrames.empty())
        return;

    uint8_t burst = 0;
    const uint32_t frameIntervalUs = targetFrameIntervalUs();
    uint32_t nowUs = micros();
    if (nextSendUs == 0)
        nextSendUs = nowUs;

    while (static_cast<int32_t>(nowUs - nextSendUs) >= 0 && burst < MAX_SEND_BURST_PER_LOOP)
    {
        sendReplayFrame(replayFrames[replayIndex]);
        replayIndex = (replayIndex + 1) % replayFrames.size();
        nextSendUs += frameIntervalUs;
        ++burst;
        nowUs = micros();
    }

    if (burst == MAX_SEND_BURST_PER_LOOP && static_cast<int32_t>(nowUs - nextSendUs) > static_cast<int32_t>(frameIntervalUs * MAX_SEND_BURST_PER_LOOP))
        nextSendUs = nowUs + frameIntervalUs;
}

// ─── Arduino 入口 ───
void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== CAN Dual-Bus NDJSON Replay Perf Test ===");

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

    replayReady = loadReplayFrames();
    if (!replayReady)
    {
        Serial.println("没有可回放帧，停止性能测试");
        return;
    }

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

    nextSendUs = micros();
    rateWindowStartMs = millis();
    Serial.println("开始双总线回放性能测试...");
}

void loop()
{
    readAndCountIncoming();
    updatePerfWindow();
    sendDueReplayFrames();
    readAndCountIncoming();
    updatePerfWindow();
}
