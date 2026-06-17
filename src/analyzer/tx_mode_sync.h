#pragma once
#include "drivers/can_driver.h"

class TxModeSync
{
public:
    void sync(CanDriver *driverA, CanDriver *driverB);

private:
    bool hasApplied_[2] = {false, false};
    CanBusMode appliedMode_[2] = {CanBusMode::ListenOnly, CanBusMode::ListenOnly};
};
