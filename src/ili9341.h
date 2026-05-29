#pragma once
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_LCD.hpp>

// ILI9341 panel driver — confirmed-working config.
// spi_3wire=true required: half-duplex write-only (pin_miso=-1).
// rotation=7, SPI2_HOST. Do not change without re-testing on hardware.
struct Panel_ILI9341_Local : public lgfx::v1::Panel_LCD {
    Panel_ILI9341_Local() {
        _cfg.memory_width  = _cfg.panel_width  = 240;
        _cfg.memory_height = _cfg.panel_height = 320;
    }
    void setColorDepth_impl(lgfx::v1::color_depth_t) override {
        _write_depth = lgfx::v1::rgb565_2Byte;
        _read_depth  = lgfx::v1::rgb888_3Byte;
    }
protected:
    const uint8_t* getInitCommands(uint8_t listno) const override {
        static constexpr uint8_t list0[] = {
            0x01, 0+CMD_INIT_DELAY, 150,
            0xCB, 5, 0x39,0x2C,0x00,0x34,0x02,
            0xCF, 3, 0x00,0xC1,0x30,
            0xE8, 3, 0x85,0x00,0x78,
            0xEA, 2, 0x00,0x00,
            0xED, 4, 0x64,0x03,0x12,0x81,
            0xF7, 1, 0x20,
            0xC0, 1, 0x23, 0xC1, 1, 0x10,
            0xC5, 2, 0x3e,0x28, 0xC7, 1, 0x86,
            0x36, 1, 0x48, 0x3A, 1, 0x55,
            0xB1, 2, 0x00,0x18,
            0xB6, 3, 0x08,0x82,0x27,
            0xF2, 1, 0x00, 0x26, 1, 0x01,
            0xE0, 15, 0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                       0x37,0x07,0x10,0x03,0x0E,0x09,0x00,
            0xE1, 15, 0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                       0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F,
            0x11, 0+CMD_INIT_DELAY, 120,
            0x29, 0+CMD_INIT_DELAY, 120,
            0xFF, 0xFF,
        };
        return (listno == 0) ? list0 : nullptr;
    }
};

class LGFX_ILI9341 : public lgfx::v1::LGFX_Device {
    Panel_ILI9341_Local _panel;
    lgfx::v1::Bus_SPI   _bus;
public:
    LGFX_ILI9341() {
        auto b = _bus.config();
        b.spi_host    = SPI2_HOST;
        b.spi_mode    = 0;
        b.freq_write  = 20000000;
        b.freq_read   = 16000000;
        b.spi_3wire   = true;
        b.use_lock    = true;
        b.dma_channel = SPI_DMA_CH_AUTO;
        b.pin_sclk = 40; b.pin_mosi = 14; b.pin_miso = -1; b.pin_dc = 15;
        _bus.config(b);
        _panel.setBus(&_bus);
        auto p = _panel.config();
        p.pin_cs = 5; p.pin_rst = 13; p.pin_busy = -1;
        p.readable = false; p.invert = false; p.rgb_order = false;
        p.memory_width = 240; p.memory_height = 320;
        p.panel_width  = 240; p.panel_height  = 320;
        _panel.config(p);
        setPanel(&_panel);
    }
};
