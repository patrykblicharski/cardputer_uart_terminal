#include "menu.h"
#include "app_state.h"
#include "display/display_ext.h"
#include "display/display_int.h"
#include "config.h"
#include <M5Cardputer.h>

static int  s_sel   = 0;
static bool s_dirty = true;

// ─── External display ─────────────────────────────────────────────────────────

static void extDraw() {
    extDisplay.fillScreen(TFT_BLACK);

    // Header bar
    extDisplay.fillRect(0, 0, EXT_W, 32, TFT_DARKGREY);
    extDisplay.setTextSize(2);
    int tw = extDisplay.textWidth("CARDPUTER ADV");
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor((EXT_W - tw) / 2, 8);
    extDisplay.print("CARDPUTER ADV");

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(8, 40);
    extDisplay.print("Select application:");

    for (int i = 0; i < kMenuItemCount; i++) {
        int     iy    = 54 + i * 42;
        bool    iSel  = (i == s_sel);
        bool    avail = kMenuItems[i].available;

        uint16_t rowBg  = iSel ? C_DIM    : TFT_BLACK;
        uint16_t border = iSel ? C_GREEN  : C_BORDER;
        uint16_t fgName = iSel ? C_WHITE  : (avail ? C_GREEN : C_GRAY);
        uint16_t fgDesc = iSel ? C_GREEN  : C_GRAY;

        extDisplay.fillRect(4, iy,      EXT_W - 8, 38, rowBg);
        extDisplay.drawRect(4, iy,      EXT_W - 8, 38, border);

        extDisplay.setTextSize(2);
        extDisplay.setTextColor(fgName, rowBg);
        extDisplay.setCursor(12, iy + 4);
        extDisplay.print(kMenuItems[i].name);

        extDisplay.setTextSize(1);
        extDisplay.setTextColor(fgDesc, rowBg);
        extDisplay.setCursor(12, iy + 24);
        extDisplay.print(kMenuItems[i].desc);
    }

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(4, EXT_H - 11);
    extDisplay.print(";=Up  .=Down  ENTER=Select");
}

// ─── Internal display ─────────────────────────────────────────────────────────

static void intDraw() {
    intSprite.fillScreen(TFT_BLACK);

    intSprite.fillRect(0, 0, INT_W, 18, TFT_DARKGREY);
    intSprite.setTextSize(1);
    intSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    intSprite.setCursor(4, 5);
    intSprite.print("CARDPUTER ADV \x14 MENU");

    intSprite.setTextSize(2);
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    int tw = intSprite.textWidth(kMenuItems[s_sel].name);
    intSprite.setCursor((INT_W - tw) / 2, 52);
    intSprite.print(kMenuItems[s_sel].name);

    intSprite.setTextSize(1);
    intSprite.setTextColor(C_GRAY, TFT_BLACK);
    intSprite.setCursor(4, INT_H - 12);
    intSprite.printf(";=Up  .=Down  ENTER=Open   [%d/%d]", s_sel + 1, kMenuItemCount);

    intSprite.pushSprite(0, 0);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void menu_Setup() {
    s_sel   = 0;
    s_dirty = true;
    extDraw();
    intDraw();
}

void menu_Loop() {
    if (s_dirty || g_session.forceRedraw) { extDraw(); intDraw(); s_dirty = false; g_session.forceRedraw = false; }

    if (!M5Cardputer.Keyboard.isChange()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    bool redraw = false;
    for (auto c : st.word) {
        if      (c == ';') { s_sel = (s_sel - 1 + kMenuItemCount) % kMenuItemCount; redraw = true; }
        else if (c == '.') { s_sel = (s_sel + 1) % kMenuItemCount;                  redraw = true; }
    }
    if (redraw) { extDraw(); intDraw(); }

    if (st.enter && kMenuItems[s_sel].available) {
        switch (s_sel) {
            case 0: transitionTo(STATE_UART_MODE);    break;
            case 1: transitionTo(STATE_APP_I2C);      break;
            case 2: transitionTo(STATE_APP_LORA);     break;
            case 3: transitionTo(STATE_APP_BLE_UART); break;
        }
    }
}
