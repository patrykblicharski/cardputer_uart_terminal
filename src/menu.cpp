#include "menu.h"
#include "app_state.h"
#include "display_ext.h"
#include "display_int.h"
#include "config.h"
#include <M5Cardputer.h>

static const char* ITEMS[] = { "Wykryj host", "Wybierz z listy", "Uruchom terminal" };
static constexpr int ITEM_COUNT = 3;
static int s_sel = 0;
static bool s_dirty = true;

static void draw() {
    extDisplay.fillScreen(TFT_BLACK);

    // Topbar
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    extDisplay.print("CARDPUTER UART TOOL");

    // Menu items
    int y0 = 50;
    int dy = 32;
    for (int i = 0; i < ITEM_COUNT; i++) {
        bool sel = (i == s_sel);
        extDisplay.fillRect(30, y0 + i * dy - 4, EXT_W - 60, 24, sel ? C_DIM : TFT_BLACK);
        extDisplay.setTextSize(1);
        extDisplay.setTextColor(sel ? C_GREEN : C_GRAY, sel ? C_DIM : TFT_BLACK);
        extDisplay.setCursor(44, y0 + i * dy + 4);
        if (sel) extDisplay.print((char)0xBB);  // »
        else     extDisplay.print(' ');
        extDisplay.print(' ');
        extDisplay.print(ITEMS[i]);

        // Digit hint
        extDisplay.setTextColor(C_AMBER, sel ? C_DIM : TFT_BLACK);
        char digit[3] = { '[', (char)('1' + i), ']' };
        extDisplay.setCursor(32, y0 + i * dy + 4);
        extDisplay.print(digit);
    }

    // Hint bar at bottom
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 2);
    extDisplay.print("j/k=nav  ENTER/1-3=select");

    // Internal display
    intSprite.fillScreen(TFT_BLACK);
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    intSprite.setTextSize(1);
    intSprite.setCursor(4, 4);
    intSprite.print("MAIN MENU");
    intSprite.setCursor(4, 20);
    intSprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    intSprite.print("j/k + ENTER");
    intSprite.pushSprite(0, 0);

    s_dirty = false;
}

void menu_Setup() {
    s_sel   = 0;
    s_dirty = true;
    draw();
}

void menu_Loop() {
    if (s_dirty) draw();

    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    for (auto c : st.word) {
        if (c == 'j' || c == 'J') {
            s_sel = (s_sel + 1) % ITEM_COUNT;
            s_dirty = true;
        } else if (c == 'k' || c == 'K') {
            s_sel = (s_sel - 1 + ITEM_COUNT) % ITEM_COUNT;
            s_dirty = true;
        } else if (c >= '1' && c <= '3') {
            s_sel = c - '1';
            s_dirty = true;
        }
    }
    if (st.fn) {
        for (auto c : st.word) {
            if (c == '4') { s_sel = (s_sel - 1 + ITEM_COUNT) % ITEM_COUNT; s_dirty = true; }
            if (c == '6') { s_sel = (s_sel + 1) % ITEM_COUNT;              s_dirty = true; }
        }
        return;
    }
    if (st.enter) {
        switch (s_sel) {
            case 0: transitionTo(STATE_DETECT);      break;
            case 1: transitionTo(STATE_SELECT_LIST); break;
            case 2: transitionTo(STATE_TERMINAL);    break;
        }
    }
}
