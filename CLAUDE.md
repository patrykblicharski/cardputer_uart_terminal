# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Workspace Overview

Two related PlatformIO/Arduino ESP32 firmware projects that work together:

- **`cardputer_uart_terminal/`** — M5Stack Cardputer handheld device running a dual-display UART terminal emulator
- **`heltec_lora_node/`** — Heltec WiFi LoRa 32 v2 acting as a LoRa radio bridge, receiving UART commands from the Cardputer and transmitting over LoRa mesh

Together: user types in Cardputer → UART to Heltec → LoRa radio to remote nodes → responses come back the same path.

---

## Build & Flash Commands

Both projects use PlatformIO. Run from within each project directory.

```bash
# Cardputer
pio run -e m5stack-cardputer                      # build
pio run -e m5stack-cardputer -t upload            # flash
pio device monitor -e m5stack-cardputer           # serial monitor

# Heltec LoRa Node
pio run -e heltec_wifi_lora_32_V2                 # build
pio run -e heltec_wifi_lora_32_V2 -t upload       # flash
pio device monitor -e heltec_wifi_lora_32_V2      # serial monitor
```

No test framework — functional testing is done on-hardware via serial monitor.

---

## Cardputer UART Terminal — Architecture

**Target:** ESP32-S3, M5Stack Cardputer board (8 MB flash, QIO, OPI PSRAM)

**Two displays driven simultaneously:**
- External ILI9341 320×240 LCD (3-wire SPI2, custom driver in `ili9341.h`) — terminal output
- Internal ST7789 240×135 display (built into Cardputer) — status bar (port, baud, options, hotkeys)

**Source files:**

| File | Role |
|---|---|
| `src/config.h` | All hardware pinouts, display dimensions, baud rate list, color palette |
| `src/main.cpp` | Init only: brings up M5Cardputer, calls display init and terminal loop |
| `src/uart_term.cpp/h` | All keyboard input, serial I/O, port/baud management, FN key shortcuts |
| `src/display_ext.cpp/h` | External ILI9341 terminal rendering (sprite-based, 320×212 px text area) |
| `src/display_int.cpp/h` | Internal ST7789 status display updates |
| `src/ili9341.h` | Custom LGFX panel driver for ILI9341 — do not change SPI/rotation settings without hardware testing |

**Display layout (external):** 13 px top bar + 212 px terminal sprite + 14 px input line = 240 px height total.

**FN key shortcuts:**

| Key | Action |
|---|---|
| FN+1 | Cycle port mode: USB → UART → BOTH |
| FN+2 | Cycle baud rate (9600–230400) |
| FN+3 | Toggle local echo |
| FN+4 | Toggle ANSI escape filtering |
| FN+5 | Clear terminal |
| FN+6 | Toggle CRLF mode |
| FN+7 | Toggle font scale 1×/2× |
| CTRL+key | Send raw control character (0x01–0x1A) |

**Defaults:** port=BOTH, baud=115200, echo=ON, ANSI filter=ON, CRLF=ON, font=1×

**Dependencies:** `m5stack/M5Cardputer`, `m5stack/M5GFX`, `m5stack/M5Unified`

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
