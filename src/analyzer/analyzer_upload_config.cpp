#include "analyzer/analyzer_upload_config.h"

#include <cctype>
#include <cstring>

#if defined(ARDUINO)
#include <Preferences.h>
#endif

namespace
{
constexpr const char *kPrefsNamespace = "analyzer_upload";
constexpr const char *kPrefsMode = "mode";
constexpr const char *kPrefsUrl = "url";

bool equalsIgnoreCase(const char *left, const char *right)
{
    if (!left || !right)
        return false;
    while (*left && *right)
    {
        if (std::tolower(static_cast<unsigned char>(*left)) !=
            std::tolower(static_cast<unsigned char>(*right)))
            return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

bool isUrlCharacterAllowed(char c)
{
    const unsigned char value = static_cast<unsigned char>(c);
    return value >= 0x21 && value <= 0x7e && c != '"' && c != '\\';
}

bool hasExplicitScheme(const char *text)
{
    return strstr(text, "://") != nullptr;
}

bool appendText(char *out, size_t outSize, size_t &offset, const char *text, size_t length)
{
    if (offset + length >= outSize)
        return false;
    memcpy(out + offset, text, length);
    offset += length;
    out[offset] = '\0';
    return true;
}

bool validPort(const char *begin, const char *end)
{
    if (begin == end)
        return false;
    unsigned long port = 0;
    for (const char *it = begin; it != end; ++it)
    {
        if (!std::isdigit(static_cast<unsigned char>(*it)))
            return false;
        port = port * 10 + static_cast<unsigned long>(*it - '0');
        if (port > 65535)
            return false;
    }
    return port >= 1;
}

bool validateAuthority(const char *begin, const char *end)
{
    if (begin == end || static_cast<size_t>(end - begin) > 253 || memchr(begin, '@', end - begin))
        return false;

    if (*begin == '[')
    {
        const char *close = static_cast<const char *>(memchr(begin, ']', end - begin));
        if (!close || close == begin + 1)
            return false;
        return close + 1 == end || (*(close + 1) == ':' && validPort(close + 2, end));
    }

    const char *colon = static_cast<const char *>(memchr(begin, ':', end - begin));
    const char *hostEnd = colon ? colon : end;
    if (hostEnd == begin || *begin == '.' || hostEnd[-1] == '.')
        return false;
    for (const char *it = begin; it != hostEnd; ++it)
    {
        const unsigned char c = static_cast<unsigned char>(*it);
        if (!(std::isalnum(c) || c == '.' || c == '-'))
            return false;
    }
    return !colon || validPort(colon + 1, end);
}
}

const char *analyzerUploadModeName(CanUploadMode mode)
{
    switch (mode)
    {
    case CanUploadMode::Off:
        return "off";
    case CanUploadMode::All:
        return "all";
    case CanUploadMode::Critical:
        return "critical";
    default:
        return "off";
    }
}

bool analyzerUploadParseMode(const char *text, CanUploadMode &out)
{
    if (equalsIgnoreCase(text, "off"))
        out = CanUploadMode::Off;
    else if (equalsIgnoreCase(text, "all"))
        out = CanUploadMode::All;
    else if (equalsIgnoreCase(text, "critical"))
        out = CanUploadMode::Critical;
    else
        return false;
    return true;
}

bool analyzerUploadNormalizeUrl(const char *input, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';
    if (!input)
        return false;

    while (*input && std::isspace(static_cast<unsigned char>(*input)))
        ++input;
    const char *end = input + strlen(input);
    while (end > input && std::isspace(static_cast<unsigned char>(end[-1])))
        --end;
    if (end == input)
        return false;

    const size_t inputLength = static_cast<size_t>(end - input);
    for (size_t i = 0; i < inputLength; ++i)
    {
        if (!isUrlCharacterAllowed(input[i]))
            return false;
    }

    constexpr const char *kHttpPrefix = "http://";
    constexpr size_t kHttpPrefixLength = 7;
    const bool hasHttpPrefix = inputLength >= kHttpPrefixLength && strncmp(input, kHttpPrefix, kHttpPrefixLength) == 0;
    if (!hasHttpPrefix && hasExplicitScheme(input))
        return false;

    const char *authority = input + (hasHttpPrefix ? kHttpPrefixLength : 0);
    const char *inputEnd = input + inputLength;
    const char *path = static_cast<const char *>(memchr(authority, '/', inputEnd - authority));
    const char *authorityEnd = path ? path : inputEnd;
    if (!validateAuthority(authority, authorityEnd))
        return false;
    if (memchr(authority, '?', inputEnd - authority) || memchr(authority, '#', inputEnd - authority))
        return false;
    if (path)
    {
        const size_t pathLength = static_cast<size_t>(inputEnd - path);
        if (!((pathLength == 1 && path[0] == '/') ||
              (pathLength == 10 && memcmp(path, "/can/batch", 10) == 0)))
            return false;
    }

    size_t offset = 0;
    if (!appendText(out, outSize, offset, kHttpPrefix, kHttpPrefixLength) ||
        !appendText(out, outSize, offset, authority, static_cast<size_t>(authorityEnd - authority)))
    {
        out[0] = '\0';
        return false;
    }
    constexpr const char *kBatchPath = "/can/batch";
    if (!appendText(out, outSize, offset, kBatchPath, strlen(kBatchPath)) || offset > kAnalyzerUploadMaxUrl)
    {
        out[0] = '\0';
        return false;
    }
    return true;
}

AnalyzerUploadConfig analyzerUploadDefaultConfig()
{
    AnalyzerUploadConfig result;
    result.mode = CanUploadMode::Off;
    analyzerUploadNormalizeUrl(kAnalyzerUploadDefaultUrl, result.url, sizeof(result.url));
    return result;
}

bool analyzerUploadSanitizeConfig(const char *mode,
                                  const char *url,
                                  AnalyzerUploadConfig &out)
{
    out = analyzerUploadDefaultConfig();
    CanUploadMode parsedMode;
    char normalizedUrl[kAnalyzerUploadMaxUrl + 1] = {};
    if (!analyzerUploadParseMode(mode, parsedMode) ||
        !analyzerUploadNormalizeUrl(url, normalizedUrl, sizeof(normalizedUrl)))
        return false;

    out.mode = parsedMode;
    memcpy(out.url, normalizedUrl, strlen(normalizedUrl) + 1);
    return true;
}

bool analyzerUploadLoad(AnalyzerUploadConfig &out)
{
    out = analyzerUploadDefaultConfig();
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, true))
        return false;
    const String mode = prefs.getString(kPrefsMode, analyzerUploadModeName(out.mode));
    const String url = prefs.getString(kPrefsUrl, out.url);
    prefs.end();

    AnalyzerUploadConfig loaded;
    if (!analyzerUploadSanitizeConfig(mode.c_str(), url.c_str(), loaded))
        return false;
    out = loaded;
#endif
    return true;
}

bool analyzerUploadSave(const AnalyzerUploadConfig &config)
{
    AnalyzerUploadConfig sanitized;
    if (!analyzerUploadSanitizeConfig(analyzerUploadModeName(config.mode), config.url, sanitized))
        return false;
#if defined(ARDUINO)
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false))
        return false;
    const size_t modeWritten = prefs.putString(kPrefsMode, analyzerUploadModeName(sanitized.mode));
    const size_t urlWritten = prefs.putString(kPrefsUrl, sanitized.url);
    prefs.end();
    return modeWritten == strlen(analyzerUploadModeName(sanitized.mode)) &&
           urlWritten == strlen(sanitized.url);
#else
    (void)sanitized;
#endif
    return true;
}
