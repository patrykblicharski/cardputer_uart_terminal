#pragma once
#include <cstdint>

// External ILI9341 320x240 — SPI2_HOST, spi_3wire=true, rotation=7
static constexpr int EXT_W = 320, EXT_H = 240;

// Layout zones (top bar / terminal / input line)
static constexpr int TOP_H   = 13;
static constexpr int INPUT_H = 14;
static constexpr int TERM_X  = 0;
static constexpr int TERM_Y  = TOP_H + 1;
static constexpr int TERM_W  = EXT_W;
static constexpr int INPUT_Y = EXT_H - INPUT_H;
static constexpr int TERM_H  = INPUT_Y - TERM_Y;   // 212 px

// Internal ST7789 240x135
static constexpr int INT_W = 240, INT_H = 135;

// RGB565 palette
static constexpr uint16_t C_GREEN  = 0x07E8u;
static constexpr uint16_t C_DIM    = 0x0364u;
static constexpr uint16_t C_AMBER  = 0xFC00u;
static constexpr uint16_t C_GRAY   = 0x39E7u;

// UART pins (hardware UART1)
static constexpr int UART_TX_PIN = 1;
static constexpr int UART_RX_PIN = 2;

// Available baud rates, default index 4 = 115200
static constexpr uint32_t kBaudRates[]   = {9600, 19200, 38400, 57600, 115200, 230400};
static constexpr size_t   kBaudRateCount = sizeof(kBaudRates) / sizeof(kBaudRates[0]);
