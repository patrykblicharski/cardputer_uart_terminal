#include "display_ext.h"
#include <M5Cardputer.h>

LGFX_ILI9341 extDisplay;
M5Canvas      terminal(&extDisplay);

void extDisplay_Init() {
    extDisplay.init();
    extDisplay.setRotation(7);
    extDisplay.fillScreen(TFT_BLACK);
    terminal.setColorDepth(16);
    terminal.createSprite(TERM_W, TERM_H_FULL);
}

void extDraw_SimpleTopbar(const char* title) {
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    extDisplay.print(title);
}

void extTerm_Init() {
    extDisplay.fillScreen(TFT_BLACK);
    // Recreate sprite at full height (in case LoRa or another app resized it)
    terminal.deleteSprite();
    terminal.setColorDepth(16);
    terminal.createSprite(TERM_W, TERM_H_FULL);
    terminal.fillScreen(TFT_BLACK);
    terminal.setTextColor(C_GREEN, TFT_BLACK);
    terminal.setTextScroll(true);
    terminal.setTextWrap(true, true);
    terminal.setTextSize(1);
    terminal.setCursor(0, 0);
}

void extTerm_DrawTopbar(const char* portLbl, uint32_t baud, const char* deviceName) {
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);

    if (deviceName && deviceName[0]) {
        extDisplay.setTextColor(C_AMBER, TFT_DARKGREY);
        extDisplay.setCursor(3, 3);
        extDisplay.print(deviceName);
    } else {
        extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
        extDisplay.setCursor(3, 3);
        extDisplay.print("CARDPUTER UART");
    }

    char buf[28];
    snprintf(buf, sizeof(buf), "%s  %lu", portLbl, (unsigned long)baud);
    int tw = extDisplay.textWidth(buf);
    extDisplay.setTextColor(TFT_YELLOW, TFT_DARKGREY);
    extDisplay.setCursor(EXT_W - tw - 3, 3);
    extDisplay.print(buf);
}

void extTerm_DrawInputLine(const String& input) {
    extDisplay.fillRect(0, INPUT_Y, EXT_W, INPUT_H, TFT_BLACK);
    extDisplay.drawFastHLine(0, INPUT_Y, EXT_W, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 3);
    extDisplay.print("|> ");
    String disp = input;
    while (disp.length() > 0 && extDisplay.textWidth(disp.c_str()) > EXT_W - 30)
        disp.remove(0, 1);
    extDisplay.print(disp);
    extDisplay.setTextColor(C_AMBER, TFT_BLACK);
    extDisplay.print("_");
}

void extTerm_Push(const String& input) {
    terminal.pushSprite(TERM_X, TERM_Y);
    extTerm_DrawInputLine(input);
}
