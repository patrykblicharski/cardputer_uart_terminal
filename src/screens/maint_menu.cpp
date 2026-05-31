#include "maint_menu.h"
#include "app_state.h"
#include "display/display_ext.h"
#include "display/display_int.h"
#include "config.h"
#include <M5Cardputer.h>

// ─── Sensor line parser ────────────────────────────────────────────────────────

bool maint_ParseSensors(const String& line) {
    if (!line.startsWith("!SENSORS:") || !line.endsWith("!")) return false;
    String payload = line.substring(9, line.length() - 1);

    SensorSample& s = g_session.lastSensors;
    s.count = 0;
    s.valid = false;

    int start = 0;
    while (s.count < MAX_SENSORS) {
        int comma = payload.indexOf(',', start);
        String token = (comma >= 0) ? payload.substring(start, comma)
                                    : payload.substring(start);
        token.trim();
        int eq = token.indexOf('=');
        if (eq > 0) {
            s.names[s.count]  = token.substring(0, eq);
            s.values[s.count] = token.substring(eq + 1).toFloat();
            s.count++;
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    s.valid = (s.count > 0);
    return s.valid;
}

// ─── Serial reader for sensor states ─────────────────────────────────────────
// uart_term is NOT running during STATE_SENSOR_VIEW / STATE_SENSOR_CHART,
// so those states must drain Serial1 themselves via this function.

static String s_sensorLineBuf;

void maint_ReadSensorSerial() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            s_sensorLineBuf.trim();
            maint_ParseSensors(s_sensorLineBuf);
            s_sensorLineBuf = "";
        } else if (c != '\r') {
            s_sensorLineBuf += c;
        }
    }
}

// ─── Maintenance overlay menu ─────────────────────────────────────────────────

static const char* MAINT_ITEMS[] = {
    "Wyswietl czujniki",
    "Wykres czujnikow",
    "Wyslij komende",
    "Powrot"
};
static constexpr int MAINT_ITEM_COUNT = 4;
static int  s_mSel   = 0;
static bool s_mDirty = true;

static void drawOverlay() {
    constexpr int PW = 130;
    extDisplay.fillRect(0, TOP_H + 1, PW, EXT_H - TOP_H - 1, TFT_DARKGREY);
    extDisplay.drawFastVLine(PW, TOP_H + 1, EXT_H - TOP_H - 1, TFT_WHITE);

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_AMBER, TFT_DARKGREY);
    extDisplay.setCursor(4, TOP_H + 6);
    extDisplay.print("MAINTENANCE");
    extDisplay.drawFastHLine(0, TOP_H + 17, PW, TFT_BLACK);

    int y0 = TOP_H + 22;
    int dy = 20;
    for (int i = 0; i < MAINT_ITEM_COUNT; i++) {
        bool sel = (i == s_mSel);
        extDisplay.fillRect(0, y0 + i * dy, PW, dy, sel ? C_DIM : TFT_DARKGREY);
        extDisplay.setTextColor(sel ? C_GREEN : TFT_WHITE, sel ? C_DIM : TFT_DARKGREY);
        extDisplay.setCursor(6, y0 + i * dy + 5);
        if (sel) extDisplay.print((char)0xBB);
        else     extDisplay.print(' ');
        extDisplay.print(' ');
        extDisplay.print(MAINT_ITEMS[i]);
    }

    extDisplay.setTextColor(C_GRAY, TFT_DARKGREY);
    extDisplay.setCursor(4, EXT_H - 20);
    extDisplay.print(";/.=nav  ENTER=ok  FN+DEL=wyjdz");

    s_mDirty = false;
}

void maint_Setup() {
    s_mSel   = 0;
    s_mDirty = true;
    drawOverlay();
}

void maint_Loop() {
    if (s_mDirty || g_session.forceRedraw) { s_mDirty = false; g_session.forceRedraw = false; drawOverlay(); }

    if (!M5Cardputer.Keyboard.isChange()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.fn && st.del) { transitionTo(STATE_TERMINAL); return; }
    if (st.fn) return;

    bool moved = false;
    for (auto c : st.word) {
        if (c == ';' || c == 'k' || c == 'K') {
            s_mSel = (s_mSel - 1 + MAINT_ITEM_COUNT) % MAINT_ITEM_COUNT;
            moved = true;
        }
        if (c == '.' || c == 'j' || c == 'J') {
            s_mSel = (s_mSel + 1) % MAINT_ITEM_COUNT;
            moved = true;
        }
    }
    if (moved) { drawOverlay(); return; }

    if (st.enter) {
        switch (s_mSel) {
            case 0: transitionTo(STATE_SENSOR_VIEW);  break;
            case 1: transitionTo(STATE_SENSOR_CHART); break;
            case 2: transitionTo(STATE_TERMINAL);     break;
            case 3: transitionTo(STATE_TERMINAL);     break;
        }
    }
}
