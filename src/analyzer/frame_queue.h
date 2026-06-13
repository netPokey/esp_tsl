#pragma once
#include <cstdint>
#include "analyzer_types.h"
#include "shared_types.h"

// 单生产者单消费者（SPSC）无锁环形队列。
// 生产者：Core0 采集任务（push）。消费者：Core1 分析循环（pop）。
// 缓冲由调用方提供：测试用栈/堆，设备用 PSRAM。可用容量为 capacity-1。
class FrameQueue
{
public:
    void init(CapturedFrame *buffer, uint16_t capacity);

    // 生产者侧调用。队列满时丢弃当前帧、累加 dropped，返回 false。
    bool push(const CapturedFrame &frame);

    // 消费者侧调用。无数据返回 false。
    bool pop(CapturedFrame &out);

    uint32_t dropped() const;

private:
    CapturedFrame *buffer_ = nullptr;
    uint16_t capacity_ = 0;
    Shared<uint16_t> head_{0};
    Shared<uint16_t> tail_{0};
    Shared<uint32_t> dropped_{0};
};
