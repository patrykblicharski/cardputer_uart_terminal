#include <M5Cardputer.h>
#include "display_ext.h"
#include "display_int.h"
#include "uart_term.h"

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    intDisplay_Init();          // internal ST7789 sprite — must be before extDisplay
    delay(100);
    extDisplay_Init();          // external ILI9341 — after M5Cardputer.begin + intDisplay

    uartTerm_Setup();
}

void loop() {
    M5Cardputer.update();
    uartTerm_Loop();
}
