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
static WebServer server(80);
static DNSServer dnsServer;
static Preferences prefs;
static CarManagerBase *webHandler = nullptr;
static DualCanRuntime *webRuntime = nullptr;
static bool serialPrintEnabled = true;
static bool webReady = false;

// 把布尔值格式化成 JSON 字面量。
String boolJson(bool value)
{
    return value ? "true" : "false";
}

// 把单条总线运行态序列化成 JSON 片段。
String busJson(const CanBusRuntime &bus)
{
    String out = "{";
    out += "\"online\":" + boolJson(bus.online);
    out += ",\"rx\":" + String(bus.rxFrames);
    out += ",\"tx\":" + String(bus.txFrames);
    out += ",\"last_id\":" + String(bus.lastId);
    out += ",\"last_dlc\":" + String(bus.lastDlc);
    out += "}";
    return out;
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
        out += ",\"precond_req\":" + boolJson(webHandler->precondRequested);
        out += ",\"em_detect\":" + boolJson(webHandler->emergencyDetect);
        out += ",\"isa_ovr\":" + boolJson(webHandler->isaSpeedOverride);
        out += ",\"isa_sup\":" + boolJson(webHandler->isaSuppress);
        out += ",\"isa_mul\":" + String(webHandler->isaSpeedMul);
        out += ",\"enable_print\":" + boolJson(serialPrintEnabled);
        out += ",\"battery\":{";
        out += "\"soc\":" + String(webHandler->socPercent, 1);
        out += ",\"voltage\":" + String(webHandler->packVoltage, 1);
        out += ",\"current\":" + String(webHandler->packCurrent, 1);
        out += ",\"power_kw\":" + String(webHandler->packPowerKW, 2);
        out += ",\"temp_min\":" + String(webHandler->packTempMin, 1);
        out += ",\"temp_max\":" + String(webHandler->packTempMax, 1);
        out += ",\"wh_per_km\":" + String(webHandler->whPerKm, 1);
        out += ",\"precond\":" + boolJson(webHandler->precondActive);
        out += ",\"precond_allowed\":" + boolJson(webHandler->precondAllowed);
        out += ",\"precond_worth\":" + boolJson(webHandler->precondWorthwhile);
        out += "}";
    }
    else
    {
        out += "\"fsd_enabled\":false,\"force_fsd\":false,\"speed_profile\":0,\"speed_profile_name\":\"Unknown\",\"speed_offset\":0,\"control_bus\":\"UNKNOWN\",\"precond_req\":false,\"em_detect\":true,\"isa_ovr\":true,\"isa_sup\":false,\"isa_mul\":7,\"enable_print\":true,\"battery\":{\"soc\":0,\"voltage\":0,\"current\":0,\"power_kw\":0,\"temp_min\":0,\"temp_max\":0,\"wh_per_km\":0,\"precond\":false,\"precond_allowed\":false,\"precond_worth\":false}";
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
        out += ",\"can\":{\"total_rx\":0,\"total_tx\":0,\"a\":{\"online\":false,\"rx\":0,\"tx\":0,\"last_id\":0,\"last_dlc\":0},\"b\":{\"online\":false,\"rx\":0,\"tx\":0,\"last_id\":0,\"last_dlc\":0}}";
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
}

// 统一返回一个最小成功响应，供开关类接口复用。
void respondOk()
{
    server.send(200, "application/json", "{\"ok\":true}");
}

bool parseBodyFlag()
{
    return server.arg("plain").indexOf("true") >= 0 || server.arg("plain").indexOf('1') >= 0;
}

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

    server.onNotFound([]() {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    });
}
}

inline void webServerSetContext(CarManagerBase *handler, DualCanRuntime *runtime)
{
    webHandler = handler;
    webRuntime = runtime;
}

inline bool webServerSerialLoggingEnabled()
{
    return serialPrintEnabled;
}

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

inline void webServerLoop()
{
    if (!webReady)
        return;
    dnsServer.processNextRequest();
    server.handleClient();
}