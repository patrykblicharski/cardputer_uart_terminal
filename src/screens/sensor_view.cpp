#include "sensor_view.h"
#include "maint_menu.h"
#include "app_state.h"
#include "display/display_ext.h"
#include "display/display_int.h"
#include "config.h"
#include <M5Cardputer.h>

static constexpr unsigned long SENSOR_POLL_MS = 500;
static unsigned long s_lastPoll = 0;

static void draw() {
    extDisplay.fillScreen(TFT_BLACK);
    extDraw_SimpleTopbar("CZUJNIKI  [auto-refresh 500ms]");
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
    extDisplay.print("FN+DEL = powrot");
}

void sensorView_Setup() {
    s_lastPoll = 0;
    g_session.lastSensors.valid = false;
    extDisplay.fillScreen(TFT_BLACK);
    draw();
    intDisplay_ShowLabel("SENSOR VIEW");
}

void sensorView_Loop() {
    if (g_session.forceRedraw) { draw(); g_session.forceRedraw = false; }
    if (M5Cardputer.Keyboard.isChange()) {
        auto& st = M5Cardputer.Keyboard.keysState();
        if (st.fn && st.del) { transitionTo(STATE_MAINT_MENU); return; }
    }

    unsigned long now = millis();
    if (now - s_lastPoll >= SENSOR_POLL_MS) {
        s_lastPoll = now;
        Serial1.print("?SENSORS\r\n");
    }
    maint_ReadSensorSerial();
    if (g_session.lastSensors.valid) {
        draw();
        g_session.lastSensors.valid = false;
    }
}
