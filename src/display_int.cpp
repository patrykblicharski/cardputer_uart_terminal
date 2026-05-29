#include "display_int.h"
#include <M5Cardputer.h>

M5Canvas intSprite(&M5Cardputer.Display);

void intDisplay_Init() {
    intSprite.createSprite(INT_W, INT_H);
    intSprite.setTextSize(1);
}

void intStatus_Draw(const TermStatus& s) {
    intSprite.fillScreen(TFT_BLACK);

    // Header bar
    intSprite.fillRect(0, 0, INT_W, 16, TFT_DARKGREY);
    intSprite.setTextSize(1);
    if (s.deviceName && s.deviceName[0]) {
        intSprite.setTextColor(C_AMBER, TFT_DARKGREY);
        intSprite.setCursor(4, 4);
        intSprite.print(s.deviceName);
    } else {
        intSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
        intSprite.setCursor(4, 4);
        intSprite.print("UART TERMINAL");
    }

    // Port / baud
    int y = 22;
    intSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    intSprite.setCursor(4,  y); intSprite.printf("PORT: %-4s", s.portLabel);
    intSprite.setCursor(80, y); intSprite.printf("BAUD: %lu", (unsigned long)s.baud);

    // Last RX source / USB state
    y += 12;
    intSprite.setCursor(4, y);  intSprite.printf("RX:   %-4s", s.lastRxLabel);
    intSprite.setTextColor(s.usbConnected ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    intSprite.setCursor(80, y); intSprite.printf("USB:  %s", s.usbConnected ? "ON " : "OFF");

    // Divider
    y += 14; intSprite.drawFastHLine(0, y, INT_W, TFT_DARKGREY); y += 4;

    intSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    intSprite.setTextColor(!s.localEcho  ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    intSprite.setCursor(4,  y); intSprite.printf("ECHO: %s", s.localEcho  ? "ON " : "OFF");
    intSprite.setTextColor(!s.sendCrlf   ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    intSprite.setCursor(80, y); intSprite.printf("CRLF: %s", s.sendCrlf   ? "ON " : "OFF");

    y += 12;
    intSprite.setTextColor(!s.filterAnsi ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    intSprite.setCursor(4,  y); intSprite.printf("ANSI: %s", s.filterAnsi ? "ON " : "OFF");
    intSprite.setTextColor(s.fontScale != 1 ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    intSprite.setCursor(80, y); intSprite.printf("FONT: %dx", (int)s.fontScale);

    // Divider
    y += 14; intSprite.drawFastHLine(0, y, INT_W, TFT_DARKGREY); y += 4;

    intSprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    intSprite.setCursor(4, y); intSprite.print("FN+G Grid  FN+H Maint  CTRL+[=menu");
    y += 10;
    intSprite.setCursor(4, y); intSprite.print("FN+1 Port  FN+2 Baud  FN+5 Clear");
    y += 10;
    intSprite.setCursor(4, y); intSprite.print("FN+3 Echo  FN+4 ANSI  FN+7 Font");

    intSprite.pushSprite(0, 0);
}
