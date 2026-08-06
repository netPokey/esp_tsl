#pragma once

#include <cstddef>
#include "can_upload_mode.h"

constexpr size_t kAnalyzerUploadMaxUrl = CAN_UPLOAD_MAX_URL_LENGTH;

struct AnalyzerUploadConfig
{
    CanUploadMode mode = CanUploadMode::All;
    uint8_t busMask = CAN_UPLOAD_BUS_BOTH;
    char url[kAnalyzerUploadMaxUrl + 1] = {};
};

const char *analyzerUploadModeName(CanUploadMode mode);
bool analyzerUploadParseMode(const char *text, CanUploadMode &out);
const char *analyzerUploadBusesName(uint8_t mask);
bool analyzerUploadParseBuses(const char *text, uint8_t &out);
bool analyzerUploadNormalizeUrl(const char *input, char *out, size_t outSize);
bool analyzerUploadSanitizeConfig(const char *mode, const char *buses, const char *url,
                                  AnalyzerUploadConfig &out);
AnalyzerUploadConfig analyzerUploadDefaultConfig();
bool analyzerUploadLoad(AnalyzerUploadConfig &out);
bool analyzerUploadSave(const AnalyzerUploadConfig &config);
