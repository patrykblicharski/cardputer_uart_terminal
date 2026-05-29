#pragma once
#include <WString.h>

// Parse "!SENSORS:Name=val,...!" into g_session.lastSensors; returns true on success
bool maint_ParseSensors(const String& line);

// Overlay maintenance menu (STATE_MAINT_MENU)
void maint_Setup();
void maint_Loop();

// Full-screen sensor value view (STATE_SENSOR_VIEW)
void sensorView_Setup();
void sensorView_Loop();

// Full-screen rolling chart (STATE_SENSOR_CHART)
void sensorChart_Setup();
void sensorChart_Loop();
