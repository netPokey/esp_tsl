#pragma once

#include <cstdint>
#include <cstring>
#include <Arduino.h>

// 单条日志结构。
// 作用域：给 Web 页面、串口镜像、现场排错统一提供一份轻量级时间线数据。
struct LogEntry
{
    uint32_t timestamp = 0;
    char message[96] = {};
};

// 固定容量环形日志缓冲。
// 设计目标：不依赖动态分配，在长时间运行时仍然保持内存占用稳定。
class LogBuffer
{
public:
    static constexpr int kMaxEntries = 64;

    // 追加一条文本日志。
    // 约束：超过最大容量后自动覆盖最旧记录，适合设备侧持续运行场景。
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

    // 返回当前有效日志数量。
    int count() const { return count_; }

    // 返回环形缓冲的当前写入头位置。
    int head() const { return head_; }

    // 读取一条按时间顺序排列的日志。
    // 输入超界时会自动压到合法范围，避免上层页面和串口桥因为索引问题崩掉。
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

// 全局日志单例入口。
// 用函数静态对象而不是头文件内联变量，是为了兼容当前工具链和头文件分发方式。
inline LogBuffer &globalLogStorage()
{
    static LogBuffer value;
    return value;
}

#define globalLog globalLogStorage()