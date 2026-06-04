#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "handlers.h"
#include "runtime_state.h"
#include "log_buffer.h"

#ifndef FLIPPER_UART_TX
#define FLIPPER_UART_TX 4
#endif
#ifndef FLIPPER_UART_RX
#define FLIPPER_UART_RX 5
#endif
#ifndef FLIPPER_UART_BAUD
#define FLIPPER_UART_BAUD 115200
#endif
#ifndef FLIPPER_FW_VERSION
#define FLIPPER_FW_VERSION "0.3.0"
#endif

// 串口桥接层。
// 作用：提供一条独立于 WiFi 页面之外的文本控制面，方便接 Flipper、蓝牙串口、脚本宿主。
class UartBridge
{
public:
    // 初始化串口桥，并绑定业务处理层与运行态。
    void begin(CarManagerBase *handler, DualCanRuntime *runtime)
    {
        handler_ = handler;
        runtime_ = runtime;
        port_.begin(FLIPPER_UART_BAUD, SERIAL_8N1, FLIPPER_UART_RX, FLIPPER_UART_TX);
        emitHello();
    }

    // 推进串口桥状态机。
    // 执行顺序是：收命令、按需推流状态、按需镜像日志。
    void loop()
    {
        consumeIncomingBytes();
        emitPeriodicStatusIfNeeded();
        mirrorNewLogsIfNeeded();
    }

private:
    // 串口桥内部使用的 UART1 端口。
    HardwareSerial port_{1};

    // 当前绑定的业务处理器。
    CarManagerBase *handler_ = nullptr;

    // 当前绑定的双 CAN 运行态。
    DualCanRuntime *runtime_ = nullptr;

    // 一行命令接收缓冲区。
    char rxBuffer_[128] = {};

    // 当前已写入接收缓冲的长度。
    size_t rxLength_ = 0;

    // 上次推流状态的发送时间戳。
    unsigned long lastEmitMs_ = 0;

    // 已经镜像到串口侧的日志数量。
    int mirroredLogCount_ = 0;

    // 是否启用周期状态推流。
    bool streamEnabled_ = false;

    // 是否启用日志增量镜像。
    bool logMirrorEnabled_ = false;

    // 按行读取串口输入。
    // 命令协议是轻量文本 DSL：一行一条，换行提交，超长输入直接丢弃重新同步。
    void consumeIncomingBytes()
    {
        while (port_.available())
        {
            const char ch = static_cast<char>(port_.read());
            if (ch == '\r')
                continue;
            if (ch == '\n')
            {
                rxBuffer_[rxLength_] = '\0';
                if (rxLength_ > 0)
                    dispatch(rxBuffer_);
                rxLength_ = 0;
                continue;
            }

            if (rxLength_ < sizeof(rxBuffer_) - 1)
                rxBuffer_[rxLength_++] = ch;
            else
                rxLength_ = 0;
        }
    }

    // 在推流模式下定期吐出三类摘要：控制状态、电池状态、双 CAN 统计。
    void emitPeriodicStatusIfNeeded()
    {
        if (!streamEnabled_)
            return;

        const unsigned long now = millis();
        if (now - lastEmitMs_ < 500)
            return;

        lastEmitMs_ = now;
        emitStatus();
        emitBattery();
        emitBusStats();
    }

    // 把新增日志镜像到串口侧。
    // 这样外部宿主不需要轮询整个日志缓冲，只消费增量事件即可。
    void mirrorNewLogsIfNeeded()
    {
        if (!logMirrorEnabled_)
        {
            mirroredLogCount_ = globalLog.count();
            return;
        }

        const int total = globalLog.count();
        if (total <= mirroredLogCount_)
            return;

        for (int i = mirroredLogCount_; i < total; ++i)
            port_.printf("EVT LOG %s\n", globalLog.get(i).message);

        mirroredLogCount_ = total;
    }

    // 分发一条已经收完整的命令行。
    // 协议格式固定为 `CMD <verb> [arg]`，这里把文本命令映射到统一业务控制接口。
    void dispatch(char *line)
    {
        char *prefix = strtok(line, " ");
        if (!prefix || strcasecmp(prefix, "CMD") != 0)
            return;

        char *verb = strtok(nullptr, " ");
        char *arg = strtok(nullptr, " ");
        if (!verb)
            return;

        if (strcasecmp(verb, "HELLO") == 0)
        {
            ack("HELLO");
            emitHello();
            return;
        }

        if (strcasecmp(verb, "STATUS") == 0)
        {
            ack("STATUS");
            emitStatus();
            emitBattery();
            emitBusStats();
            return;
        }

        if (strcasecmp(verb, "FSD") == 0)
        {
            bool value = false;
            if (!parseOnOff(arg, value) || !handler_)
            {
                err("FSD", "arg");
                return;
            }
            handler_->setForceFSDEnabled(value);
            port_.printf("ACK FSD %s\n", value ? "on" : "off");
            return;
        }

        if (strcasecmp(verb, "MODE") == 0)
        {
            if (!arg || !handler_)
            {
                err("MODE", "arg");
                return;
            }
            const int mode = atoi(arg);
            if (mode < 0 || mode > 4)
            {
                err("MODE", "range");
                return;
            }
            handler_->setSpeedProfileManual(mode);
            port_.printf("ACK MODE %d\n", mode);
            return;
        }

        if (strcasecmp(verb, "PRECOND") == 0)
        {
            bool value = false;
            if (!parseOnOff(arg, value) || !handler_)
            {
                err("PRECOND", "arg");
                return;
            }
            handler_->setPreconditioningRequested(value);
            port_.printf("ACK PRECOND %s\n", value ? "on" : "off");
            return;
        }

        if (strcasecmp(verb, "ISAOVR") == 0)
        {
            bool value = false;
            if (!parseOnOff(arg, value) || !handler_)
            {
                err("ISAOVR", "arg");
                return;
            }
            handler_->setIsaOverride(value);
            port_.printf("ACK ISAOVR %s\n", value ? "on" : "off");
            return;
        }

        if (strcasecmp(verb, "ISASUP") == 0)
        {
            bool value = false;
            if (!parseOnOff(arg, value) || !handler_)
            {
                err("ISASUP", "arg");
                return;
            }
            handler_->setIsaSuppress(value);
            port_.printf("ACK ISASUP %s\n", value ? "on" : "off");
            return;
        }

        if (strcasecmp(verb, "STREAM") == 0)
        {
            bool value = false;
            if (!parseOnOff(arg, value))
            {
                err("STREAM", "arg");
                return;
            }
            streamEnabled_ = value;
            port_.printf("ACK STREAM %s\n", value ? "on" : "off");
            return;
        }

        if (strcasecmp(verb, "LOG") == 0)
        {
            bool value = false;
            if (!parseOnOff(arg, value))
            {
                err("LOG", "arg");
                return;
            }
            logMirrorEnabled_ = value;
            port_.printf("ACK LOG %s\n", value ? "on" : "off");
            return;
        }

        err(verb, "unknown");
    }

    // 解析 on/off 或 1/0 形式的布尔参数。
    bool parseOnOff(const char *arg, bool &value) const
    {
        if (!arg)
            return false;
        if (strcasecmp(arg, "on") == 0 || strcmp(arg, "1") == 0)
        {
            value = true;
            return true;
        }
        if (strcasecmp(arg, "off") == 0 || strcmp(arg, "0") == 0)
        {
            value = false;
            return true;
        }
        return false;
    }

    // 输出握手消息，告诉外部控制器当前固件版本和运行模式。
    void emitHello()
    {
        port_.printf("EVT HELLO ver=%s mode=dual-can\n", FLIPPER_FW_VERSION);
    }

    void emitStatus()
    {
        if (!handler_)
            return;

        port_.printf("EVT STATUS fsd=%d force=%d mode=%d offset=%d ctrl=%s frames=%lu sent=%lu uptime=%lu\n",
                     handler_->fsdEnabled ? 1 : 0,
                     handler_->getForceFSDEnabled() ? 1 : 0,
                     handler_->speedProfile,
                     handler_->speedOffset,
                     handler_->controlBusName(),
                     static_cast<unsigned long>(handler_->frameCount),
                     static_cast<unsigned long>(handler_->sentCount),
                     static_cast<unsigned long>(millis()));
    }

    void emitBattery()
    {
        if (!handler_)
            return;

        port_.printf("EVT BATTERY soc=%.1f v=%.1f i=%.1f kw=%.2f tmin=%.1f tmax=%.1f wh=%.1f precond=%d allow=%d worth=%d\n",
                     handler_->socPercent,
                     handler_->packVoltage,
                     handler_->packCurrent,
                     handler_->packPowerKW,
                     handler_->packTempMin,
                     handler_->packTempMax,
                     handler_->whPerKm,
                     handler_->precondActive ? 1 : 0,
                     handler_->precondAllowed ? 1 : 0,
                     handler_->precondWorthwhile ? 1 : 0);
    }

    void emitBusStats()
    {
        if (!runtime_)
            return;

        port_.printf("EVT BUS can_a_rx=%lu can_a_tx=%lu can_b_rx=%lu can_b_tx=%lu\n",
                     static_cast<unsigned long>(runtime_->busA.rxFrames),
                     static_cast<unsigned long>(runtime_->busA.txFrames),
                     static_cast<unsigned long>(runtime_->busB.rxFrames),
                     static_cast<unsigned long>(runtime_->busB.txFrames));
    }

    void ack(const char *name)
    {
        port_.printf("ACK %s\n", name);
    }

    void err(const char *name, const char *reason)
    {
        port_.printf("ERR %s %s\n", name, reason);
    }
};