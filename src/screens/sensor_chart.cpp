#include "sensor_chart.h"
#include "maint_menu.h"
#include "app_state.h"
#include "display/display_ext.h"
#include "display/display_int.h"
#include "config.h"
#include <M5Cardputer.h>

static constexpr unsigned long SENSOR_POLL_MS = 500;
static unsigned long s_lastPoll    = 0;
static bool          s_dirtyLabels = true;
static int           s_cursorSel   = 0;

// ─── Topbar ──────────────────────────────────────────────────────────────────

static void drawTopbar() {
    const char* status = g_session.chartRunning ? "WYKRES  [ENTER=stop]" : "WYKRES  [ENTER=start]";
    extDraw_SimpleTopbar(status);
}

// ─── Label panel (left column with checkboxes) ────────────────────────────────

static void drawLabels() {
    extDisplay.fillRect(0, TOP_H + 1, CHART_LEFT_W, INPUT_Y - TOP_H - 1, TFT_DARKGREY);
    extDisplay.drawFastVLine(CHART_LEFT_W, TOP_H + 1, INPUT_Y - TOP_H - 1, TFT_WHITE);
    extDisplay.setTextSize(1);

    int numSensors = min(g_session.lastSensors.count, (int)MAX_SENSORS);
    int y = TOP_H + 5;
    for (int i = 0; i < numSensors; i++) {
        bool sel = (i == s_cursorSel);
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
    s_dirtyLabels = false;
}

// ─── Chart area ──────────────────────────────────────────────────────────────

static void drawChart() {
    int chartX = CHART_LEFT_W + 2;
    int chartW = EXT_W - chartX;
    int chartY = TOP_H + 1;
    int chartH = INPUT_Y - chartY - 4;

    extDisplay.fillRect(chartX, chartY, chartW, chartH, TFT_BLACK);

    int cnt = g_session.chartFilled;
    if (cnt < 2) return;

    int numSensors = min(g_session.lastSensors.count, 4);
    for (int ci = 0; ci < numSensors; ci++) {
        if (!g_session.sensorChecked[ci]) continue;
        float mn = g_session.chartMin[ci];
        float mx = g_session.chartMax[ci];
        if (mx - mn < 0.001f) { mn -= 1.0f; mx += 1.0f; }

        uint16_t col = C_CHART[ci % 4];
        int prev_px = -1, prev_py = -1;
        for (int i = 0; i < cnt; i++) {
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

// ─── Ring buffer append ───────────────────────────────────────────────────────

static void pushSample() {
    SensorSample& s = g_session.lastSensors;
    if (!s.valid) return;
    int idx = g_session.chartHead;
    for (int ci = 0; ci < s.count && ci < MAX_SENSORS; ci++) {
        float v = s.values[ci];
        g_session.chartBuf[ci][idx] = v;
        if (v < g_session.chartMin[ci]) g_session.chartMin[ci] = v;
        if (v > g_session.chartMax[ci]) g_session.chartMax[ci] = v;
    }
    g_session.chartHead = (idx + 1) % CHART_SAMPLES;
    if (g_session.chartFilled < CHART_SAMPLES) g_session.chartFilled++;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void sensorChart_Setup() {
    s_lastPoll    = 0;
    s_dirtyLabels = true;
    s_cursorSel   = 0;
    g_session.chartRunning = false;

    extDisplay.fillScreen(TFT_BLACK);
    drawTopbar();
    drawLabels();
    extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 2);
    extDisplay.print("j/k=sensor  SPC=toggle  FN+5=clr  FN+DEL=back");
    intDisplay_ShowLabel("SENSOR CHART");
}

void sensorChart_Loop() {
    if (g_session.forceRedraw) { s_dirtyLabels = true; drawTopbar(); g_session.forceRedraw = false; }
    if (M5Cardputer.Keyboard.isChange()) {
        auto& st = M5Cardputer.Keyboard.keysState();

        if (st.fn && st.del) { transitionTo(STATE_MAINT_MENU); return; }
        if (st.fn) {
            for (auto c : st.word) {
                if (c == '4') { s_cursorSel = max(0, s_cursorSel - 1); s_dirtyLabels = true; }
                if (c == '6') { s_cursorSel = min(g_session.lastSensors.count - 1, s_cursorSel + 1); s_dirtyLabels = true; }
                if (c == '5') {
                    g_session.chartHead = 0; g_session.chartFilled = 0;
                    for (int i = 0; i < MAX_SENSORS; i++) {
                        g_session.chartMin[i] =  1e30f;
                        g_session.chartMax[i] = -1e30f;
                    }
                    extDisplay.fillRect(CHART_LEFT_W + 2, TOP_H + 1,
                                        EXT_W - CHART_LEFT_W - 2, INPUT_Y - TOP_H - 5, TFT_BLACK);
                }
            }
        } else {
            for (auto c : st.word) {
                if      (c == 'j' || c == 'J') { s_cursorSel = min(g_session.lastSensors.count - 1, s_cursorSel + 1); s_dirtyLabels = true; }
                else if (c == 'k' || c == 'K') { s_cursorSel = max(0, s_cursorSel - 1);                                s_dirtyLabels = true; }
                else if (c == ' ')             { g_session.sensorChecked[s_cursorSel] = !g_session.sensorChecked[s_cursorSel]; s_dirtyLabels = true; }
            }
            if (st.enter) {
                g_session.chartRunning = !g_session.chartRunning;
                drawTopbar();
            }
        }
    }

    if (s_dirtyLabels) drawLabels();

    if (!g_session.chartRunning) return;

    unsigned long now = millis();
    if (now - s_lastPoll >= SENSOR_POLL_MS) {
        s_lastPoll = now;
        Serial1.print("?SENSORS\r\n");
    }
    maint_ReadSensorSerial();
    if (g_session.lastSensors.valid) {
        pushSample();
        drawChart();
        g_session.lastSensors.valid = false;
    }
}
