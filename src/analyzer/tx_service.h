#pragma once
#include <cstdint>
#include "drivers/can_driver.h"

constexpr uint32_t kTxServiceMinIntervalMs = 10;
constexpr uint32_t kTxServiceMaxStandardId = 0x7FF;

enum class TxSendResult : uint8_t
{
    Ok,
    InvalidChannel,
    DriverUnavailable,
    TxDisabled,
    InvalidId,
    InvalidDlc,
    RateLimited,
};

class TxService
{
public:
    void init(CanDriver *canA, CanDriver *canB);
    TxSendResult sendSingle(uint8_t channel, uint32_t id, uint8_t dlc, const uint8_t *data, uint32_t now_ms);

private:
    CanDriver *driverFor(uint8_t channel) const;

    CanDriver *canA_ = nullptr;
    CanDriver *canB_ = nullptr;
    bool has_last_send_ = false;
    uint32_t last_send_ms_ = 0;
};
