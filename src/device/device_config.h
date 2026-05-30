#pragma once
#include <WString.h>
#include <vector>

struct SensorConfig {
    String name;
    String unit;
    String type;        // "analog", "digital", "1wire", "i2c"
    int    pin = -1;
    bool   maintenance = false;
};

struct MaintenanceAction {
    String label;
    String command;
};

struct DeviceConfig {
    String path;
    String deviceName;
    String uid;
    String typeId;
    String maintType;
    uint32_t baud = 115200;
    std::vector<SensorConfig>       sensors;
    std::vector<MaintenanceAction>  actions;
    bool isValid() const { return uid.length() > 0; }
};

struct DeviceIdentity {
    String deviceName;
    String uid;
    String typeId;
    String maintType;
    bool isValid() const { return uid.length() > 0; }
};

bool   deviceConfig_Begin(String& error);
bool   deviceConfig_LoadByUid(const String& uid, DeviceConfig& out, String& error);
bool   deviceConfig_LoadAtIndex(size_t index, DeviceConfig& out, String& error);
size_t deviceConfig_Count();
String deviceConfig_LabelAt(size_t index);
String deviceConfig_PathAt(size_t index);
bool   deviceConfig_ParseIdentity(const String& line, DeviceIdentity& out);
