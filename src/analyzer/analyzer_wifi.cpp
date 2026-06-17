#include "analyzer/analyzer_wifi.h"
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
#include <WiFi.h>
#endif

namespace
{
#if defined(ARDUINO)
constexpr const char *kPrefsNs = "analyzer_wifi";
constexpr const char *kPrefsSsid = "ssid";
constexpr const char *kPrefsPass = "pass";
constexpr const char *kApSsid = "CAN-Analyzer";
constexpr const char *kApPass = "1234567890";
constexpr unsigned long kStaTimeoutMs = 10000;
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

bool analyzerWifiSanitizeCredentials(const char *ssid, const char *pass, AnalyzerWifiCredentials &out)
{
    memset(&out, 0, sizeof(out));
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

void analyzerWifiStartAp()
{
#if defined(ARDUINO)
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPass);
    delay(120);
#endif
}

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
