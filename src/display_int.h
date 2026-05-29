#pragma once
#include <M5GFX.h>
#include "config.h"

extern M5Canvas intSprite;

struct TermStatus {
    const char* portLabel;
    uint32_t    baud;
    const char* lastRxLabel;
    bool        usbConnected;
    bool        localEcho;
    bool        filterAnsi;
    bool        sendCrlf;
    uint8_t     fontScale;
};

void intDisplay_Init();
void intStatus_Draw(const TermStatus& s);
