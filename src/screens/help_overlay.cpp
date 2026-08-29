#include "help_overlay.h"
#include "../display/display_ext.h"
#include "../display/display_int.h"
#include "../config.h"
#include "../app_state.h"
#include <M5Cardputer.h>

static bool s_active = false;
static int  s_hy     = 0;

// ─── Draw helpers ─────────────────────────────────────────────────────────────

static void helpRow(const char* key, const char* desc) {
    extDisplay.setTextColor(C_AMBER, TFT_BLACK);
    extDisplay.setCursor(8,  s_hy);
    extDisplay.print(key);
    extDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    extDisplay.setCursor(96, s_hy);
    extDisplay.print(desc);
    s_hy += 13;
}

static void helpSep() {
    extDisplay.drawFastHLine(4, s_hy + 2, EXT_W - 8, 0x2104);
    s_hy += 7;
}

// ─── Content per state ────────────────────────────────────────────────────────

static void drawContent(AppState state) {
    s_hy = 28;
    extDisplay.setTextSize(1);

    switch (state) {

    case STATE_MENU:
        helpRow(";  /  .",         "Nawigacja gora/dol");
        helpRow("ENTER",           "Uruchom aplikacje");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_DETECT:
        helpRow("FN+DEL",          "Menu glowne");
        helpRow("ENTER",           "Kontynuuj po wykryciu");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_SELECT_LIST:
        helpRow("FN+DEL",          "Menu glowne");
        helpRow("j  /  k",         "Nawigacja gora/dol");
        helpRow("ENTER",           "Wczytaj konfiguracje");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_TERMINAL:
        helpRow("FN+DEL",          "Menu glowne");
        helpRow("FN+1",            "Port: USB / UART / BOTH");
        helpRow("FN+2",            "Predkosc (baud)");
        helpRow("FN+3",            "Echo lokalne wl/wyl");
        helpRow("FN+4",            "Filtr ANSI wl/wyl");
        helpRow("FN+5",            "Wyczysc terminal");
        helpRow("FN+6",            "Tryb CRLF wl/wyl");
        helpRow("FN+7",            "Czcionka 1x / 2x");
        helpRow("FN+G",            "Siatka komend");
        helpRow("FN+H",            "Menu maintenance");
        helpRow("CTRL+A..Z",       "Wyslij znak kontrolny");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_MAINT_MENU:
        helpRow("FN+DEL",          "Powrot do terminala");
        helpRow("j / k  lub FN+4/6", "Nawigacja");
        helpRow("ENTER",           "Wybierz");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_SENSOR_VIEW:
        helpRow("FN+DEL",          "Powrot do maintenance");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_SENSOR_CHART:
        helpRow("FN+DEL",          "Powrot do maintenance");
        helpRow("j  /  k",         "Wybierz czujnik");
        helpRow("SPACJA",          "Wlacz / wylacz czujnik");
        helpRow("ENTER",           "Start / Stop wykresu");
        helpRow("FN+5",            "Wyczysc dane");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_APP_I2C:
        helpRow("FN+DEL",          "Menu glowne");
        helpRow("FN+1",            "Zmien pare pinow SDA/SCL");
        helpRow("ENTER",           "Skanuj / Pokaz szczegoly");
        helpRow(";  /  .",         "Nawigacja listy");
        helpRow("[  /  ]",         "Zmien wartosc parametru");
        helpRow("FN+R",            "Ponow skan");
        helpRow("DEL",             "Powrot do listy");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_APP_LORA:
        helpRow("FN+DEL",          "Menu glowne");
        helpRow("FN+1",            "Zmien cel wiadomosci");
        helpRow("FN+2",            "Zadaj REPORT od celu");
        helpRow("FN+3",            "Przelacz MAINT celu");
        helpRow("FN+4",            "Pobierz STATUS Heltec");
        helpRow("FN+5",            "Przelacz MAINT lokalny");
        helpRow("FN+6",            "Zmien SF (7..12)");
        helpRow("FN+7",            "Zmien czestotliwosc");
        helpRow("ENTER",           "Wyslij wiadomosc");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    case STATE_APP_BLE_UART:
        helpRow("FN+DEL",          "Menu glowne (rozlacz)");
        helpRow("ENTER",           "Skanuj / Polacz");
        helpRow(";  /  .",         "Nawigacja listy");
        helpRow("FN+1",            "Filtr NUS / Rozlacz");
        helpRow("FN+2",            "Rozlacz + rescan");
        helpRow("FN+3",            "Echo lokalne wl/wyl");
        helpRow("FN+4",            "Filtr ANSI wl/wyl");
        helpRow("FN+5",            "Wyczysc terminal");
        helpRow("FN+6",            "Tryb CRLF wl/wyl");
        helpRow("FN+7",            "Czcionka 1x / 2x");
        helpRow("FN+R",            "Ponow skan (lista)");
        helpSep();
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;

    default:
        helpRow("FN+DEL",          "Menu glowne");
        helpRow("CTRL+=",          "Ta pomoc (zamknij)");
        break;
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void helpOverlay_Show(AppState state) {
    s_active = true;

    extDisplay.fillScreen(TFT_BLACK);

    // Header bar
    extDisplay.fillRect(0, 0, EXT_W, 22, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(4, 7);

    const char* title = nullptr;
    switch (state) {
        case STATE_MENU:         title = "MENU";              break;
        case STATE_DETECT:       title = "WYKRYWANIE";        break;
        case STATE_SELECT_LIST:  title = "WYBOR URZADZENIA";  break;
        case STATE_TERMINAL:     title = "UART TERMINAL";     break;
        case STATE_MAINT_MENU:   title = "MAINTENANCE";       break;
        case STATE_SENSOR_VIEW:  title = "CZUJNIKI";          break;
        case STATE_SENSOR_CHART: title = "WYKRES";            break;
        case STATE_APP_I2C:      title = "I2C SCANNER";       break;
        case STATE_APP_LORA:     title = "LORA CHANNEL";      break;
        case STATE_APP_BLE_UART: title = "BLE UART";          break;
        default:                 title = "OGOLNE";            break;
    }
    extDisplay.print("SKROTY  \xBB  ");
    extDisplay.print(title);

    extDisplay.setTextColor(C_DIM, TFT_DARKGREY);
    const char* hint = "dowolny klawisz = zamknij";
    int tw = extDisplay.textWidth(hint);
    extDisplay.setCursor(EXT_W - tw - 4, 7);
    extDisplay.print(hint);

    drawContent(state);

    // Internal display hint
    intSprite.fillScreen(TFT_BLACK);
    intSprite.fillRect(0, 0, INT_W, 18, TFT_DARKGREY);
    intSprite.setTextSize(1);
    intSprite.setTextColor(C_GREEN, TFT_DARKGREY);
    intSprite.setCursor(4, 5);
    intSprite.print("SKROTY  \xBB  ");
    intSprite.print(title);
    intSprite.setTextSize(2);
    intSprite.setTextColor(C_DIM, TFT_BLACK);
    int itw = intSprite.textWidth("HELP");
    intSprite.setCursor((INT_W - itw) / 2, 52);
    intSprite.print("HELP");
    intSprite.setTextSize(1);
    intSprite.setTextColor(C_GRAY, TFT_BLACK);
    intSprite.setCursor(4, INT_H - 12);
    intSprite.print("CTRL+= zamknij");
    intSprite.pushSprite(0, 0);
}

bool helpOverlay_IsActive() {
    return s_active;
}

void helpOverlay_HandleKey() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    s_active = false;
    g_session.forceRedraw = true;
}
