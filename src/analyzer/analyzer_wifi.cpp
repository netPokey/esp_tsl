#include "analyzer/analyzer_wifi.h"
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
#include <WiFi.h>
#endif

namespace
{
#if defined(ARDUINO)
// NVS 命名空间与键名。只保存用户最后提交的 STA 凭据，AP 凭据固定写死。
constexpr const char *kPrefsNs = "analyzer_wifi";
constexpr const char *kPrefsSsid = "ssid";
constexpr const char *kPrefsPass = "pass";
constexpr const char *kApSsid = "tsl_can";
constexpr const char *kApPass = "1234567890";
constexpr unsigned long kStaTimeoutMs = 10000;
// 尝试以 STA 模式连接路由器。同步等待最多 10 秒：setup 阶段可接受阻塞，Web 重连也在主循环中触发。
bool trySta(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == 0)
        return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass ? pass : "");
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kStaTimeoutMs)
        delay(200);
    return WiFi.status() == WL_CONNECTED;
}

// 从 NVS 读取并再次走 sanitize，防止旧版本/损坏数据越界进入运行态。
AnalyzerWifiCredentials loadCredentials()
{
    AnalyzerWifiCredentials c;
    Preferences prefs;
    if (!prefs.begin(kPrefsNs, true))
        return c;
    const String ssid = prefs.getString(kPrefsSsid, "");
    const String pass = prefs.getString(kPrefsPass, "");
    analyzerWifiSanitizeCredentials(ssid.c_str(), pass.c_str(), c);
    prefs.end();
    return c;
}

// 保存 sanitize 后的固定缓冲；返回值用于区分 NVS 写失败与连接失败。
bool saveCredentials(const AnalyzerWifiCredentials &c)
{
    Preferences prefs;
    if (!prefs.begin(kPrefsNs, false))
        return false;
    const size_t ssidWritten = prefs.putString(kPrefsSsid, c.ssid);
    const size_t passWritten = prefs.putString(kPrefsPass, c.pass);
    prefs.end();
    return ssidWritten > 0 && passWritten == strlen(c.pass);
}
#endif
}

// 纯输入校验：SSID 必填，密码可空；长度超限直接拒绝。
// out 先清零，保证失败路径不会泄漏上一次内容。
bool analyzerWifiSanitizeCredentials(const char *ssid, const char *pass, AnalyzerWifiCredentials &out)
{
    out = {}; // 取代 memset(&out, 0, sizeof(out));
    if (!ssid || ssid[0] == '\0')
        return false;
    const size_t ssidLen = strlen(ssid);
    const size_t passLen = pass ? strlen(pass) : 0;
    if (ssidLen > kAnalyzerWifiMaxSsid || passLen > kAnalyzerWifiMaxPass)
        return false;
    memcpy(out.ssid, ssid, ssidLen);
    if (passLen > 0)
        memcpy(out.pass, pass, passLen);
    return true;
}

// 回退热点模式。先断开 STA 并切到 WIFI_AP，避免 AP/STA 混合状态带来 IP 显示混乱。
void analyzerWifiStartAp()
{
#if defined(ARDUINO)
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPass);
    delay(120);
#endif
}

// 启动策略：优先恢复上次 STA；失败后开启固定 AP，保证现场总能连回设备。
String analyzerWifiBegin()
{
#if defined(ARDUINO)
    const AnalyzerWifiCredentials c = loadCredentials();
    if (c.ssid[0] != '\0' && trySta(c.ssid, c.pass))
        return WiFi.localIP().toString();
    analyzerWifiStartAp();
    return WiFi.softAPIP().toString();
#else
    return String();
#endif
}

// 状态端点使用的快照：同时返回当前模式/IP 与保存的凭据，方便 Web 表单回显。
AnalyzerWifiStatus analyzerWifiStatus()
{
    AnalyzerWifiStatus s;
#if defined(ARDUINO)
    const AnalyzerWifiCredentials c = loadCredentials();
    s.ssid = c.ssid;
    s.pass = c.pass;
    const wifi_mode_t mode = WiFi.getMode();
    s.sta = (mode == WIFI_STA || mode == WIFI_AP_STA) && WiFi.status() == WL_CONNECTED;
    s.ap = (mode == WIFI_AP || mode == WIFI_AP_STA);
    if (s.sta)
        s.ip = WiFi.localIP().toString();
    else if (s.ap)
        s.ip = WiFi.softAPIP().toString();
#endif
    return s;
}

// Web 提交新凭据后的应用路径：先持久化，再断开旧连接并尝试 STA。
// 即使 STA 连接失败，也会回退 AP，避免用户因输错密码失联。
bool analyzerWifiSaveAndConnect(const char *ssid, const char *pass)
{
#if defined(ARDUINO)
    AnalyzerWifiCredentials c;
    if (!analyzerWifiSanitizeCredentials(ssid, pass, c))
        return false;
    if (!saveCredentials(c))
        return false;
    WiFi.disconnect(true);
    delay(100);
    if (trySta(c.ssid, c.pass))
        return true;
    analyzerWifiStartAp();
#else
    (void)ssid;
    (void)pass;
#endif
    return false;
}
