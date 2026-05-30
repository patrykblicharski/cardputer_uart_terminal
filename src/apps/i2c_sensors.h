#pragma once
#include <Wire.h>
#include <cstdint>

static constexpr int SENS_MAX_READINGS = 4;
static constexpr int SENS_MAX_PARAMS   = 8;
static constexpr int SENS_MAX_VALS     = 8;

struct SensorReading {
    char label[14];
    char value[24];
};

struct SensorParam {
    char        name[20];
    const char* vals[SENS_MAX_VALS];
    uint8_t     num_vals;
    uint8_t     idx;
    bool        ro;
};

struct SensorDetail {
    SensorReading readings[SENS_MAX_READINGS];
    int           num_readings;
    SensorParam   params[SENS_MAX_PARAMS];
    int           num_params;
    char          note[48];
    bool          read_ok;
};

void sensor_init(uint8_t addr, TwoWire& bus, SensorDetail& det);
void sensor_read(uint8_t addr, TwoWire& bus, SensorDetail& det);
void sensor_set_param(uint8_t addr, TwoWire& bus, SensorDetail& det, int pidx, int delta);
