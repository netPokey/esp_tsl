#include "analyzer/tx_mode_sync.h"
#include "analyzer/analyzer_control.h"

namespace
{
void syncChannel(CanDriver *driver, uint8_t channel, bool &hasApplied, CanBusMode &appliedMode)
{
    if (!driver || !isAnalyzerChannelOnline(channel))
        return;

    const CanBusMode targetMode = shouldAllowAnalyzerChannelTx(channel) ? CanBusMode::Normal : CanBusMode::ListenOnly;
    if (hasApplied && appliedMode == targetMode)
        return;

    if (driver->setBusMode(targetMode))
    {
        hasApplied = true;
        appliedMode = targetMode;
    }
}
}

void TxModeSync::sync(CanDriver *driverA, CanDriver *driverB)
{
    syncChannel(driverA, 0, hasApplied_[0], appliedMode_[0]);
    syncChannel(driverB, 1, hasApplied_[1], appliedMode_[1]);
}
