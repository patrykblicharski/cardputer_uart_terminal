#pragma once
#include "device_config.h"
#include "config.h"

enum AppState {
    STATE_SPLASH = 0,
    STATE_MENU,
    STATE_DETECT,
    STATE_SELECT_LIST,
    STATE_TERMINAL,
    STATE_MAINT_MENU,
    STATE_SENSOR_VIEW,
    STATE_SENSOR_CHART
};

struct SensorSample {
    String names[MAX_SENSORS];
    float  values[MAX_SENSORS];
    int    count = 0;
    bool   valid = false;
};

struct SessionState {
    AppState     app              = STATE_SPLASH;
    DeviceConfig config;
    bool         configLoaded     = false;
    bool         deviceIdentified = false;
    // Dynamic commands from ?CMD response, mapped CTRL+A,S,D,F,G,H,J,K,L
    String       dynamicCmds[9];
    int          dynamicCmdCount  = 0;
    bool         cmdGridVisible   = false;
    bool         chartRunning     = false;
    bool         sensorChecked[MAX_SENSORS];
    float        chartBuf[MAX_SENSORS][CHART_SAMPLES];
    int          chartHead        = 0;
    int          chartFilled      = 0;
    float        chartMin[MAX_SENSORS];
    float        chartMax[MAX_SENSORS];
    SensorSample lastSensors;

    void reset() {
        configLoaded     = false;
        deviceIdentified = false;
        dynamicCmdCount  = 0;
        cmdGridVisible   = false;
        chartRunning     = false;
        chartHead        = 0;
        chartFilled      = 0;
        lastSensors.valid = false;
        for (int i = 0; i < MAX_SENSORS; i++) {
            sensorChecked[i] = true;
            chartMin[i] = 1e30f;
            chartMax[i] = -1e30f;
        }
    }
};

extern SessionState g_session;
void transitionTo(AppState next);
