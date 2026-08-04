#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "can_frame_types.h"
#include "can_upload_mode.h"
#include "runtime_state.h"

#include "generated/analyzer_critical_ids.h"

static constexpr uint16_t CAN_UPLOAD_BATCH_SIZE = 200;
static constexpr size_t CAN_UPLOAD_MAX_DEVICE_ID_LENGTH = 63;

struct CanUploadEntry
{
    uint32_t seq = 0;
    CanBusId bus = CanBusId::Unknown;
    uint32_t timestampMs = 0;
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

struct CanUploadStatusSnapshot
{
    CanUploadMode mode = CanUploadMode::All;
    char url[CAN_UPLOAD_MAX_URL_LENGTH + 1] = {};
    uint16_t pending = 0;
    bool uploadInProgress = false;
    uint32_t batchSeq = 0;
    uint32_t sentBatches = 0;
    uint32_t failedBatches = 0;
    uint32_t uploadDropped = 0;
    uint32_t configDiscarded = 0;
    uint32_t filteredFrames = 0;
    int lastHttpCode = 0;
};

class CanBatchUploader
{
public:
    // The legacy entry point deliberately keeps the original all-frames, 200-frame,
    // fixed-URL behavior. Analyzer firmware can configure the instance afterwards.
    void begin(const char *deviceId)
    {
        portENTER_CRITICAL(&mux_);
        copyBounded(deviceId_, sizeof(deviceId_), deviceId ? deviceId : "esp32-can");
        if (url_[0] == '\0')
            copyBounded(url_, sizeof(url_), CAN_UPLOAD_DEFAULT_URL);
        portEXIT_CRITICAL(&mux_);

        if (!uploadTaskHandle_)
        {
            if (xTaskCreatePinnedToCore(uploadTask, "can_upload", 8192, this, 1, &uploadTaskHandle_, 0) != pdPASS)
            {
                uploadTaskHandle_ = nullptr;
                portENTER_CRITICAL(&mux_);
                failedBatches_++;
                lastHttpCode_ = -2;
                portEXIT_CRITICAL(&mux_);
            }
        }
    }

    // Applies a complete runtime configuration. A changed mode discards a partial
    // batch so frames selected under two policies are never mixed in one upload.
    bool configure(CanUploadMode mode, const char *url, uint32_t partialFlushMs = 0)
    {
        if (!isValidMode(mode) || !url || url[0] == '\0' || strlen(url) > CAN_UPLOAD_MAX_URL_LENGTH)
        {
            portENTER_CRITICAL(&mux_);
            configDiscarded_++;
            portEXIT_CRITICAL(&mux_);
            return false;
        }

        portENTER_CRITICAL(&mux_);
        if ((mode_ != mode || strcmp(url_, url) != 0) && pendingCount_ != 0)
        {
            configDiscarded_ += pendingCount_;
            pendingCount_ = 0;
            firstPendingAtMs_ = 0;
        }
        mode_ = mode;
        partialFlushMs_ = partialFlushMs;
        copyBounded(url_, sizeof(url_), url);
        portEXIT_CRITICAL(&mux_);
        return true;
    }

    bool setUrl(const char *url)
    {
        CanUploadMode currentMode;
        uint32_t currentPartialFlushMs;
        portENTER_CRITICAL(&mux_);
        currentMode = mode_;
        currentPartialFlushMs = partialFlushMs_;
        portEXIT_CRITICAL(&mux_);
        return configure(currentMode, url, currentPartialFlushMs);
    }

    void setMode(CanUploadMode mode)
    {
        char currentUrl[sizeof(url_)];
        uint32_t currentPartialFlushMs;
        portENTER_CRITICAL(&mux_);
        copyBounded(currentUrl, sizeof(currentUrl), url_);
        currentPartialFlushMs = partialFlushMs_;
        portEXIT_CRITICAL(&mux_);
        configure(mode, currentUrl, currentPartialFlushMs);
    }

    // Set to 1000 for Analyzer's one-second partial batches; zero preserves the
    // legacy behavior of waiting indefinitely for all 200 frames.
    void setPartialFlushMs(uint32_t partialFlushMs)
    {
        portENTER_CRITICAL(&mux_);
        partialFlushMs_ = partialFlushMs;
        portEXIT_CRITICAL(&mux_);
    }

    void noteRx(CanBusId bus, const CanFrame &frame)
    {
        noteCaptured(bus, millis(), frame.id, frame.dlc, frame.data);
    }

    void noteCaptured(CanBusId bus,
                      uint32_t timestampMs,
                      uint32_t id,
                      uint8_t dlc,
                      const uint8_t *data)
    {
        portENTER_CRITICAL(&mux_);
        if (mode_ == CanUploadMode::Off)
        {
            portEXIT_CRITICAL(&mux_);
            return;
        }
        if (mode_ == CanUploadMode::Critical && !analyzer::isAnalyzerCriticalCanId(id))
        {
            filteredFrames_++;
            portEXIT_CRITICAL(&mux_);
            return;
        }
        if (pendingCount_ >= CAN_UPLOAD_BATCH_SIZE)
        {
            uploadDropped_++;
            portEXIT_CRITICAL(&mux_);
            return;
        }

        CanUploadEntry &entry = pendingBuffer_[pendingCount_++];
        entry.seq = ++frameSeq_;
        entry.bus = bus;
        entry.timestampMs = timestampMs;
        entry.id = id;
        entry.dlc = dlc <= 8 ? dlc : 8;
        memset(entry.data, 0, sizeof(entry.data));
        if (data && entry.dlc != 0)
            memcpy(entry.data, data, entry.dlc);
        if (pendingCount_ == 1)
            firstPendingAtMs_ = millis();
        portEXIT_CRITICAL(&mux_);
    }

    // Only hands a fixed buffer to the Core 0 worker. HTTP and JSON construction
    // always happen in uploadTask(), never synchronously in the Arduino loop.
    void loop()
    {
        if (WiFi.status() != WL_CONNECTED || !uploadTaskHandle_)
            return;

        bool notify = false;
        const uint32_t now = millis();
        portENTER_CRITICAL(&mux_);
        const bool partialExpired = partialFlushMs_ != 0 && pendingCount_ != 0 &&
                                    static_cast<uint32_t>(now - firstPendingAtMs_) >= partialFlushMs_;
        if (!uploadInProgress_ && pendingCount_ != 0 &&
            (pendingCount_ >= CAN_UPLOAD_BATCH_SIZE || partialExpired))
        {
            uploadCount_ = pendingCount_;
            memcpy(uploadBuffer_, pendingBuffer_, uploadCount_ * sizeof(CanUploadEntry));
            pendingCount_ = 0;
            firstPendingAtMs_ = 0;
            copyBounded(activeUploadUrl_, sizeof(activeUploadUrl_), url_);
            uploadInProgress_ = true;
            notify = true;
        }
        portEXIT_CRITICAL(&mux_);

        if (notify)
            xTaskNotifyGive(uploadTaskHandle_);
    }

    CanUploadStatusSnapshot snapshot() const
    {
        CanUploadStatusSnapshot result;
        portENTER_CRITICAL(&mux_);
        result.mode = mode_;
        copyBounded(result.url, sizeof(result.url), url_);
        result.pending = pendingCount_;
        result.uploadInProgress = uploadInProgress_;
        result.batchSeq = batchSeq_;
        result.sentBatches = sentBatches_;
        result.failedBatches = failedBatches_;
        result.uploadDropped = uploadDropped_;
        result.configDiscarded = configDiscarded_;
        result.filteredFrames = filteredFrames_;
        result.lastHttpCode = lastHttpCode_;
        portEXIT_CRITICAL(&mux_);
        return result;
    }

    uint16_t pending() const { return snapshot().pending; }
    uint32_t batchSeq() const { return snapshot().batchSeq; }
    uint32_t sentBatches() const { return snapshot().sentBatches; }
    uint32_t failedBatches() const { return snapshot().failedBatches; }
    uint32_t uploadDropped() const { return snapshot().uploadDropped; }
    uint32_t configDiscarded() const { return snapshot().configDiscarded; }
    int lastHttpCode() const { return snapshot().lastHttpCode; }
    const char *url() const { return url_; }
    bool taskReady() const { return uploadTaskHandle_ != nullptr; }

private:
    static bool isValidMode(CanUploadMode mode)
    {
        return mode == CanUploadMode::Off || mode == CanUploadMode::All || mode == CanUploadMode::Critical;
    }

    static void copyBounded(char *destination, size_t capacity, const char *source)
    {
        if (!destination || capacity == 0)
            return;
        const char *safeSource = source ? source : "";
        const size_t length = strnlen(safeSource, capacity - 1);
        memcpy(destination, safeSource, length);
        destination[length] = '\0';
    }

    static void appendDataHex(String &out, const uint8_t *data, uint8_t dlc)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        for (uint8_t i = 0; i < dlc && i < 8; ++i)
        {
            if (i > 0)
                out += ' ';
            out += kHex[(data[i] >> 4) & 0x0F];
            out += kHex[data[i] & 0x0F];
        }
    }

    static void uploadTask(void *arg)
    {
        CanBatchUploader *self = static_cast<CanBatchUploader *>(arg);
        for (;;)
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            self->sendBatch();
            portENTER_CRITICAL(&self->mux_);
            self->uploadInProgress_ = false;
            portEXIT_CRITICAL(&self->mux_);
        }
    }

    String buildPayload(uint16_t count)
    {
        uint32_t batchSequence;
        portENTER_CRITICAL(&mux_);
        batchSequence = ++batchSeq_;
        portEXIT_CRITICAL(&mux_);

        String out;
        out.reserve(static_cast<unsigned int>(count) * 120U + 160U);
        out += "{\"device_id\":\"";
        out += deviceId_;
        out += "\",\"uptime_ms\":";
        out += String(millis());
        out += ",\"batch_seq\":";
        out += String(batchSequence);
        out += ",\"frames\":[";

        for (uint16_t i = 0; i < count; ++i)
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
        const uint16_t count = uploadCount_;
        HTTPClient http;
        http.setTimeout(1500);
        if (!http.begin(activeUploadUrl_))
        {
            portENTER_CRITICAL(&mux_);
            failedBatches_++;
            lastHttpCode_ = -1;
            portEXIT_CRITICAL(&mux_);
            http.end();
            return;
        }

        http.addHeader("Content-Type", "application/json");
        const int code = http.POST(buildPayload(count));
        portENTER_CRITICAL(&mux_);
        lastHttpCode_ = code;
        if (code >= 200 && code < 300)
            sentBatches_++;
        else
            failedBatches_++;
        portEXIT_CRITICAL(&mux_);
        http.end();
    }

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    char deviceId_[CAN_UPLOAD_MAX_DEVICE_ID_LENGTH + 1] = "esp32-can";
    char url_[CAN_UPLOAD_MAX_URL_LENGTH + 1] = "http://1.116.182.175:48601/can/batch";
    char activeUploadUrl_[CAN_UPLOAD_MAX_URL_LENGTH + 1] = {};
    CanUploadEntry pendingBuffer_[CAN_UPLOAD_BATCH_SIZE];
    CanUploadEntry uploadBuffer_[CAN_UPLOAD_BATCH_SIZE];
    TaskHandle_t uploadTaskHandle_ = nullptr;
    CanUploadMode mode_ = CanUploadMode::All;
    uint32_t partialFlushMs_ = 0;
    uint32_t firstPendingAtMs_ = 0;
    uint16_t pendingCount_ = 0;
    uint16_t uploadCount_ = 0;
    bool uploadInProgress_ = false;
    uint32_t frameSeq_ = 0;
    uint32_t batchSeq_ = 0;
    uint32_t sentBatches_ = 0;
    uint32_t failedBatches_ = 0;
    uint32_t uploadDropped_ = 0;
    uint32_t configDiscarded_ = 0;
    uint32_t filteredFrames_ = 0;
    int lastHttpCode_ = 0;
};
