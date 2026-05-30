#pragma once
#include <cstdint>

struct I2CDeviceInfo {
    uint8_t     addr;
    const char* name;
    const char* cat;
    const char* note;
};

const I2CDeviceInfo* i2cdb_lookup(uint8_t addr);
