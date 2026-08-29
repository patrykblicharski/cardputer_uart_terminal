# Architektura — Cardputer UART Terminal

Firmware PlatformIO/Arduino ESP32-S3 dla M5Stack Cardputer.
Dwa projekty w workspace: `cardputer_uart_terminal/` (główny) i `heltec_lora_node/` (LoRa bridge).

## State machine

`main.cpp` — prosta maszyna stanów: `transitionTo(AppState)` wywołuje `_Setup()`, `loop()` wywołuje `_Loop()`.

```
STATE_SPLASH
    └─> STATE_MENU
            ├─> STATE_UART_MODE          ← wybór trybu UART (nowy)
            │       ├─> STATE_SELECT_LIST    wybierz config z SD
            │       ├─> STATE_DETECT         auto-wykrywanie (HELLO)
            │       └─> STATE_TERMINAL       bezpośredni terminal
            │
            ├─> STATE_APP_I2C            ← skaner I2C
            └─> STATE_APP_LORA           ← kanał LoRa
            └─> STATE_APP_BLE_UART       ← BLE UART (NUS)

STATE_TERMINAL
    └─> STATE_MAINT_MENU
            ├─> STATE_SENSOR_VIEW
            └─> STATE_SENSOR_CHART
```

Stan globalny: `SessionState g_session` w `app_state.h`.

## Struktura folderów

```
src/
├── main.cpp          ← entry point, state machine
├── config.h          ← piny, stałe, paleta (wszędzie importowane)
├── app_state.h       ← AppState enum, SessionState, SensorSample (wszędzie importowane)
├── display/          ← sterowniki wyświetlaczy
│   ├── ili9341.h          ← custom LGFX driver (nie ruszać!)
│   ├── display_ext.h/cpp  ← ILI9341 320×240 external
│   └── display_int.h/cpp  ← ST7789 240×135 internal
├── device/           ← model danych, SD/JSON
│   └── device_config.h/cpp
└── screens/          ← wszystkie stany aplikacji
    ├── splash / menu
    ├── uart_mode                  ← NOWY — wybór trybu UART
    ├── device_detect / select_list
    ├── uart_term / cmd_grid
    ├── maint_menu
    └── sensor_view / sensor_chart
```

Konwencja includów: cross-folder = `"display/display_ext.h"`, same-folder = `"cmd_grid.h"`, root = `"config.h"`.
PlatformIO automatycznie dodaje `src/` i wszystkie podfoldery do include path — nie trzeba edytować `platformio.ini`.

## Pliki źródłowe

| Plik | Odpowiedzialność |
|------|-----------------|
| `config.h` | Piny, stałe layoutu, paleta RGB565, baudy |
| `app_state.h` | `AppState` enum, `SessionState`, `SensorSample`, `DeviceConfig` |
| `main.cpp` | setup/loop, `transitionTo()`, includes wszystkich stanów |
| `display/display_ext.h/cpp` | ILI9341 320×240 — sprite terminal, topbar, input line |
| `display/display_int.h/cpp` | ST7789 240×135 — status bar, `intDisplay_ShowLabel()` |
| `display/ili9341.h` | Custom LGFX driver — nie ruszać konfiguracji SPI/rotation |
| `screens/uart_mode.cpp/h` | STATE_UART_MODE — sub-menu: Wybierz config / Auto / Tylko terminal |
| `screens/uart_term.cpp/h` | Terminal UART — klawiatura, FN keys, send/receive, local echo, ANSI filter |
| `screens/maint_menu.cpp/h` | Parser `!SENSORS:...!`, `maint_ReadSensorSerial()`, overlay STATE_MAINT_MENU |
| `screens/sensor_view.cpp/h` | STATE_SENSOR_VIEW — wyświetlanie wartości czujników |
| `screens/sensor_chart.cpp/h` | STATE_SENSOR_CHART — wykres liniowy, checkbox wyboru sensorów |
| `screens/cmd_grid.cpp/h` | Parser `!CMD:...!`, rysowanie siatki komend |
| `device/device_config.cpp/h` | SD card (HSPI/SPI3), JSON, `DeviceConfig`, `DeviceIdentity`, `SensorConfig` |
| `screens/device_detect.cpp/h` | STATE_DETECT — protokół HELLO/HELLO SIR/ident |
| `screens/select_list.cpp/h` | STATE_SELECT_LIST — lista urządzeń z SD |
| `screens/menu.cpp/h` | STATE_MENU — główne menu |
| `screens/splash.cpp/h` | STATE_SPLASH — ekran startowy z progress bar |

## Wzorzec każdego stanu

```cpp
// screens/xxx.h
void xxx_Setup();
void xxx_Loop();

// screens/xxx.cpp — static zmienne stanu, static draw(), public Setup/Loop
```

Aby dodać nowy stan:
1. Nowy `.h/.cpp` w `screens/` z `xxx_Setup/Loop`
2. Wpis w `AppState` enum w `app_state.h`
3. Case w `transitionTo()` i `loop()` w `main.cpp`
4. Include w `main.cpp`

## Środowiska PlatformIO

| Env | Opis |
|-----|------|
| `m5stack-cardputer` | Główna aplikacja |
| `test-keyboard` | Diagnostyka klawiatury (src: `test_kb/test_keyboard.cpp`) |
| `test-sd` | Test konfiguracji karty SD — 3 warianty SPIClass (src: `test_sd/test_sd.cpp`) |

## Nawigacja — klucze

Standardowe dla wszystkich ekranów listy/menu:

| Klawisz | Akcja |
|---------|-------|
| `;` | Góra |
| `.` | Dół |
| `ENTER` | Wybierz |
| `FN+DEL` | Wstecz / wyjdź |

`j`/`k` jako alternatywa tam, gdzie jest to historycznie, ale `;`/`.` zawsze działa.

## Ważne pułapki

**SD card vs LGFX (SPI2):**
ILI9341 używa SPI2_HOST (FSPI). Arduino `SPI` na ESP32-S3 mapuje domyślnie na SPI2 (FSPI) — ten sam host.
`device_config.cpp` używa `SPIClass(HSPI)` = SPI3, żeby uniknąć konfliktu. Nie zmieniać na `SPI.begin()`.

**Serial read w stanach sensor:**
Gdy `app == STATE_SENSOR_VIEW` lub `STATE_SENSOR_CHART`, `uartTerm_Loop()` NIE jest wywoływane.
Dlatego serial musi być czytany przez `maint_ReadSensorSerial()` (w `screens/maint_menu.cpp`).

**ILI9341 driver (`display/ili9341.h`):**
`spi_3wire=true` (half-duplex, pin_miso=-1), rotation=7, SPI2_HOST.
Nie zmieniać bez testów na sprzęcie.

**Sprite terminal:**
`terminal` (M5Canvas) jest w `display/display_ext.cpp`, widoczny globalnie przez `extern M5Canvas terminal`.
Rozmiar zmienia się dynamicznie gdy `cmdGridVisible`: TERM_H_FULL (212px) ↔ TERM_H_GRID (178px).

**`isChange()` — clear-on-read:**
`M5Cardputer.Keyboard.isChange()` kasuje flagę przy odczycie. Global handler w `loop()` czyta `keysState()` BEZ `isChange()` żeby nie zablokować ekranowych `_Loop()`.

## Protokół UART (device → Cardputer)

```
HELLO SIR                    → start detekcji (STATE_DETECT)
DeviceName;UID;TypeID;maint  → identyfikacja urządzenia
!CMD:func1,func2,...!        → dynamiczne komendy (CTRL+A,S,D,F,G,H,J,K,L)
!SENSORS:name=val,...!       → dane czujników
```

## FN key shortcuts (w terminalu)

| Klawisz | Akcja |
|---------|-------|
| FN+DEL | Wyjście do głównego menu |
| FN+1 | Cykl port: USB → UART → BOTH |
| FN+2 | Cykl baud rate |
| FN+3 | Toggle local echo |
| FN+4 | Toggle ANSI filter |
| FN+5 | Clear terminal |
| FN+6 | Toggle CRLF |
| FN+7 | Toggle font 1×/2× |
| FN+G | Toggle cmd grid overlay |
| FN+H | Wejście do maintenance menu |
| CTRL+A..Z | Wyślij raw control character (0x01–0x1A) |
