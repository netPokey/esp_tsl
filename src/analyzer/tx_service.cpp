#include "analyzer/tx_service.h"
#include "analyzer/analyzer_control.h"

void TxService::init(CanDriver *canA, CanDriver *canB)
{
    canA_ = canA;
    canB_ = canB;
    has_last_send_ = false;
    last_send_ms_ = 0;
}

CanDriver *TxService::driverFor(uint8_t channel) const
{
    if (channel == 0)
        return canA_;
    if (channel == 1)
        return canB_;
    return nullptr;
}

TxSendResult TxService::sendSingle(uint8_t channel, uint32_t id, uint8_t dlc, const uint8_t *data, uint32_t now_ms)
{
    if (channel > 1)
        return TxSendResult::InvalidChannel;

    CanDriver *driver = driverFor(channel);
    if (!driver)
        return TxSendResult::DriverUnavailable;

    if (!shouldAllowAnalyzerChannelTx(channel))
        return TxSendResult::TxDisabled;

    if (id > kTxServiceMaxStandardId)
        return TxSendResult::InvalidId;

    if (dlc > 8 || (dlc > 0 && !data))
        return TxSendResult::InvalidDlc;

    if (has_last_send_ && static_cast<uint32_t>(now_ms - last_send_ms_) < kTxServiceMinIntervalMs)
        return TxSendResult::RateLimited;

    CanFrame frame{};
    frame.id = id;
    frame.dlc = dlc;
    for (uint8_t i = 0; i < dlc; ++i)
        frame.data[i] = data[i];

    driver->send(frame);
    has_last_send_ = true;
    last_send_ms_ = now_ms;
    return TxSendResult::Ok;
}
