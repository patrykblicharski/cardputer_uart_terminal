#include <M5Cardputer.h>
#include "display_ext.h"
#include "display_int.h"
#include "app_state.h"
#include "splash.h"
#include "menu.h"
#include "device_detect.h"
#include "select_list.h"
#include "uart_term.h"
#include "maint_menu.h"

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
    }
}

// ─── Arduino setup / loop ─────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    intDisplay_Init();
    delay(100);
    extDisplay_Init();

    g_session.reset();
    transitionTo(STATE_SPLASH);
}

void loop() {
    M5Cardputer.update();
    switch (g_session.app) {
        case STATE_SPLASH:       splash_Loop();       break;
        case STATE_MENU:         menu_Loop();         break;
        case STATE_DETECT:       detect_Loop();       break;
        case STATE_SELECT_LIST:  selectList_Loop();   break;
        case STATE_TERMINAL:     uartTerm_Loop();     break;
        case STATE_MAINT_MENU:   maint_Loop();        break;
        case STATE_SENSOR_VIEW:  sensorView_Loop();   break;
        case STATE_SENSOR_CHART: sensorChart_Loop();  break;
    }
}
