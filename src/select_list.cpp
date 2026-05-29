#include "select_list.h"
#include "app_state.h"
#include "device_config.h"
#include "display_ext.h"
#include "display_int.h"
#include "config.h"
#include <M5Cardputer.h>

static int    s_sel   = 0;
static int    s_count = 0;
static bool   s_dirty = true;
static bool   s_sdOk  = false;
static String s_error;

static void drawList() {
    extDisplay.fillScreen(TFT_BLACK);
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    extDisplay.print("WYBIERZ URZADZENIE");

    if (!s_sdOk) {
        extDisplay.setTextColor(TFT_RED, TFT_BLACK);
        extDisplay.setCursor(3, 40);
        extDisplay.print("Blad SD: " + s_error);
        extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
        extDisplay.setCursor(3, INPUT_Y + 2);
        extDisplay.print("CTRL+[ = wstecz");
        return;
    }
    if (s_count == 0) {
        extDisplay.setTextColor(C_GRAY, TFT_BLACK);
        extDisplay.setCursor(3, 40);
        extDisplay.print("Brak plikow w /devices/");
        extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
        extDisplay.setCursor(3, INPUT_Y + 2);
        extDisplay.print("CTRL+[ = wstecz");
        return;
    }

    int y0 = TOP_H + 8;
    int dy = 20;
    int maxVis = (INPUT_Y - y0) / dy;
    int offset = 0;
    if (s_sel >= maxVis) offset = s_sel - maxVis + 1;

    for (int i = 0; i < s_count && (i - offset) < maxVis; i++) {
        if (i < offset) continue;
        int row = i - offset;
        bool sel = (i == s_sel);
        extDisplay.fillRect(2, y0 + row * dy, EXT_W - 4, dy - 2,
                            sel ? C_DIM : TFT_BLACK);
        extDisplay.setTextColor(sel ? C_GREEN : TFT_WHITE, sel ? C_DIM : TFT_BLACK);
        extDisplay.setCursor(8, y0 + row * dy + 4);
        if (sel) extDisplay.print("> ");
        else     extDisplay.print("  ");
        extDisplay.print(deviceConfig_LabelAt(i));
    }

    extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 2);
    extDisplay.print("j/k=nav  ENTER=select  CTRL+[=wstecz");

    s_dirty = false;
}

void selectList_Setup() {
    s_sel   = 0;
    s_dirty = true;
    s_error = "";

    s_sdOk  = deviceConfig_Begin(s_error);
    s_count = (int)deviceConfig_Count();

    intSprite.fillScreen(TFT_BLACK);
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    intSprite.setTextSize(1);
    intSprite.setCursor(4, 4);
    intSprite.print("WYBOR URZADZENIA");
    intSprite.pushSprite(0, 0);

    drawList();
}

void selectList_Loop() {
    if (s_dirty) drawList();

    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.ctrl) {
        for (auto c : st.word) {
            if (c == '[') { transitionTo(STATE_MENU); return; }
        }
        return;
    }
    if (st.fn) return;

    for (auto c : st.word) {
        if (c == 'j' || c == 'J') {
            if (s_count > 0) s_sel = (s_sel + 1) % s_count;
            s_dirty = true;
        } else if (c == 'k' || c == 'K') {
            if (s_count > 0) s_sel = (s_sel - 1 + s_count) % s_count;
            s_dirty = true;
        }
    }
    if (st.enter && s_sdOk && s_count > 0) {
        String err;
        if (deviceConfig_LoadAtIndex(s_sel, g_session.config, err)) {
            g_session.configLoaded = true;
            transitionTo(STATE_TERMINAL);
        } else {
            s_error = err;
            s_dirty = true;
        }
    }
}
