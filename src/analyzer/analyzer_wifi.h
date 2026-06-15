#pragma once
#if defined(NATIVE_BUILD)
#include <string>
using String = std::string;
#else
#include <Arduino.h>
#endif
#include <cstddef>

constexpr size_t kAnalyzerWifiMaxSsid = 32;
constexpr size_t kAnalyzerWifiMaxPass = 64;

struct AnalyzerWifiCredentials
{
    char ssid[kAnalyzerWifiMaxSsid + 1] = {};
    char pass[kAnalyzerWifiMaxPass + 1] = {};
};

struct AnalyzerWifiStatus
{
    bool sta = false;
    bool ap = false;
    String ip;
    String ssid;
    String pass;
};

bool analyzerWifiSanitizeCredentials(const char *ssid, const char *pass, AnalyzerWifiCredentials &out);
String analyzerWifiBegin();
AnalyzerWifiStatus analyzerWifiStatus();
bool analyzerWifiSaveAndConnect(const char *ssid, const char *pass);
void analyzerWifiStartAp();
