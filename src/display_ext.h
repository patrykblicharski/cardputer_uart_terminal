#pragma once
#include "ili9341.h"
#include "config.h"
#include <WString.h>

extern LGFX_ILI9341 extDisplay;
extern M5Canvas      terminal;

void extDisplay_Init();

// Called once when entering terminal state; clears display and sprite
void extTerm_Init();

// Topbar — optionally show device name
void extTerm_DrawTopbar(const char* portLabel, uint32_t baud,
                        const char* deviceName = nullptr);

// Input line at bottom of screen
void extTerm_DrawInputLine(const String& input);

// Push terminal sprite to display and redraw input line
void extTerm_Push(const String& input);
