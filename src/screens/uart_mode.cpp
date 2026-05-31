#include "uart_mode.h"
#include "app_state.h"
#include "display/display_ext.h"
#include "display/display_int.h"
#include "config.h"
#include <M5Cardputer.h>

struct UartModeItem {
    const char* name;
    const char* desc;
    AppState    target;
};

static constexpr UartModeItem kItems[] = {
    { "Wybierz config",   "Zaladuj plik konfiguracyjny z karty SD", STATE_SELECT_LIST },
    { "Auto wykrywanie",  "Urzadzenie identyfikuje sie przez HELLO", STATE_DETECT      },
    { "Tylko terminal",   "Bezposredni terminal bez konfiguracji",   STATE_TERMINAL    },
};
static constexpr int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));

static int s_sel = 0;

static void extDraw() {
    extDisplay.fillScreen(TFT_BLACK);

    extDisplay.fillRect(0, 0, EXT_W, 32, TFT_DARKGREY);
    extDisplay.setTextSize(2);
    int tw = extDisplay.textWidth("UART Terminal");
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor((EXT_W - tw) / 2, 8);
    extDisplay.print("UART Terminal");

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(8, 40);
    extDisplay.print("Wybierz tryb uruchomienia:");

    int y0 = 56;
    int dy = 52;
    for (int i = 0; i < kItemCount; i++) {
        int     iy   = y0 + i * dy;
        bool    sel  = (i == s_sel);

        uint16_t rowBg  = sel ? C_DIM   : TFT_BLACK;
        uint16_t border = sel ? C_GREEN : C_BORDER;
        uint16_t fgName = sel ? C_WHITE : C_GREEN;
        uint16_t fgDesc = sel ? C_GREEN : C_GRAY;

        extDisplay.fillRect(4, iy,      EXT_W - 8, 46, rowBg);
        extDisplay.drawRect(4, iy,      EXT_W - 8, 46, border);

        extDisplay.setTextSize(2);
        extDisplay.setTextColor(fgName, rowBg);
        extDisplay.setCursor(12, iy + 7);
        extDisplay.print(kItems[i].name);

        extDisplay.setTextSize(1);
        extDisplay.setTextColor(fgDesc, rowBg);
        extDisplay.setCursor(12, iy + 32);
        extDisplay.print(kItems[i].desc);
    }

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(4, EXT_H - 11);
    extDisplay.print(";=Gora  .=Dol  ENTER=Wybierz  FN+DEL=Wstecz");
}

static void intDraw() {
    intSprite.fillScreen(TFT_BLACK);
    intSprite.fillRect(0, 0, INT_W, 18, TFT_DARKGREY);
    intSprite.setTextSize(1);
    intSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    intSprite.setCursor(4, 5);
    intSprite.print("UART TERMINAL \x14 TRYB");

    intSprite.setTextSize(2);
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    int tw = intSprite.textWidth(kItems[s_sel].name);
    intSprite.setCursor((INT_W - tw) / 2, 52);
    intSprite.print(kItems[s_sel].name);

    intSprite.setTextSize(1);
    intSprite.setTextColor(C_GRAY, TFT_BLACK);
    intSprite.setCursor(4, INT_H - 12);
    intSprite.printf(";=Gora  .=Dol  ENTER=OK   [%d/%d]", s_sel + 1, kItemCount);

    intSprite.pushSprite(0, 0);
}

void uartMode_Setup() {
    s_sel = 0;
    extDraw();
    intDraw();
}

void uartMode_Loop() {
    if (g_session.forceRedraw) { g_session.forceRedraw = false; extDraw(); intDraw(); }

    if (!M5Cardputer.Keyboard.isChange()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.fn && st.del) { transitionTo(STATE_MENU); return; }
    if (st.fn) return;

    bool moved = false;
    for (auto c : st.word) {
        if (c == ';') { s_sel = (s_sel - 1 + kItemCount) % kItemCount; moved = true; }
        if (c == '.') { s_sel = (s_sel + 1) % kItemCount;              moved = true; }
    }
    if (moved) { extDraw(); intDraw(); return; }

    if (st.enter) {
        if (kItems[s_sel].target == STATE_TERMINAL) {
            // Reset session — wchodzimy bez zadnej konfiguracji
            g_session.configLoaded     = false;
            g_session.deviceIdentified = false;
            g_session.dynamicCmdCount  = 0;
        }
        transitionTo(kItems[s_sel].target);
    }
}
