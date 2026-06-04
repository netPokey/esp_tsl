#pragma once

#include "../libraries/private_library/pin_config.h"

// 板载状态 LED。
// 当前固件用它表现“正在处理总线流量 / 当前空闲”的最小心跳状态。
#ifndef PIN_LED
#define PIN_LED 8
#endif