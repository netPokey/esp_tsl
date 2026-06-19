#pragma once
#if defined(NATIVE_BUILD)
#include <string>
using String = std::string;
#else
#include <Arduino.h>
#endif
#include <cstddef>

// WiFi 凭据固定长度存储，便于 NVS 读写与 JSON 输入校验；长度符合 802.11 常见限制。
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

// 校验并拷贝输入凭据；失败时 out 清零。native 单测主要覆盖这个纯函数。
bool analyzerWifiSanitizeCredentials(const char *ssid, const char *pass, AnalyzerWifiCredentials &out);
// 启动网络：优先用 NVS 中的 STA 凭据连接路由器，失败则回退 AP。
String analyzerWifiBegin();
// 返回当前网络模式/IP 与已保存凭据，供 Web 面板展示。
AnalyzerWifiStatus analyzerWifiStatus();
// 保存新凭据并立即尝试 STA；失败时回退 AP，返回是否成功连上 STA。
bool analyzerWifiSaveAndConnect(const char *ssid, const char *pass);
// 强制开启默认 AP：CAN-Analyzer / 1234567890。
void analyzerWifiStartAp();
