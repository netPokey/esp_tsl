#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "can_batch_uploader_types.h"
#include "can_batch_wire.h"
#include "can_frame_types.h"
#include "can_upload_mode.h"
#include "generated/analyzer_critical_ids.h"
#include "runtime_state.h"

enum class CanUploadSessionState : uint8_t
{
    Inactive,
    Active,
    Stopping,
};

struct CanUploadStatusSnapshot
{
    CanUploadMode mode = CanUploadMode::All;
    CanUploadTransport transport = CanUploadTransport::Json;
    CanUploadSessionState sessionState = CanUploadSessionState::Inactive;
    uint8_t busMask = CAN_UPLOAD_BUS_BOTH;
    char url[CAN_UPLOAD_MAX_URL_LENGTH + 1] = {};
    uint16_t pending = 0;
    bool uploadInProgress = false;
    uint32_t sessionSeq = 0;
    uint32_t batchSeq = 0;
    uint32_t sentBatches = 0;
    uint32_t failedBatches = 0;
    uint32_t uploadDropped = 0;
    uint32_t configDiscarded = 0;
    uint32_t filteredFrames = 0;
    uint32_t busFilteredFrames = 0;
    uint32_t stopDiscardedFrames = 0;
    uint16_t pendingHighWater = 0;
    uint32_t overloadEvents = 0;
    bool admissionPaused = false;
    int lastHttpCode = 0;
};

class CanBatchUploader
{
public:
    // 旧 main 固件沿用默认 active + JSON；Analyzer 显式传 false，确保每次重启不上传。
    void begin(const char *deviceId, bool startActive = true)
    {
        portENTER_CRITICAL(&mux_);
        copyBounded(deviceId_, sizeof(deviceId_), deviceId ? deviceId : "esp32-can");
        active_ = startActive;
        sessionState_ = startActive ? CanUploadSessionState::Active : CanUploadSessionState::Inactive;
        portEXIT_CRITICAL(&mux_);

        if (!uploadTaskHandle_ &&
            xTaskCreatePinnedToCore(uploadTask, "can_upload", 8192, this, 1, &uploadTaskHandle_, 0) != pdPASS)
        {
            uploadTaskHandle_ = nullptr;
            portENTER_CRITICAL(&mux_);
            ++failedBatches_;
            lastHttpCode_ = -2;
            active_ = false;
            sessionState_ = CanUploadSessionState::Inactive;
            portEXIT_CRITICAL(&mux_);
        }
    }

    bool configure(CanUploadMode mode, const char *url, uint32_t partialFlushMs = 0,
                   CanUploadTransport transport = CanUploadTransport::Json,
                   uint8_t busMask = CAN_UPLOAD_BUS_BOTH)
    {
        if (!isValidMode(mode) || !url || url[0] == '\0' ||
            strlen(url) > CAN_UPLOAD_MAX_URL_LENGTH || !isValidBusMask(busMask))
            return false;

        portENTER_CRITICAL(&mux_);
        if ((mode_ != mode || transport_ != transport || busMask_ != busMask || strcmp(url_, url) != 0) && pendingCount_)
        {
            configDiscarded_ += pendingCount_;
            pendingCount_ = 0;
            firstPendingAtMs_ = 0;
        }
        mode_ = mode;
        transport_ = transport;
        busMask_ = busMask;
        partialFlushMs_ = partialFlushMs;
        copyBounded(url_, sizeof(url_), url);
        if (mode == CanUploadMode::Off)
        {
            active_ = false;
            sessionState_ = uploadInProgress_ ? CanUploadSessionState::Stopping : CanUploadSessionState::Inactive;
        }
        portEXIT_CRITICAL(&mux_);
        return true;
    }

    bool startSession()
    {
        if (!uploadTaskHandle_)
            return false;
        portENTER_CRITICAL(&mux_);
        if (sessionState_ == CanUploadSessionState::Stopping)
        {
            portEXIT_CRITICAL(&mux_);
            return false;
        }
        if (!active_)
        {
            pendingCount_ = 0;
            firstPendingAtMs_ = 0;
            admissionPaused_ = false;
            active_ = true;
            sessionState_ = CanUploadSessionState::Active;
            ++sessionSeq_;
        }
        portEXIT_CRITICAL(&mux_);
        return true;
    }

    void stopSession()
    {
        portENTER_CRITICAL(&mux_);
        active_ = false;
        if (pendingCount_)
        {
            stopDiscardedFrames_ += pendingCount_;
            pendingCount_ = 0;
            firstPendingAtMs_ = 0;
        }
        admissionPaused_ = false;
        sessionState_ = uploadInProgress_ ? CanUploadSessionState::Stopping : CanUploadSessionState::Inactive;
        portEXIT_CRITICAL(&mux_);
    }

    bool sessionActive() const
    {
        portENTER_CRITICAL(&mux_);
        const bool active = sessionState_ != CanUploadSessionState::Inactive;
        portEXIT_CRITICAL(&mux_);
        return active;
    }

    void noteRx(CanBusId bus, const CanFrame &frame)
    {
        noteCaptured(bus, millis(), frame.id, frame.dlc, frame.data);
    }

    void noteCaptured(CanBusId bus, uint32_t timestampMs, uint32_t id,
                      uint8_t dlc, const uint8_t *data)
    {
        CanUploadMode mode;
        uint8_t busMask;
        portENTER_CRITICAL(&mux_);
        if (!active_)
        {
            portEXIT_CRITICAL(&mux_);
            return;
        }
        mode = mode_;
        busMask = busMask_;
        portEXIT_CRITICAL(&mux_);

        const uint8_t frameBusBit = bus == CanBusId::A ? CAN_UPLOAD_BUS_A :
                                    bus == CanBusId::B ? CAN_UPLOAD_BUS_B : 0;
        if (!frameBusBit || !(busMask & frameBusBit))
        {
            portENTER_CRITICAL(&mux_);
            ++busFilteredFrames_;
            portEXIT_CRITICAL(&mux_);
            return;
        }
        if (mode == CanUploadMode::Off)
            return;
        if (mode == CanUploadMode::Critical && !analyzer::isAnalyzerCriticalCanId(id))
        {
            portENTER_CRITICAL(&mux_);
            ++filteredFrames_;
            portEXIT_CRITICAL(&mux_);
            return;
        }

        portENTER_CRITICAL(&mux_);
        if (!active_)
        {
            portEXIT_CRITICAL(&mux_);
            return;
        }
        if (pendingCount_ >= CAN_UPLOAD_BATCH_SIZE)
        {
            ++uploadDropped_;
            if (!admissionPaused_)
            {
                admissionPaused_ = true;
                ++overloadEvents_;
            }
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
        if (data && entry.dlc)
            memcpy(entry.data, data, entry.dlc);
        if (pendingCount_ == 1)
            firstPendingAtMs_ = millis();
        if (pendingCount_ > pendingHighWater_)
            pendingHighWater_ = pendingCount_;
        portEXIT_CRITICAL(&mux_);
    }

    void loop()
    {
        if (WiFi.status() != WL_CONNECTED || !uploadTaskHandle_)
            return;
        bool notify = false;
        const uint32_t now = millis();
        portENTER_CRITICAL(&mux_);
        const bool partialExpired = partialFlushMs_ && pendingCount_ &&
            static_cast<uint32_t>(now - firstPendingAtMs_) >= partialFlushMs_;
        if (active_ && !uploadInProgress_ && pendingCount_ &&
            (pendingCount_ >= CAN_UPLOAD_BATCH_SIZE || partialExpired))
        {
            uploadCount_ = pendingCount_;
            memcpy(uploadBuffer_, pendingBuffer_, uploadCount_ * sizeof(CanUploadEntry));
            pendingCount_ = 0;
            firstPendingAtMs_ = 0;
            admissionPaused_ = false;
            activeUploadTransport_ = transport_;
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
        CanUploadStatusSnapshot out;
        portENTER_CRITICAL(&mux_);
        out.mode = mode_;
        out.transport = transport_;
        out.sessionState = sessionState_;
        out.busMask = busMask_;
        copyBounded(out.url, sizeof(out.url), url_);
        out.pending = pendingCount_;
        out.uploadInProgress = uploadInProgress_;
        out.sessionSeq = sessionSeq_;
        out.batchSeq = batchSeq_;
        out.sentBatches = sentBatches_;
        out.failedBatches = failedBatches_;
        out.uploadDropped = uploadDropped_;
        out.configDiscarded = configDiscarded_;
        out.filteredFrames = filteredFrames_;
        out.busFilteredFrames = busFilteredFrames_;
        out.stopDiscardedFrames = stopDiscardedFrames_;
        out.pendingHighWater = pendingHighWater_;
        out.overloadEvents = overloadEvents_;
        out.admissionPaused = admissionPaused_;
        out.lastHttpCode = lastHttpCode_;
        portEXIT_CRITICAL(&mux_);
        return out;
    }

    uint16_t pending() const { return snapshot().pending; }
    uint32_t batchSeq() const { return snapshot().batchSeq; }
    uint32_t sentBatches() const { return snapshot().sentBatches; }
    uint32_t failedBatches() const { return snapshot().failedBatches; }
    int lastHttpCode() const { return snapshot().lastHttpCode; }
    const char *url() const { return url_; }
    bool taskReady() const { return uploadTaskHandle_ != nullptr; }

private:
    static bool isValidMode(CanUploadMode mode)
    {
        return mode == CanUploadMode::Off || mode == CanUploadMode::All || mode == CanUploadMode::Critical;
    }
    static bool isValidBusMask(uint8_t mask) { return mask >= CAN_UPLOAD_BUS_A && mask <= CAN_UPLOAD_BUS_BOTH; }
    static void copyBounded(char *dst, size_t cap, const char *src)
    {
        if (!dst || !cap) return;
        const size_t n = strnlen(src ? src : "", cap - 1);
        memcpy(dst, src ? src : "", n);
        dst[n] = '\0';
    }
    static void appendDataHex(String &out, const uint8_t *data, uint8_t dlc)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        for (uint8_t i = 0; i < dlc; ++i)
        {
            if (i) out += ' ';
            out += hex[data[i] >> 4];
            out += hex[data[i] & 0x0f];
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
            if (!self->active_)
                self->sessionState_ = CanUploadSessionState::Inactive;
            portEXIT_CRITICAL(&self->mux_);
        }
    }

    String &buildJsonPayload(uint16_t count, uint32_t batchSequence)
    {
        payload_.clear();
        payload_.reserve(static_cast<unsigned int>(count) * 120U + 160U);
        payload_ += "{\"device_id\":\"";
        payload_ += deviceId_;
        payload_ += "\",\"uptime_ms\":";
        payload_ += String(millis());
        payload_ += ",\"batch_seq\":";
        payload_ += String(batchSequence);
        payload_ += ",\"frames\":[";
        for (uint16_t i = 0; i < count; ++i)
        {
            const CanUploadEntry &entry = uploadBuffer_[i];
            if (i) payload_ += ',';
            payload_ += "{\"seq\":" + String(entry.seq) + ",\"bus\":\"";
            payload_ += canBusName(entry.bus);
            payload_ += "\",\"ts\":" + String(entry.timestampMs) + ",\"id\":" + String(entry.id);
            payload_ += ",\"dlc\":" + String(entry.dlc) + ",\"data\":\"";
            appendDataHex(payload_, entry.data, entry.dlc);
            payload_ += "\"}";
        }
        payload_ += "]}";
        return payload_;
    }

    void sendBatch()
    {
        const uint16_t count = uploadCount_;
        uint32_t batchSequence;
        portENTER_CRITICAL(&mux_);
        batchSequence = ++batchSeq_;
        portEXIT_CRITICAL(&mux_);

        HTTPClient http;
        http.setTimeout(1500);
        if (!http.begin(activeUploadUrl_))
        {
            noteHttpResult(-1);
            http.end();
            return;
        }

        int code;
        if (activeUploadTransport_ == CanUploadTransport::BinaryV1)
        {
            const size_t bytes = can_batch_wire::encode(binaryPayload_, sizeof(binaryPayload_), deviceId_,
                                                        millis(), batchSequence, uploadBuffer_, count);
            if (!bytes)
            {
                http.end();
                noteHttpResult(-3);
                return;
            }
            http.addHeader("Content-Type", "application/vnd.teslacan.can-batch-v1+octet-stream");
            code = http.POST(binaryPayload_, bytes);
        }
        else
        {
            http.addHeader("Content-Type", "application/json");
            code = http.POST(buildJsonPayload(count, batchSequence));
        }
        http.end();
        noteHttpResult(code);
    }

    void noteHttpResult(int code)
    {
        portENTER_CRITICAL(&mux_);
        lastHttpCode_ = code;
        if (code >= 200 && code < 300) ++sentBatches_;
        else ++failedBatches_;
        portEXIT_CRITICAL(&mux_);
    }

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    char deviceId_[CAN_UPLOAD_MAX_DEVICE_ID_LENGTH + 1] = "esp32-can";
    char url_[CAN_UPLOAD_MAX_URL_LENGTH + 1] = "http://1.116.182.175:48601/can/batch";
    char activeUploadUrl_[CAN_UPLOAD_MAX_URL_LENGTH + 1] = {};
    String payload_;
    uint8_t binaryPayload_[can_batch_wire::kMaxBatchBytes] = {};
    CanUploadEntry pendingBuffer_[CAN_UPLOAD_BATCH_SIZE];
    CanUploadEntry uploadBuffer_[CAN_UPLOAD_BATCH_SIZE];
    TaskHandle_t uploadTaskHandle_ = nullptr;
    CanUploadMode mode_ = CanUploadMode::All;
    CanUploadTransport transport_ = CanUploadTransport::Json;
    CanUploadTransport activeUploadTransport_ = CanUploadTransport::Json;
    CanUploadSessionState sessionState_ = CanUploadSessionState::Inactive;
    uint8_t busMask_ = CAN_UPLOAD_BUS_BOTH;
    bool active_ = false;
    uint32_t partialFlushMs_ = 0;
    uint32_t firstPendingAtMs_ = 0;
    uint16_t pendingCount_ = 0;
    uint16_t uploadCount_ = 0;
    bool uploadInProgress_ = false;
    uint32_t sessionSeq_ = 0;
    uint32_t frameSeq_ = 0;
    uint32_t batchSeq_ = 0;
    uint32_t sentBatches_ = 0;
    uint32_t failedBatches_ = 0;
    uint32_t uploadDropped_ = 0;
    uint32_t configDiscarded_ = 0;
    uint32_t filteredFrames_ = 0;
    uint32_t busFilteredFrames_ = 0;
    uint32_t stopDiscardedFrames_ = 0;
    uint16_t pendingHighWater_ = 0;
    uint32_t overloadEvents_ = 0;
    bool admissionPaused_ = false;
    int lastHttpCode_ = 0;
};
