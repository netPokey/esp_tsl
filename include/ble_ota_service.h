#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEOTA.h>

#include "log_buffer.h"

// BLE OTA 服务封装层。
// 作用：把第三方 BLEOTA 库收口到一个独立模块里，主流程只关心 begin/loop，
// 后续如果要切换安全策略、设备名规则或回调埋点，都只需要改这里。
class BleOtaService
{
public:
    // 初始化 BLE OTA 服务。
    // 输入是对外广播的设备信息，默认和整机控制面解耦，不依赖 WiFi 页面是否开启。
    void begin(const char *deviceName,
               const char *model,
               const char *fwVersion,
               const char *manufacturer)
    {
        if (ready_)
            return;

        deviceName_ = deviceName ? deviceName : "TeslaCAN-BLEOTA";

        BLEDevice::init(deviceName_);
        server_ = BLEDevice::createServer();
        server_->setCallbacks(new ServerCallbacks(this));

        ota_.begin(server_);
        ota_.setModel(model ? model : "TeslaCAN Dual CAN");
        ota_.setFWVersion(fwVersion ? fwVersion : "dev");
        ota_.setManufactuer(manufacturer ? manufacturer : "csk");
        ota_.init();

        restartAdvertising();
        ready_ = true;

        globalLog.add("BLE OTA ready");
        Serial.printf("BLE OTA started: %s\n", deviceName_);
        Serial.println("BLE OTA WebApp: https://gb88.github.io/BLEOTA/");
    }

    // 驱动 BLE OTA 状态机。
    // 这里同时负责处理断连后的重新广播，让 OTA 服务可以长期驻留在后台可用。
    void loop()
    {
        if (!ready_)
            return;

        if (!connected_ && wasConnected_)
        {
            delay(200);
            restartAdvertising();
            globalLog.add("BLE OTA advertising restarted");
        }

        wasConnected_ = connected_;
        ota_.process();
    }

    // 返回 BLE OTA 服务是否已经初始化完成。
    bool ready() const
    {
        return ready_;
    }

    // 返回当前是否有 BLE OTA 客户端已连接。
    bool connected() const
    {
        return connected_;
    }

    // 返回当前广播设备名，供页面或日志输出使用。
    const char *deviceName() const
    {
        return deviceName_;
    }

private:
    // BLE 连接回调。
    // 这里只维护最基础的连接状态和日志，不把业务逻辑塞进 BLE 回调线程。
    class ServerCallbacks : public BLEServerCallbacks
    {
    public:
        explicit ServerCallbacks(BleOtaService *owner)
            : owner_(owner)
        {
        }

        void onConnect(BLEServer *server) override
        {
            (void)server;
            if (!owner_)
                return;
            owner_->connected_ = true;
            globalLog.add("BLE OTA client connected");
        }

        void onDisconnect(BLEServer *server) override
        {
            (void)server;
            if (!owner_)
                return;
            owner_->connected_ = false;
            globalLog.add("BLE OTA client disconnected");
        }

    private:
        BleOtaService *owner_ = nullptr;
    };

    // 重新启动广播。
    // BLEOTA 使用自己的 OTA GATT service UUID，这里显式挂到广播包里，方便 WebApp 直接发现。
    void restartAdvertising()
    {
        BLEAdvertising *advertising = BLEDevice::getAdvertising();
        if (!advertising)
            return;

        advertising->stop();
        advertising->addServiceUUID(ota_.getBLEOTAuuid());
        advertising->setScanResponse(false);
        advertising->setMinPreferred(0x06);
        advertising->setMinPreferred(0x12);
        BLEDevice::startAdvertising();
    }

    BLEOTAClass ota_;
    BLEServer *server_ = nullptr;
    const char *deviceName_ = "TeslaCAN-BLEOTA";
    bool ready_ = false;
    bool connected_ = false;
    bool wasConnected_ = false;
};