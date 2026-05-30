#include <M5Cardputer.h>
#include "display/display_ext.h"
#include "display/display_int.h"
#include "app_state.h"
#include "screens/splash.h"
#include "screens/menu.h"
#include "screens/device_detect.h"
#include "screens/select_list.h"
#include "screens/uart_term.h"
#include "screens/maint_menu.h"
#include "screens/sensor_view.h"
#include "screens/sensor_chart.h"
#include "apps/app_i2c.h"
#include "apps/app_lora.h"
#include "screens/help_overlay.h"

// ─── Global session ───────────────────────────────────────────────────────────

SessionState g_session;

// ─── State machine ────────────────────────────────────────────────────────────

void transitionTo(AppState next) {
    g_session.app = next;
    switch (next) {
        case STATE_SPLASH:      splash_Setup();      break;
        case STATE_MENU:        menu_Setup();        break;
        case STATE_DETECT:      detect_Setup();      break;
        case STATE_SELECT_LIST: selectList_Setup();  break;
        case STATE_TERMINAL:    uartTerm_Setup();    break;
        case STATE_MAINT_MENU:  maint_Setup();       break;
        case STATE_SENSOR_VIEW: sensorView_Setup();  break;
        case STATE_SENSOR_CHART:sensorChart_Setup(); break;
        case STATE_APP_I2C:     i2cApp_Setup();      break;
        case STATE_APP_LORA:    loraApp_Setup();      break;
    }
}

// ─── Arduino setup / loop ─────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    M5Cardputer.begin();

    intDisplay_Init();
    delay(100);
    extDisplay_Init();

    g_session.reset();
    transitionTo(STATE_SPLASH);
}

void loop() {
    M5Cardputer.update();

    if (helpOverlay_IsActive()) {
        helpOverlay_HandleKey();
        return;
    }

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto& st = M5Cardputer.Keyboard.keysState();
        if (st.ctrl) {
            for (auto c : st.word) {
                if (c == '=') { helpOverlay_Show(g_session.app); return; }
            }
        }
    }

    switch (g_session.app) {
        case STATE_SPLASH:       splash_Loop();       break;
        case STATE_MENU:         menu_Loop();         break;
        case STATE_DETECT:       detect_Loop();       break;
        case STATE_SELECT_LIST:  selectList_Loop();   break;
        case STATE_TERMINAL:     uartTerm_Loop();     break;
        case STATE_MAINT_MENU:   maint_Loop();        break;
        case STATE_SENSOR_VIEW:  sensorView_Loop();   break;
        case STATE_SENSOR_CHART: sensorChart_Loop();  break;
        case STATE_APP_I2C:      i2cApp_Loop();       break;
        case STATE_APP_LORA:     loraApp_Loop();      break;
    }
}
