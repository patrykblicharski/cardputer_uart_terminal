#pragma once
#include <WString.h>

// Parse "!SENSORS:Name=val,...!" into g_session.lastSensors; returns true on success
bool maint_ParseSensors(const String& line);

// Drain Serial1 and parse any sensor response lines into g_session.lastSensors.
// Must be called each loop iteration in sensor states (uart_term is not running then).
void maint_ReadSensorSerial();

// Overlay maintenance menu (STATE_MAINT_MENU)
void maint_Setup();
void maint_Loop();
