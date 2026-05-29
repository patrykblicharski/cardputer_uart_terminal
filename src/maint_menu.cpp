#include "maint_menu.h"
#include "app_state.h"
#include "display_ext.h"
#include "display_int.h"
#include "config.h"
#include <M5Cardputer.h>

// ─── Sensor parser ────────────────────────────────────────────────────────────

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

// ─── Maint overlay menu ───────────────────────────────────────────────────────

static const char* MAINT_ITEMS[] = {
    "Wyswietl czujniki",
    "Wykres czujnikow",
    "Wyslij komende",
    "Powrot"
};
static constexpr int MAINT_ITEM_COUNT = 4;
static int  s_mSel   = 0;
static bool s_mDirty = true;

static void drawMaintOverlay() {
    constexpr int PW = 130; // panel width
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

    extDisplay.setTextColor(TFT_DARKGREY, TFT_DARKGREY);
    extDisplay.setCursor(4, EXT_H - 20);
    extDisplay.print("ESC=zamknij");

    s_mDirty = false;
}

void maint_Setup() {
    s_mSel   = 0;
    s_mDirty = true;
    drawMaintOverlay();
}

void maint_Loop() {
    if (s_mDirty) drawMaintOverlay();

    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.fn) {
        for (auto c : st.word) {
            if (c == '4') { s_mSel = (s_mSel - 1 + MAINT_ITEM_COUNT) % MAINT_ITEM_COUNT; s_mDirty = true; }
            if (c == '6') { s_mSel = (s_mSel + 1) % MAINT_ITEM_COUNT;                    s_mDirty = true; }
        }
        return;
    }
    for (auto c : st.word) {
        if (c == 'j' || c == 'J') { s_mSel = (s_mSel + 1) % MAINT_ITEM_COUNT; s_mDirty = true; }
        if (c == 'k' || c == 'K') { s_mSel = (s_mSel - 1 + MAINT_ITEM_COUNT) % MAINT_ITEM_COUNT; s_mDirty = true; }
    }
    if (st.enter) {
        switch (s_mSel) {
            case 0: transitionTo(STATE_SENSOR_VIEW);  break;
            case 1: transitionTo(STATE_SENSOR_CHART); break;
            case 2: transitionTo(STATE_TERMINAL);     break; // powrot do terminala
            case 3: transitionTo(STATE_TERMINAL);     break;
        }
    }
    // ESC via CTRL+[
    if (st.ctrl) {
        for (auto c : st.word) {
            if (c == '[') { transitionTo(STATE_TERMINAL); return; }
        }
    }
}

// ─── Sensor view ─────────────────────────────────────────────────────────────

static unsigned long s_svLastPoll = 0;
static constexpr unsigned long SENSOR_POLL_MS = 500;

static void drawSensorView() {
    extDisplay.fillScreen(TFT_BLACK);
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    extDisplay.print("CZUJNIKI  [auto-refresh 500ms]");

    extDisplay.drawFastHLine(0, TOP_H, EXT_W, TFT_DARKGREY);

    SensorSample& s = g_session.lastSensors;
    int y = TOP_H + 10;
    if (!s.valid) {
        extDisplay.setTextColor(C_GRAY, TFT_BLACK);
        extDisplay.setCursor(3, y);
        extDisplay.print("Czekam na dane...");
    } else {
        for (int i = 0; i < s.count; i++) {
            String unit;
            // Find unit from config
            if (g_session.configLoaded) {
                for (auto& sc : g_session.config.sensors) {
                    if (sc.name == s.names[i]) { unit = sc.unit; break; }
                }
            }
            extDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
            extDisplay.setCursor(3, y);
            char buf[64];
            snprintf(buf, sizeof(buf), "%-16s: %8.2f %s",
                     s.names[i].c_str(), s.values[i], unit.c_str());
            extDisplay.print(buf);
            y += 14;
        }
    }

    extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 2);
    extDisplay.print("ESC/CTRL+[ = powrot");
}

void sensorView_Setup() {
    s_svLastPoll = 0;
    extDisplay.fillScreen(TFT_BLACK);
    drawSensorView();

    intSprite.fillScreen(TFT_BLACK);
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    intSprite.setTextSize(1);
    intSprite.setCursor(4, 4);
    intSprite.print("SENSOR VIEW");
    intSprite.pushSprite(0, 0);
}

void sensorView_Loop() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) goto poll;
    {
        auto& st = M5Cardputer.Keyboard.keysState();
        if (st.ctrl) {
            for (auto c : st.word) {
                if (c == '[') { transitionTo(STATE_MAINT_MENU); return; }
            }
        }
    }
poll:
    unsigned long now = millis();
    if (now - s_svLastPoll >= SENSOR_POLL_MS) {
        s_svLastPoll = now;
        Serial1.print("?SENSORS\r\n");
        // Response handled in uart_term readSerial, updates g_session.lastSensors
    }
    // Check if new data arrived
    if (g_session.lastSensors.valid) {
        drawSensorView();
    }
}

// ─── Sensor chart ─────────────────────────────────────────────────────────────

static unsigned long s_scLastPoll = 0;
static bool          s_scDirtyLabels = true;
static int           s_scCursorSel   = 0;  // for checkbox navigation

static void pushChartSample() {
    SensorSample& s = g_session.lastSensors;
    if (!s.valid) return;
    int idx = g_session.chartHead;
    // Match sensor names to config order
    for (int ci = 0; ci < s.count && ci < MAX_SENSORS; ci++) {
        float v = s.values[ci];
        g_session.chartBuf[ci][idx] = v;
        if (v < g_session.chartMin[ci]) g_session.chartMin[ci] = v;
        if (v > g_session.chartMax[ci]) g_session.chartMax[ci] = v;
    }
    g_session.chartHead = (idx + 1) % CHART_SAMPLES;
    if (g_session.chartFilled < CHART_SAMPLES) g_session.chartFilled++;
}

static void drawChart() {
    int chartX = CHART_LEFT_W + 2;
    int chartW = EXT_W - chartX;
    int chartY = TOP_H + 1;
    int chartH = INPUT_Y - chartY - 4;

    // Clear chart area
    extDisplay.fillRect(chartX, chartY, chartW, chartH, TFT_BLACK);

    int cnt = g_session.chartFilled;
    if (cnt < 2) return;

    int numSensors = g_session.lastSensors.count;
    if (numSensors > 4) numSensors = 4;

    for (int ci = 0; ci < numSensors; ci++) {
        if (!g_session.sensorChecked[ci]) continue;
        float mn = g_session.chartMin[ci];
        float mx = g_session.chartMax[ci];
        if (mx - mn < 0.001f) { mn -= 1.0f; mx += 1.0f; }

        uint16_t col = C_CHART[ci % 4];
        int prev_px = -1, prev_py = -1;

        for (int i = 0; i < cnt; i++) {
            // Index in ring buffer (oldest sample first)
            int bufIdx = (g_session.chartHead - cnt + i + CHART_SAMPLES) % CHART_SAMPLES;
            float v = g_session.chartBuf[ci][bufIdx];
            int px = chartX + (int)((long)i * (chartW - 1) / (cnt - 1));
            int py = chartY + chartH - 1 - (int)((v - mn) / (mx - mn) * (chartH - 1));
            py = max(chartY, min(chartY + chartH - 1, py));
            if (prev_px >= 0) extDisplay.drawLine(prev_px, prev_py, px, py, col);
            prev_px = px; prev_py = py;
        }
    }
}

static void drawChartLabels() {
    // Left panel: checkboxes
    extDisplay.fillRect(0, TOP_H + 1, CHART_LEFT_W, INPUT_Y - TOP_H - 1, TFT_DARKGREY);
    extDisplay.drawFastVLine(CHART_LEFT_W, TOP_H + 1, INPUT_Y - TOP_H - 1, TFT_WHITE);

    extDisplay.setTextSize(1);
    int y = TOP_H + 5;
    int numSensors = g_session.lastSensors.count;
    if (numSensors > MAX_SENSORS) numSensors = MAX_SENSORS;

    for (int i = 0; i < numSensors; i++) {
        bool sel = (i == s_scCursorSel);
        bool chk = g_session.sensorChecked[i];
        uint16_t fg = (i < 4) ? C_CHART[i] : TFT_WHITE;
        extDisplay.setTextColor(sel ? C_GREEN : fg, TFT_DARKGREY);
        extDisplay.setCursor(3, y + i * 14);
        extDisplay.print(chk ? "[x]" : "[ ]");
        extDisplay.setTextColor(sel ? C_GREEN : TFT_WHITE, TFT_DARKGREY);
        String lbl = g_session.lastSensors.names[i];
        if (lbl.length() > 7) lbl = lbl.substring(0, 7);
        extDisplay.print(lbl);
    }
    s_scDirtyLabels = false;
}

static void drawChartTopbar() {
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    const char* status = g_session.chartRunning ? "WYKRES  [ENTER=stop]" : "WYKRES  [ENTER=start]";
    extDisplay.print(status);
}

void sensorChart_Setup() {
    s_scLastPoll   = 0;
    s_scDirtyLabels= true;
    s_scCursorSel  = 0;
    g_session.chartRunning = false;

    extDisplay.fillScreen(TFT_BLACK);
    drawChartTopbar();
    drawChartLabels();

    extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 2);
    extDisplay.print("j/k=sensor  SPC=toggle  FN+5=clr  ESC=back");

    intSprite.fillScreen(TFT_BLACK);
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    intSprite.setTextSize(1);
    intSprite.setCursor(4, 4);
    intSprite.print("SENSOR CHART");
    intSprite.pushSprite(0, 0);
}

void sensorChart_Loop() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) goto poll;
    {
        auto& st = M5Cardputer.Keyboard.keysState();
        if (st.ctrl) {
            for (auto c : st.word) {
                if (c == '[') { transitionTo(STATE_MAINT_MENU); return; }
            }
        }
        if (st.fn) {
            for (auto c : st.word) {
                if (c == '4') { s_scCursorSel = max(0, s_scCursorSel - 1); s_scDirtyLabels = true; }
                if (c == '6') { s_scCursorSel = min(g_session.lastSensors.count - 1, s_scCursorSel + 1); s_scDirtyLabels = true; }
                if (c == '5') {
                    g_session.chartHead = 0; g_session.chartFilled = 0;
                    for (int i = 0; i < MAX_SENSORS; i++) {
                        g_session.chartMin[i] = 1e30f;
                        g_session.chartMax[i] = -1e30f;
                    }
                    extDisplay.fillRect(CHART_LEFT_W + 2, TOP_H + 1,
                                        EXT_W - CHART_LEFT_W - 2, INPUT_Y - TOP_H - 5, TFT_BLACK);
                }
            }
            goto redraw;
        }
        for (auto c : st.word) {
            if (c == 'j' || c == 'J') {
                s_scCursorSel = min(g_session.lastSensors.count - 1, s_scCursorSel + 1);
                s_scDirtyLabels = true;
            } else if (c == 'k' || c == 'K') {
                s_scCursorSel = max(0, s_scCursorSel - 1);
                s_scDirtyLabels = true;
            } else if (c == ' ') {
                g_session.sensorChecked[s_scCursorSel] = !g_session.sensorChecked[s_scCursorSel];
                s_scDirtyLabels = true;
            }
        }
        if (st.enter) {
            g_session.chartRunning = !g_session.chartRunning;
            drawChartTopbar();
        }
    }
redraw:
    if (s_scDirtyLabels) drawChartLabels();

poll:
    if (!g_session.chartRunning) return;
    unsigned long now = millis();
    if (now - s_scLastPoll >= SENSOR_POLL_MS) {
        s_scLastPoll = now;
        Serial1.print("?SENSORS\r\n");
    }
    if (g_session.lastSensors.valid) {
        pushChartSample();
        drawChart();
        g_session.lastSensors.valid = false; // consumed
    }
}
