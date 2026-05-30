#include "i2c_db.h"

static const I2CDeviceInfo kDb[] = {
    // Magnetometers
    { 0x0D, "QMC5883L",          "Magnetometer",    "Clone of HMC5883L"              },
    { 0x1E, "HMC5883L",          "Magnetometer",    "Triple-axis compass"            },

    // Ambient Light
    { 0x10, "VEML7700",          "Ambient Light",   "Visible+IR, fixed addr"         },
    { 0x23, "BH1750",            "Light Lux",       "ADDR=GND. Also PCF8574 0x23"    },
    { 0x29, "VL53L0X / TSL2561", "ToF / Light",     "VL53=dist; TSL2561=light"       },
    { 0x39, "APDS-9960",         "Gesture+Color",   "Also TSL2561 (ADDR=float)"      },
    { 0x5C, "BH1750",            "Light Lux",       "ADDR=VCC (alt addr)"            },

    // Temperature
    { 0x18, "MCP9808 / LIS3DH",  "Temp / Accel",    "MCP9808=+-0.25C; LIS3DH=accel" },
    { 0x48, "ADS1115 / TMP102",  "ADC / Temp",      "ADDR=GND; 16-bit ADC or temp"   },
    { 0x49, "ADS1115 / TMP102",  "ADC / Temp",      "ADDR=VCC"                       },
    { 0x4A, "ADS1115",           "16-bit ADC",      "ADDR=SDA"                       },
    { 0x4B, "ADS1115",           "16-bit ADC",      "ADDR=SCL"                       },
    { 0x5E, "MLX90614",          "IR Temperature",  "Non-contact -70..380C"          },

    // Humidity + Temperature
    { 0x27, "DHT12",             "Humidity+Temp",   "I2C DHT12, fixed addr"          },
    { 0x38, "AHT20 / AHT21",     "Humidity+Temp",   "Fixed 0x38. Also PCF8574A"      },
    { 0x40, "HTU21D / Si7021",   "Humidity+Temp",   "Fixed addr. Also HDC1080"       },
    { 0x44, "SHT30 / SHT31",     "Humidity+Temp",   "ADDR=GND"                       },
    { 0x45, "SHT30 / SHT31",     "Humidity+Temp",   "ADDR=VCC (alt addr)"            },

    // Pressure + Temperature
    { 0x76, "BMP280 / BME280",   "Pressure+Temp",   "SDO=GND. BME280 adds humidity"  },
    { 0x77, "BMP280 / BME280",   "Pressure+Temp",   "SDO=VCC. Also BMP085/BMP180"    },

    // IMU / Accelerometer / Gyro
    { 0x19, "LSM303 / LSM6DSx",  "Accel / IMU",     "LSM303 accel part"              },
    { 0x1C, "MMA8452Q",          "Accelerometer",   "SA0=GND"                        },
    { 0x1D, "MMA8452Q / ADXL",   "Accelerometer",   "SA0=VCC; ADXL345 ADDR=VCC"     },
    { 0x53, "ADXL345",           "Accelerometer",   "ALT_ADDR=GND"                   },
    { 0x68, "MPU-6050 / DS1307", "IMU / RTC",       "AD0=GND=>IMU; fixed=>DS1307"    },
    { 0x69, "MPU-6050 / 6500",   "IMU Gyro+Accel",  "AD0=VCC (alt addr)"             },
    { 0x6A, "LSM6DS3 / ICM",     "IMU Gyro+Accel",  "SDO/SA0=GND"                    },
    { 0x6B, "LSM6DS3 / ICM",     "IMU Gyro+Accel",  "SDO/SA0=VCC (alt addr)"         },

    // OLED / Display drivers
    { 0x3C, "SSD1306 / SH1106",  "OLED Display",    "SA0=GND. 128x64 or 128x32"      },
    { 0x3D, "SSD1306 / SH1106",  "OLED Display",    "SA0=VCC (alt addr)"             },

    // Gas / Air Quality
    { 0x5A, "CCS811",            "CO2 / TVOC",      "nWAKE=GND=>0x5A"               },
    { 0x5B, "CCS811",            "CO2 / TVOC",      "nWAKE=VCC=>0x5B"               },
    { 0x58, "SGP30",             "CO2eq / TVOC",    "Fixed addr (Sensirion)"         },
    { 0x59, "SGP40",             "VOC Index",       "Fixed addr (Sensirion)"         },

    // HR / SpO2
    { 0x57, "MAX30102",          "HR + SpO2",       "Heart rate + pulse ox sensor"   },

    // RTC
    { 0x51, "PCF8563",           "RTC",             "NXP low-power real-time clock"  },
    { 0x6F, "MCP7940N",          "RTC",             "Microchip RTCC with SRAM"       },
    { 0x32, "RX8025T",           "RTC",             "Epson high-precision RTC"       },

    // EEPROM
    { 0x50, "AT24Cxx EEPROM",    "EEPROM",          "A0-A2=GND; range 0x50-0x57"    },

    // Fuel Gauge
    { 0x36, "MAX17043",          "Fuel Gauge",      "LiPo SOC estimator"             },

    // I/O Expanders
    { 0x20, "PCF8574 / MCP23017","I/O Expander",    "8-bit I/O; A0-A2 set addr"     },
    { 0x21, "PCF8574 / MCP23017","I/O Expander",    "A0=VCC"                         },
    { 0x22, "PCF8574 / MCP23017","I/O Expander",    "A1=VCC"                         },
    { 0x24, "PCF8574 / MCP23017","I/O Expander",    "A2=VCC"                         },
    { 0x25, "PCF8574 / MCP23017","I/O Expander",    "A0+A2=VCC"                      },
    { 0x26, "PCF8574 / MCP23017","I/O Expander",    "A1+A2=VCC"                      },

    // I2C Multiplexers
    { 0x70, "TCA9548A",          "I2C Multiplexer", "8-ch mux; A0-A2 set 0x70-0x77" },
    { 0x71, "TCA9548A",          "I2C Multiplexer", "A0=VCC"                         },
    { 0x72, "TCA9548A",          "I2C Multiplexer", "A1=VCC"                         },
    { 0x73, "TCA9548A",          "I2C Multiplexer", "A0+A1=VCC"                      },
    { 0x74, "TCA9548A",          "I2C Multiplexer", "A2=VCC"                         },

    // PWM / LED Drivers
    { 0x60, "PCA9685",           "16ch PWM Driver", "A0-A5 addr bits (0x40-0x7F)"   },
    { 0x61, "PCA9685",           "16ch PWM Driver", "A0=VCC"                         },
    { 0x62, "PCA9685",           "16ch PWM Driver", "A1=VCC"                         },
};

static const int kDbCount = (int)(sizeof(kDb) / sizeof(kDb[0]));

const I2CDeviceInfo* i2cdb_lookup(uint8_t addr) {
    for (int i = 0; i < kDbCount; i++)
        if (kDb[i].addr == addr) return &kDb[i];
    return nullptr;
}
