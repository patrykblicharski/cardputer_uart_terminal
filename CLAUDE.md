# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Workspace Overview

Two related PlatformIO/Arduino ESP32 firmware projects that work together:

- **`cardputer_uart_terminal/`** — M5Stack Cardputer multi-tool launcher with dual-display UI (UART terminal, I2C scanner, LoRa channel)
- **`heltec_lora_node/`** — Heltec WiFi LoRa 32 v2 acting as a LoRa radio bridge, receiving UART commands from the Cardputer and transmitting over LoRa mesh

Together: user types in Cardputer → UART to Heltec → LoRa radio to remote nodes → responses come back the same path.

---

## Build & Flash Commands

Both projects use PlatformIO. Run from within each project directory.

```bash
# Cardputer — main app
pio run -e m5stack-cardputer                      # build
pio run -e m5stack-cardputer -t upload            # flash
pio device monitor -e m5stack-cardputer           # serial monitor

# Cardputer — hardware test environments
pio run -e test-keyboard -t upload                # keyboard diagnostic
pio run -e test-sd -t upload                      # SD card config test (tries FSPI/HSPI/global SPI)

# Heltec LoRa Node
pio run -e heltec_wifi_lora_32_V2                 # build
pio run -e heltec_wifi_lora_32_V2 -t upload       # flash
pio device monitor -e heltec_wifi_lora_32_V2      # serial monitor
```

No test framework — functional testing is done on-hardware via serial monitor.

---

## Cardputer — Architecture

**Target:** ESP32-S3, M5Stack Cardputer board (8 MB flash, QIO, OPI PSRAM)

**Two displays driven simultaneously:**
- External ILI9341 320×240 LCD (3-wire SPI2, custom driver in `src/display/ili9341.h`) — main UI / terminal output
- Internal ST7789 240×135 display (built into Cardputer) — status / hints

### State machine

`src/main.cpp` holds the top-level state machine. `transitionTo(AppState)` calls the appropriate `_Setup()` function and sets `g_session.app`. Every `loop()` tick dispatches to the active screen's `_Loop()`.

```
STATE_SPLASH
STATE_MENU               ← top-level app launcher
STATE_UART_MODE          ← UART mode selector (Wybierz config / Auto / Tylko terminal)
STATE_DETECT             ┐
STATE_SELECT_LIST        │  UART workflow
STATE_TERMINAL           │
STATE_MAINT_MENU         │
STATE_SENSOR_VIEW        │
STATE_SENSOR_CHART       ┘
STATE_APP_I2C            ← I2C scanner app
STATE_APP_LORA           ← LoRa channel app
STATE_APP_BLE_UART       ← BLE UART client (NUS scan/connect/terminal)
```

### Source file layout

```
src/
  config.h                   Hardware pinouts, display dims, baud rates, RGB565 palette
  app_state.h                AppState enum, SessionState struct, extern transitionTo()
  main.cpp                   setup()/loop() + transitionTo() dispatcher
  display/
    ili9341.h                Custom LGFX panel driver for ILI9341 — do not change SPI/rotation
    display_ext.h/cpp        External ILI9341 helpers (extDisplay, terminal sprite, chrome fns)
    display_int.h/cpp        Internal ST7789 helpers (intSprite, intStatus_Draw, intDisplay_ShowLabel)
  device/
    device_config.h/cpp      SD-card device config loader, identity parser
  screens/
    splash.h/cpp             Boot splash with progress bar
    menu.h/cpp               Top-level app launcher menu (kMenuItems[], ;/.=nav, ENTER=launch)
    uart_mode.h/cpp          UART mode selector: Wybierz config / Auto wykrywanie / Tylko terminal
    device_detect.h/cpp      Auto-detect UART host (HELLO/HELLO SIR handshake)
    select_list.h/cpp        Pick device config from SD card
    uart_term.h/cpp          Main UART terminal (keyboard, serial I/O, FN shortcuts)
    cmd_grid.h/cpp           CMD:cmd1|... overlay grid
    maint_menu.h/cpp         Maintenance overlay + sensor line parser
    sensor_view.h/cpp        Live sensor table (auto-refresh 500 ms)
    sensor_chart.h/cpp       Rolling line chart for sensors
  apps/
    app_i2c.h/cpp            I2C scanner app (bit-bang soft probe, device DB, sensor detail)
    app_lora.h/cpp           LoRa channel app (Serial1 bridge to Heltec, protocol parser)
    app_ble_uart.h/cpp       BLE UART client (NimBLE NUS scan/connect/terminal)
    i2c_db.h/cpp             50+ I2C device address database
    i2c_sensors.h/cpp        Sensor drivers: BMP280/BME280, MPU-6050, AHT20, SHT3x, BH1750, DHT12
```

### Display layout (external ILI9341 320×240)

- **UART terminal:** 13 px topbar + 212 px terminal sprite + 14 px input line
- **LoRa channel:** 13 px topbar + 173 px terminal sprite + 38 px target bar + 14 px input line
  - Terminal sprite is resized in `loraApp_Setup()` and restored by `extTerm_Init()`
- **I2C scanner / menu:** full 240 px, drawn directly on extDisplay (no sprite)

### Navigation — universal shortcut

**`FN+DEL`** = exit current screen / go back one level. Used consistently across ALL screens:
- In UART terminal → main menu
- In I2C app, LoRa app → main menu
- In maint overlay, sensor view/chart → back to terminal
- In detect, select_list → UART mode selector (STATE_UART_MODE)
- In uart_mode → main menu

**List/menu navigation:** `;` = up, `.` = down, `ENTER` = select. Applied universally: menu, uart_mode, select_list, maint_menu. `j`/`k` accepted as aliases in most screens.

### FN shortcuts — UART terminal

| Key | Action |
|---|---|
| FN+DEL | Exit to main menu |
| FN+1 | Cycle port mode: USB → UART → BOTH |
| FN+2 | Cycle baud rate (9600–230400) |
| FN+3 | Toggle local echo |
| FN+4 | Toggle ANSI escape filtering |
| FN+5 | Clear terminal |
| FN+6 | Toggle CRLF mode |
| FN+7 | Toggle font scale 1×/2× |
| FN+G | Toggle command grid overlay |
| FN+H | Open maintenance menu |
| CTRL+A..Z | Send raw control character (0x01–0x1A) |

**Defaults:** port=BOTH, baud=115200, echo=ON, ANSI filter=ON, CRLF=ON, font=1×

### FN shortcuts — I2C scanner

| Key | Action |
|---|---|
| FN+DEL | Exit to main menu |
| FN+1 | Cycle SDA/SCL pin pair |
| FN+R | Rescan (from results screen) |
| ENTER | Start scan / open device detail |
| DEL | Back (from detail to results) |
| ;/. | Navigate list / parameters |
| [/] | Decrease / increase parameter value |

**I2C note:** Uses bit-bang soft probe on configurable pins (default G3/G4). Never touches `Wire` or `Wire1` during scan to avoid corrupting the keyboard I2C bus (GPIO8/9). `Wire1` is used only in device detail view and is properly closed on exit.

### FN shortcuts — LoRa channel

| Key | Action |
|---|---|
| FN+DEL | Exit to main menu |
| FN+1 | Cycle send target (ALL / known nodes) |
| FN+2 | Request REPORT from target |
| FN+3 | Toggle MAINT on target |
| FN+4 | Request STATUS from Heltec |
| FN+5 | Toggle local MAINT mode |
| FN+6 | Cycle SF (7–12) |
| FN+7 | Cycle frequency preset (433.1 / 433.5 / 434.0 MHz) |

### FN shortcuts — BLE UART

| Key | Action |
|---|---|
| FN+DEL | Disconnect and exit to main menu |
| ENTER | Start scan / connect to selected device |
| ; / . | Navigate scan results |
| FN+1 | Idle: toggle All/NUS filter · Terminal: disconnect → results |
| FN+2 | Disconnect and rescan |
| FN+3 | Toggle local echo |
| FN+4 | Toggle ANSI filter |
| FN+5 | Clear terminal |
| FN+6 | Toggle CRLF |
| FN+7 | Toggle font 1×/2× |
| FN+R | Rescan (from results list) |
| DEL | Back (results → idle) |

**BLE note:** Uses NimBLE-Arduino as a Nordic UART Service (NUS) client. Scan discovers advertisers (optionally NUS-only); connect subscribes to TX notify and writes RX. Compatible with common BLE UART peripherals (nRF52, ESP32 NUS servers, etc.).

### Important invariants

- `LGFX_ILI9341` driver: SPI2_HOST, spi_3wire=true, rotation=7 — do not change without hardware test
- Keyboard I2C bus is on GPIO8/9 — never call `Wire.end()` / `Wire.begin()` at runtime
- `terminal` sprite (M5Canvas) is shared between UART and LoRa. `extTerm_Init()` resets it to 212 px height (TERM_H_FULL). `loraApp_Setup()` resizes to 173 px (LORA_TERM_H) to make room for the target bar
- `g_session` (SessionState) holds cross-screen state: device config, command list, chart buffers, last sensor sample
- **SD card uses `SPIClass(HSPI)` (SPI3)** — do NOT use global `SPI` / `SPI.begin()` which maps to FSPI (SPI2) and conflicts with LGFX. See `device/device_config.cpp`.
- `isChange()` is clear-on-read — global handler in `loop()` reads `keysState()` without calling `isChange()` to avoid consuming the flag before screen `_Loop()` handlers

**Dependencies:** `m5stack/M5Cardputer`, `m5stack/M5GFX`, `m5stack/M5Unified`, `bblanchon/ArduinoJson`, `h2zero/NimBLE-Arduino`

---

## Heltec LoRa Node — Architecture

**Target:** Heltec WiFi LoRa 32 v2 (ESP32, 433.5 MHz LoRa)

**Single source file:** `src/main.cpp` — complete implementation.

**Pins:** LoRa SPI on SCK=5/MISO=19/MOSI=27/CS=18/RST=14/DIO0=26; UART to Cardputer on RX=16, TX=17 at 115200.

**Configuration persisted to NVS** (Arduino `Preferences`, namespace `"lora_node"`): node ID, frequency, SF, BW, TX power, sync word.

**UART protocol (Cardputer → Heltec):**
```
SEND:TO:message          # broadcast MSG packet over LoRa
CMD:TO:payload           # send CMD packet (e.g. CMD:NODE2:MAINT:ON)
SET:KEY:VALUE            # reconfigure (SF, BW, FREQ, PWR, SW, ID)
STATUS                   # dump current config
MAINT:ON / MAINT:OFF     # toggle heartbeat maintenance mode
```

**UART responses (Heltec → Cardputer):**
```
BOOT:ID:VERSION          # on startup
RX:FROM:RSSI:SNR:TYPE:payload   # received LoRa packet
TX:OK / TX:FAIL          # transmission result
INFO:KEY:VALUE           # status key-value pairs
HB:ID:UPTIME             # heartbeat (maintenance mode, 30 s interval)
```

**LoRa air packet format:** `FROM>TO:TYPE:payload`
Packet types: `MSG`, `CMD`, `RPT`, `ACK`. `ALL` is the broadcast address; nodes filter on `TO == nodeId || TO == "ALL"`.

**Defaults:** freq=433.5 MHz, SF=9, BW=125 kHz, power=17 dBm, syncWord=0xAB, nodeId="NODE1"

**Dependency:** `sandeepmistry/LoRa@^0.8.0`
