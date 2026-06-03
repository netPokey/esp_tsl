#pragma once

#include <cstdint>
#include <cstring>
#include <Arduino.h>

struct LogEntry
{
    uint32_t timestamp = 0;
    char message[96] = {};
};

class LogBuffer
{
public:
    static constexpr int kMaxEntries = 64;

    void add(const char *msg)
    {
        if (!msg)
            return;

        entries_[head_].timestamp = millis();
        strncpy(entries_[head_].message, msg, sizeof(entries_[head_].message) - 1);
        entries_[head_].message[sizeof(entries_[head_].message) - 1] = '\0';

        head_ = (head_ + 1) % kMaxEntries;
        if (count_ < kMaxEntries)
            count_++;
    }

    int count() const { return count_; }
    int head() const { return head_; }

    const LogEntry &get(int index) const
    {
        if (count_ <= 0)
            return empty_;

        int safeIndex = index;
        if (safeIndex < 0)
            safeIndex = 0;
        if (safeIndex >= count_)
            safeIndex = count_ - 1;

        const int actual = (head_ - count_ + safeIndex + kMaxEntries) % kMaxEntries;
        return entries_[actual];
    }

private:
    LogEntry entries_[kMaxEntries] = {};
    LogEntry empty_{};
    int head_ = 0;
    int count_ = 0;
};

inline LogBuffer &globalLogStorage()
{
    static LogBuffer value;
    return value;
}

#define globalLog globalLogStorage()