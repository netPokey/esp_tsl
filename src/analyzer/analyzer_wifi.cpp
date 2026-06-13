#include "analyzer/analyzer_wifi.h"
#include <WiFi.h>

namespace
{
bool trySta(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == 0)
        return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
        delay(200);
    return WiFi.status() == WL_CONNECTED;
}
}

String analyzerWifiBegin()
{
    if (trySta("jhwctcm", "12345678"))
        return WiFi.localIP().toString();
    if (trySta("Cc", "452509526.."))
        return WiFi.localIP().toString();

    WiFi.mode(WIFI_AP);
    WiFi.softAP("CAN-Analyzer", "analyzer1234");
    delay(120);
    return WiFi.softAPIP().toString();
}
