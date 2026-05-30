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
    const char* deviceName = nullptr;  // null = no device
};

void intDisplay_Init();
void intDisplay_ShowLabel(const char* label);  // single-line title on black bg
void intStatus_Draw(const TermStatus& s);
