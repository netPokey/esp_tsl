#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "can_frame_types.h"
#include "runtime_state.h"

static constexpr uint16_t CAN_UPLOAD_BATCH_SIZE = 200;
static constexpr const char *CAN_UPLOAD_DEFAULT_URL = "http://1.116.182.175:48601/can/batch";

struct CanUploadEntry
{
    uint32_t seq = 0;
    CanBusId bus = CanBusId::Unknown;
    uint32_t timestampMs = 0;
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

class CanBatchUploader
{
public:
    void begin(const char *deviceId)
    {
        deviceId_ = deviceId ? deviceId : "esp32-can";
    }

    void noteRx(CanBusId bus, const CanFrame &frame)
    {
        CanUploadEntry &entry = buffer_[writeIndex_++];
        entry.seq = ++frameSeq_;
        entry.bus = bus;
        entry.timestampMs = millis();
        entry.id = frame.id;
        entry.dlc = frame.dlc <= 8 ? frame.dlc : 8;
        memset(entry.data, 0, sizeof(entry.data));
        memcpy(entry.data, frame.data, entry.dlc);

        if (writeIndex_ >= CAN_UPLOAD_BATCH_SIZE)
        {
            writeIndex_ = 0;
            batchReady_ = true;
        }
    }

    void loop()
    {
        if (!batchReady_ || uploadInProgress_ || WiFi.status() != WL_CONNECTED)
            return;

        memcpy(uploadBuffer_, buffer_, sizeof(uploadBuffer_));
        batchReady_ = false;
        uploadInProgress_ = true;
        if (xTaskCreatePinnedToCore(uploadTask, "can_upload", 8192, this, 1, nullptr, 0) != pdPASS)
        {
            uploadInProgress_ = false;
            failedBatches_++;
            lastHttpCode_ = -2;
        }
    }

    uint16_t pending() const { return writeIndex_; }
    uint32_t batchSeq() const { return batchSeq_; }
    uint32_t sentBatches() const { return sentBatches_; }
    uint32_t failedBatches() const { return failedBatches_; }
    int lastHttpCode() const { return lastHttpCode_; }
    const char *url() const { return CAN_UPLOAD_DEFAULT_URL; }

private:
    static void appendDataHex(String &out, const uint8_t *data, uint8_t dlc)
    {
        for (uint8_t i = 0; i < dlc && i < 8; ++i)
        {
            if (i > 0)
                out += ' ';
            if (data[i] < 16)
                out += '0';
            out += String(data[i], HEX);
        }
        out.toUpperCase();
    }

    static void uploadTask(void *arg)
    {
        CanBatchUploader *self = static_cast<CanBatchUploader *>(arg);
        self->sendBatch();
        self->uploadInProgress_ = false;
        vTaskDelete(nullptr);
    }

    String buildPayload()
    {
        String out;
        out.reserve(24000);
        out += "{\"device_id\":\"";
        out += deviceId_;
        out += "\",\"uptime_ms\":";
        out += String(millis());
        out += ",\"batch_seq\":";
        out += String(++batchSeq_);
        out += ",\"frames\":[";

        for (uint16_t i = 0; i < CAN_UPLOAD_BATCH_SIZE; ++i)
        {
            const CanUploadEntry &entry = uploadBuffer_[i];
            if (i > 0)
                out += ',';
            out += "{\"seq\":";
            out += String(entry.seq);
            out += ",\"bus\":\"";
            out += canBusName(entry.bus);
            out += "\",\"ts\":";
            out += String(entry.timestampMs);
            out += ",\"id\":";
            out += String(entry.id);
            out += ",\"dlc\":";
            out += String(entry.dlc);
            out += ",\"data\":\"";
            appendDataHex(out, entry.data, entry.dlc);
            out += "\"}";
        }

        out += "]}";
        return out;
    }

    void sendBatch()
    {
        HTTPClient http;
        http.setTimeout(1500);
        if (!http.begin(CAN_UPLOAD_DEFAULT_URL))
        {
            failedBatches_++;
            lastHttpCode_ = -1;
            return;
        }

        http.addHeader("Content-Type", "application/json");
        const int code = http.POST(buildPayload());
        lastHttpCode_ = code;
        if (code >= 200 && code < 300)
            sentBatches_++;
        else
            failedBatches_++;
        http.end();
    }

    const char *deviceId_ = "esp32-can";
    CanUploadEntry buffer_[CAN_UPLOAD_BATCH_SIZE];
    CanUploadEntry uploadBuffer_[CAN_UPLOAD_BATCH_SIZE];
    uint16_t writeIndex_ = 0;
    bool batchReady_ = false;
    volatile bool uploadInProgress_ = false;
    uint32_t frameSeq_ = 0;
    uint32_t batchSeq_ = 0;
    uint32_t sentBatches_ = 0;
    uint32_t failedBatches_ = 0;
    int lastHttpCode_ = 0;
};
