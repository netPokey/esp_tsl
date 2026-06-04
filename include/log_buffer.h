#pragma once

#include <cstdint>
#include <cstring>
#include <Arduino.h>

// 单条日志结构。
// 作用域：给 Web 页面、串口镜像、现场排错统一提供一份轻量级时间线数据。
struct LogEntry
{
    // 记录写入该日志时的毫秒时间戳。
    uint32_t timestamp = 0;

    // 记录固定长度日志文本，避免运行时动态分配。
    char message[96] = {};
};

// 固定容量环形日志缓冲。
// 设计目标：不依赖动态分配，在长时间运行时仍然保持内存占用稳定。
class LogBuffer
{
public:
    // 最多保留的日志条目数。
    static constexpr int kMaxEntries = 64;

    // 追加一条文本日志。
    // 约束：超过最大容量后自动覆盖最旧记录，适合设备侧持续运行场景。
    void add(const char *msg)
    {
        if (!msg)
            return;

        entries_[headIndex_].timestamp = millis();
        strncpy(entries_[headIndex_].message, msg, sizeof(entries_[headIndex_].message) - 1);
        entries_[headIndex_].message[sizeof(entries_[headIndex_].message) - 1] = '\0';

        headIndex_ = (headIndex_ + 1) % kMaxEntries;
        if (entryCount_ < kMaxEntries)
            entryCount_++;
    }

    // 返回当前有效日志数量。
    int count() const { return entryCount_; }

    // 返回环形缓冲的当前写入头位置。
    int head() const { return headIndex_; }

    // 读取一条按时间顺序排列的日志。
    // 输入超界时会自动压到合法范围，避免上层页面和串口桥因为索引问题崩掉。
    const LogEntry &get(int index) const
    {
        if (entryCount_ <= 0)
            return emptyEntry_;

        int safeIndex = index;
        if (safeIndex < 0)
            safeIndex = 0;
        if (safeIndex >= entryCount_)
            safeIndex = entryCount_ - 1;

        const int actualIndex = (headIndex_ - entryCount_ + safeIndex + kMaxEntries) % kMaxEntries;
        return entries_[actualIndex];
    }

private:
    // 环形日志实体存储区。
    LogEntry entries_[kMaxEntries] = {};

    // 当日志为空时返回的占位对象。
    LogEntry emptyEntry_{};

    // 下一条日志将写入的位置。
    int headIndex_ = 0;

    // 当前有效日志条目数量。
    int entryCount_ = 0;
};

// 全局日志单例入口。
// 用函数静态对象而不是头文件内联变量，是为了兼容当前工具链和头文件分发方式。
inline LogBuffer &globalLogStorage()
{
    static LogBuffer value;
    return value;
}

#define globalLog globalLogStorage()