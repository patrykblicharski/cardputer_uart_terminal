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

// Command grid overlay
static constexpr int CMD_GRID_H  = 34;
static constexpr int CMD_GRID_Y  = INPUT_Y - CMD_GRID_H;   // 192
static constexpr int TERM_H_FULL = TERM_H;                  // 212
static constexpr int TERM_H_GRID = CMD_GRID_Y - TERM_Y;    // 178

// Internal ST7789 240x135
static constexpr int INT_W = 240, INT_H = 135;

// RGB565 palette
static constexpr uint16_t C_GREEN  = 0x07E8u;
static constexpr uint16_t C_DIM    = 0x0364u;
static constexpr uint16_t C_AMBER  = 0xFC00u;
static constexpr uint16_t C_GRAY   = 0x39E7u;

// Chart colours (4 sensors)
static constexpr uint16_t C_CHART[4] = { 0x07E8u, 0xFC00u, 0x001Fu, 0xF800u };
static constexpr int CHART_LEFT_W  = 80;
static constexpr int CHART_SAMPLES = 120;
static constexpr int MAX_SENSORS   = 16;

// UART pins (hardware UART1)
static constexpr int UART_TX_PIN = 1;
static constexpr int UART_RX_PIN = 2;

// SD card — SPI2 shared with ILI9341
static constexpr int SD_CS_PIN   = 12;
static constexpr int SD_SCK_PIN  = 40;
static constexpr int SD_MISO_PIN = 39;
static constexpr int SD_MOSI_PIN = 14;

// Available baud rates, default index 4 = 115200
static constexpr uint32_t kBaudRates[]   = {9600, 19200, 38400, 57600, 115200, 230400};
static constexpr size_t   kBaudRateCount = sizeof(kBaudRates) / sizeof(kBaudRates[0]);
