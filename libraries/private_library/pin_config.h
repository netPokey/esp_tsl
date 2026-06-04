/*
 * 板级引脚定义。
 * 作用：集中维护当前 ESP32-S3 + 双 CAN 适配板的硬件连线，
 * 让驱动层和主流程不直接散落硬编码引脚号。
 */
#pragma once

// 编译目标：双路经典 CAN。
#define T_2Can
// #define T_2Can_Fd

// ESP32 内建 TWAI 控制器引脚。
// 这一路在工程里对应 CAN_B。
#define CAN_TX 7
#define CAN_RX 6

// 公共 SPI 引脚。
// 外接 MCP2515 通过这组 SPI 线挂在主控上。
#define SPI_SCLK 12
#define SPI_MOSI 11
#define SPI_MISO 13

#if defined T_2Can
// MCP2515 外置控制器引脚。
// 这一路在工程里对应 CAN_A。
#define MCP2515_CS 10
#define MCP2515_SCLK SPI_SCLK
#define MCP2515_MOSI SPI_MOSI
#define MCP2515_MISO SPI_MISO
#define MCP2515_RST 9
#define MCP2515_INT 8
#elif defined T_2Can_Fd
// MCP2518 CAN FD 控制器预留定义。
#define MCP2518_CS 10
#define MCP2518_SCLK SPI_SCLK
#define MCP2518_MOSI SPI_MOSI
#define MCP2518_MISO SPI_MISO

#define MCP2518_INT 8
#define MCP2518_INT_0 9
#define MCP2518_INT_1 3
#else
#error "no macro definition is set"
#endif

// ESP 下载/启动相关引脚。
#define ESP_BOOT 0