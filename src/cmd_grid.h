#pragma once
#include <WString.h>

// Parse "!CMD:func1,func2,...!" line; fills g_session.dynamicCmds, returns true on success
bool cmdGrid_ParseResponse(const String& line);

// Draw the 5-column x 2-row grid at CMD_GRID_Y on extDisplay
void cmdGrid_Draw();
