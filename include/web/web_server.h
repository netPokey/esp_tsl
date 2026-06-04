#pragma once

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../handlers.h"
#include "../log_buffer.h"
#include "../runtime_state.h"
#include "web_ui.h"

namespace
{
// Web 控制面内部单例资源。
// 这些对象全部限制在当前头文件匿名命名空间内，避免和其他模块暴露耦合。
static WebServer server(80);
static DNSServer dnsServer;
static Preferences prefs;

// 当前挂接到 Web 控制面的业务处理器。
static CarManagerBase *webHandler = nullptr;

// 当前挂接到 Web 控制面的双 CAN 运行态。
static DualCanRuntime *webRuntime = nullptr;

// 是否允许串口继续输出原始 CAN 帧。
static bool serialPrintEnabled = true;

// Web 控制面是否已经完成初始化。
static bool webReady = false;

// 把布尔值格式化成 JSON 字面量。
String boolJson(bool value)
{
    return value ? "true" : "false";
}

// 把最近一帧的 data 区转换成十六进制字符串，供页面直接展示。
String dataHex(const uint8_t *data, uint8_t dlc)
{
    String out;
    for (uint8_t i = 0; i < dlc && i < 8; ++i)
    {
        if (i > 0)
            out += ' ';
        if (data[i] < 16)
            out += '0';
        out += String(data[i], HEX);
    }
    out.toUpperCase();
    return out;
}

// 把单条总线运行态序列化成 JSON 片段。
String busJson(const CanBusRuntime &bus)
{
    String out = "{";
    out += "\"name\":\"" + String(bus.name ? bus.name : "UNKNOWN") + "\"";
    out += ",\"online\":" + boolJson(bus.online);
    out += ",\"rx\":" + String(bus.rxFrames);
    out += ",\"tx\":" + String(bus.txFrames);
    out += ",\"last_id\":" + String(bus.lastId);
    out += ",\"last_dlc\":" + String(bus.lastDlc);
    out += ",\"last_data\":\"" + dataHex(bus.lastData, bus.lastDlc) + "\"";
    out += ",\"last_seen_ms\":" + String(bus.lastSeenMs);
    out += ",\"last_injected_ms\":" + String(bus.lastInjectedMs);
    out += "}";
    return out;
}

// 生成 Web 页面轮询使用的状态快照。
// 这里把业务状态、双 CAN 统计、最后一帧和日志缓冲压成一个 JSON 文档。
String buildStatusJson()
{
    String out = "{";

    if (webHandler)
    {
        out += "\"fsd_enabled\":" + boolJson(webHandler->fsdEnabled);
        out += ",\"force_fsd\":" + boolJson(webHandler->getForceFSDEnabled());
        out += ",\"speed_profile\":" + String(webHandler->speedProfile);
        out += ",\"speed_profile_name\":\"" + String(webHandler->speedProfileName()) + "\"";
        out += ",\"speed_offset\":" + String(webHandler->speedOffset);
        out += ",\"control_bus\":\"" + String(webHandler->controlBusName()) + "\"";
        out += ",\"frame_count\":" + String(webHandler->frameCount);
        out += ",\"sent_count\":" + String(webHandler->sentCount);
        out += ",\"precond_req\":" + boolJson(webHandler->precondRequested);
        out += ",\"precond_active\":" + boolJson(webHandler->precondActive);
        out += ",\"precond_allowed\":" + boolJson(webHandler->precondAllowed);
        out += ",\"precond_worth\":" + boolJson(webHandler->precondWorthwhile);
        out += ",\"em_detect\":" + boolJson(webHandler->emergencyDetect);
        out += ",\"isa_ovr\":" + boolJson(webHandler->isaSpeedOverride);
        out += ",\"isa_sup\":" + boolJson(webHandler->isaSuppress);
        out += ",\"isa_mul\":" + String(webHandler->isaSpeedMul);
        out += ",\"enable_print\":" + boolJson(serialPrintEnabled);
        out += ",\"can_tx_enabled\":" + boolJson(isCanTxEnabled());
        out += ",\"can_tx_mode\":\"" + String(isCanTxEnabled() ? "NORMAL" : "LISTEN_ONLY") + "\"";
        out += ",\"battery\":{";
        out += "\"soc\":" + String(webHandler->socPercent, 1);
        out += ",\"voltage\":" + String(webHandler->packVoltage, 1);
        out += ",\"current\":" + String(webHandler->packCurrent, 1);
        out += ",\"power_kw\":" + String(webHandler->packPowerKW, 2);
        out += ",\"temp_min\":" + String(webHandler->packTempMin, 1);
        out += ",\"temp_max\":" + String(webHandler->packTempMax, 1);
        out += ",\"wh_per_km\":" + String(webHandler->whPerKm, 1);
        out += "}";
    }
    else
    {
        out += "\"fsd_enabled\":false,\"force_fsd\":false,\"speed_profile\":0,\"speed_profile_name\":\"Unknown\",\"speed_offset\":0,\"control_bus\":\"UNKNOWN\",\"frame_count\":0,\"sent_count\":0,\"precond_req\":false,\"precond_active\":false,\"precond_allowed\":false,\"precond_worth\":false,\"em_detect\":true,\"isa_ovr\":true,\"isa_sup\":false,\"isa_mul\":7,\"enable_print\":true,\"can_tx_enabled\":false,\"can_tx_mode\":\"LISTEN_ONLY\",\"battery\":{\"soc\":0,\"voltage\":0,\"current\":0,\"power_kw\":0,\"temp_min\":0,\"temp_max\":0,\"wh_per_km\":0}";
    }

    out += ",\"uptime_s\":" + String(millis() / 1000UL);

    if (webRuntime)
    {
        const CanBusId controlBus = webHandler ? webHandler->controlBus() : CanBusId::Unknown;
        const CanBusRuntime &lastBus = (controlBus == CanBusId::A) ? webRuntime->busA : webRuntime->busB;
        out += ",\"can\":{";
        out += "\"total_rx\":" + String(webRuntime->totalRxFrames);
        out += ",\"total_tx\":" + String(webRuntime->totalTxFrames);
        out += ",\"a\":" + busJson(webRuntime->busA);
        out += ",\"b\":" + busJson(webRuntime->busB);
        out += "}";
        out += ",\"last_frame\":{";
        out += "\"bus\":\"" + String(canBusName(controlBus)) + "\"";
        out += ",\"id\":" + String(lastBus.lastId);
        out += ",\"dlc\":" + String(lastBus.lastDlc);
        out += ",\"data\":\"" + dataHex(lastBus.lastData, lastBus.lastDlc) + "\"";
        out += "}";
    }
    else
    {
        out += ",\"can\":{\"total_rx\":0,\"total_tx\":0,\"a\":{\"name\":\"CAN_A\",\"online\":false,\"rx\":0,\"tx\":0,\"last_id\":0,\"last_dlc\":0,\"last_data\":\"\",\"last_seen_ms\":0,\"last_injected_ms\":0},\"b\":{\"name\":\"CAN_B\",\"online\":false,\"rx\":0,\"tx\":0,\"last_id\":0,\"last_dlc\":0,\"last_data\":\"\",\"last_seen_ms\":0,\"last_injected_ms\":0}}";
        out += ",\"last_frame\":{\"bus\":\"UNKNOWN\",\"id\":0,\"dlc\":0,\"data\":\"\"}";
    }

    out += ",\"logs\":[";
    const int count = globalLog.count();
    for (int i = 0; i < count; ++i)
    {
        const LogEntry &entry = globalLog.get(i);
        if (i > 0)
            out += ',';
        out += "{\"ts\":" + String(entry.timestamp) + ",\"msg\":\"" + String(entry.message) + "\"}";
    }
    out += "]}";
    return out;
}

// 从 NVS 读取上次保存的控制参数，并回灌到当前业务处理层。
void applySavedPreferences()
{
    if (!webHandler)
        return;

    webHandler->setForceFSDEnabled(prefs.getBool("forceFSD", false));
    webHandler->setPreconditioningRequested(prefs.getBool("precond", false));
    webHandler->setEmergencyDetection(prefs.getBool("emDet", true));
    webHandler->setIsaOverride(prefs.getBool("isaOvr", true));
    webHandler->setIsaSuppress(prefs.getBool("isaSup", false));
    webHandler->setIsaMultiplier(prefs.getUChar("isaMul", 7));
    serialPrintEnabled = prefs.getBool("print", true);
    setCanTxEnabled(false);
    prefs.putBool("canTx", false);
}

// 统一返回一个最小成功响应，供开关类接口复用。
void respondOk()
{
    server.send(200, "application/json", "{\"ok\":true}");
}

// 从 POST 文本体里提取布尔语义。
// 当前前端发的是 `true/false` 或 `1/0`，这里只做宽松解析。
bool parseBodyFlag()
{
    return server.arg("plain").indexOf("true") >= 0 || server.arg("plain").indexOf('1') >= 0;
}

// 绑定所有 Web 路由。
// 页面只暴露控制面和状态查询，不再承载 OTA 上传入口。
void bindRoutes()
{
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", WEB_UI_HTML);
    });

    server.on("/api/status", HTTP_GET, []() {
        server.send(200, "application/json", buildStatusJson());
    });

    server.on("/api/force-fsd", HTTP_POST, []() {
        if (webHandler)
        {
            const bool value = parseBodyFlag();
            webHandler->setForceFSDEnabled(value);
            prefs.putBool("forceFSD", value);
            globalLog.add(value ? "Force FSD enabled" : "Force FSD disabled");
        }
        respondOk();
    });

    server.on("/api/precond", HTTP_POST, []() {
        if (webHandler)
        {
            const bool value = parseBodyFlag();
            webHandler->setPreconditioningRequested(value);
            prefs.putBool("precond", value);
            globalLog.add(value ? "Battery preconditioning requested" : "Battery preconditioning stopped");
        }
        respondOk();
    });

    server.on("/api/em-detect", HTTP_POST, []() {
        if (webHandler)
        {
            const bool value = parseBodyFlag();
            webHandler->setEmergencyDetection(value);
            prefs.putBool("emDet", value);
            globalLog.add(value ? "Emergency detection enabled" : "Emergency detection disabled");
        }
        respondOk();
    });

    server.on("/api/isa-override", HTTP_POST, []() {
        if (webHandler)
        {
            const bool value = parseBodyFlag();
            webHandler->setIsaOverride(value);
            prefs.putBool("isaOvr", value);
            globalLog.add(value ? "ISA speed override enabled" : "ISA speed override disabled");
        }
        respondOk();
    });

    server.on("/api/isa-suppress", HTTP_POST, []() {
        if (webHandler)
        {
            const bool value = parseBodyFlag();
            webHandler->setIsaSuppress(value);
            prefs.putBool("isaSup", value);
            globalLog.add(value ? "ISA chime suppression enabled" : "ISA chime suppression disabled");
        }
        respondOk();
    });

    server.on("/api/isa-mul", HTTP_POST, []() {
        if (webHandler)
        {
            int value = server.arg("plain").toInt();
            if (value < 0)
                value = 0;
            if (value > 7)
                value = 7;
            webHandler->setIsaMultiplier(static_cast<uint8_t>(value));
            prefs.putUChar("isaMul", static_cast<uint8_t>(value));
            char msg[48];
            snprintf(msg, sizeof(msg), "ISA multiplier set to %d", value);
            globalLog.add(msg);
        }
        respondOk();
    });

    server.on("/api/enable-print", HTTP_POST, []() {
        serialPrintEnabled = parseBodyFlag();
        prefs.putBool("print", serialPrintEnabled);
        globalLog.add(serialPrintEnabled ? "Serial frame log enabled" : "Serial frame log disabled");
        respondOk();
    });

    server.on("/api/can-tx", HTTP_POST, []() {
        const bool value = parseBodyFlag();
        setCanTxEnabled(value);
        prefs.putBool("canTx", value);
        globalLog.add(value ? "CAN TX enabled: bus mode switched to normal" : "CAN TX disabled: bus mode switched to listen-only");
        respondOk();
    });

    server.onNotFound([]() {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    });
}
}

// 注入业务处理层和运行态上下文。
inline void webServerSetContext(CarManagerBase *handler, DualCanRuntime *runtime)
{
    webHandler = handler;
    webRuntime = runtime;
}

// 返回当前是否允许串口侧打印原始 CAN 帧。
inline bool webServerSerialLoggingEnabled()
{
    return serialPrintEnabled;
}

// 初始化 WiFi AP、DNS 劫持和 Web 控制入口。
// 这里故意只启动控制平面，不和 BLE OTA 的广播职责混在一起。
inline void webServerInit()
{
    if (webReady)
        return;

    prefs.begin("teslaCAN", false);
    applySavedPreferences();

    WiFi.mode(WIFI_AP);
    WiFi.softAP("TeslaCAN", "tesla1234");
    delay(120);
    dnsServer.start(53, "*", WiFi.softAPIP());

    bindRoutes();
    server.begin();
    webReady = true;

    globalLog.add("WiFi AP ready: TeslaCAN / tesla1234");
    Serial.println("WiFi AP started: TeslaCAN");
    Serial.print("Dashboard: http://");
    Serial.println(WiFi.softAPIP());
}

// 推进 Web 控制面循环。
inline void webServerLoop()
{
    if (!webReady)
        return;
    dnsServer.processNextRequest();
    server.handleClient();
}