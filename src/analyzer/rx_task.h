#pragma once
#include "analyzer/frame_queue.h"
#include "drivers/can_driver.h"

void rxTaskStart(CanDriver *driverA, CanDriver *driverB, FrameQueue *queue);
