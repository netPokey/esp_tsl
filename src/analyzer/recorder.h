#pragma once
#include <cstddef>
#include <cstdint>
#include "analyzer_types.h"

// 会话式录制器：固定容量环形缓冲，满则覆盖最旧帧并累加 dropped。
// 无内部锁。push 由 Core1 drain 调用（仅 active 时）；collect 由 AsyncTCP 下载
// 任务调用。二者互斥靠「下载仅在 stop 后允许」保证（download 路由拒绝 active）。
// start/stop 经 pending 队列串行化，与 drain 同在 Core1 loop，无竞争。
class Recorder
{
public:
    void init(CapturedFrame *storage, size_t capacity);
    void start();                 // 清空 head/count/dropped，置 active
    void stop();                  // 清 active；缓冲内容保留供下载
    bool active() const { return active_; }
    size_t count() const { return count_; }
    size_t capacity() const { return capacity_; }
    uint32_t dropped() const { return dropped_; }
    void push(const CapturedFrame &frame);
    // 旧->新顺序，跳过前 skip 帧，最多写 cap 帧到 out，返回写入帧数。
    size_t collect(CapturedFrame *out, size_t cap, size_t skip) const;

private:
    CapturedFrame *storage_ = nullptr;
    size_t capacity_ = 0;
    size_t head_ = 0;             // 下一个写入位置
    size_t count_ = 0;
    uint32_t dropped_ = 0;
    bool active_ = false;
};
