#pragma once

#include <cstddef>

#include "can_upload_mode.h"

constexpr size_t kAnalyzerUploadMaxUrl = CAN_UPLOAD_MAX_URL_LENGTH;
constexpr const char *kAnalyzerUploadDefaultUrl = CAN_UPLOAD_DEFAULT_URL;

struct AnalyzerUploadConfig
{
    CanUploadMode mode = CanUploadMode::Off;
    char url[kAnalyzerUploadMaxUrl + 1] = {};
};

// Pure helpers shared by the Web/API layer and native tests.
const char *analyzerUploadModeName(CanUploadMode mode);
bool analyzerUploadParseMode(const char *text, CanUploadMode &out);
bool analyzerUploadNormalizeUrl(const char *input, char *out, size_t outSize);
bool analyzerUploadSanitizeConfig(const char *mode,
                                  const char *url,
                                  AnalyzerUploadConfig &out);
AnalyzerUploadConfig analyzerUploadDefaultConfig();

// On Arduino these use the analyzer_upload NVS namespace. Native builds return
// defaults/load success and keep all parsing helpers free of framework types.
bool analyzerUploadLoad(AnalyzerUploadConfig &out);
bool analyzerUploadSave(const AnalyzerUploadConfig &config);
