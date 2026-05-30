#include "splash.h"
#include "app_state.h"
#include "display/display_ext.h"
#include "display/display_int.h"
#include "config.h"

static constexpr unsigned long SPLASH_MS = 2000;
static unsigned long s_start = 0;

void splash_Setup() {
    s_start = millis();

    extDisplay.fillScreen(TFT_BLACK);
    extDisplay.setTextSize(2);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    const char* title = "CARDPUTER UART TOOL";
    int tw = extDisplay.textWidth(title);
    extDisplay.setCursor((EXT_W - tw) / 2, EXT_H / 2 - 20);
    extDisplay.print(title);

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    const char* sub = "v2.0 -- Advanced diagnostics";
    tw = extDisplay.textWidth(sub);
    extDisplay.setCursor((EXT_W - tw) / 2, EXT_H / 2 + 10);
    extDisplay.print(sub);

    intDisplay_ShowLabel("CARDPUTER UART TOOL v2");
}

void splash_Loop() {
    unsigned long elapsed = millis() - s_start;
    if (elapsed > SPLASH_MS) {
        transitionTo(STATE_MENU);
        return;
    }
    int barX = 40, barY = EXT_H / 2 + 30;
    int barW = EXT_W - 80, barH = 8;
    int fill = (int)(barW * elapsed / SPLASH_MS);
    extDisplay.fillRect(barX, barY, barW, barH, TFT_DARKGREY);
    extDisplay.fillRect(barX, barY, fill, barH, C_GREEN);
    delay(20);
}
