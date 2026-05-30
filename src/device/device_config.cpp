#include "device_config.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

static constexpr size_t MAX_ENTRIES = 24;

static struct {
    String uid;
    String name;
    String path;
} s_entries[MAX_ENTRIES];
static size_t s_count = 0;

bool deviceConfig_Begin(String& error) {
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN)) {
        error = "SD mount failed";
        return false;
    }
    File dir = SD.open("/devices");
    if (!dir || !dir.isDirectory()) {
        error = "No /devices dir";
        return false;
    }
    s_count = 0;
    while (s_count < MAX_ENTRIES) {
        File f = dir.openNextFile();
        if (!f) break;
        String fname = f.name();
        if (!f.isDirectory() && fname.endsWith(".json")) {
            JsonDocument doc;
            if (!deserializeJson(doc, f)) {
                String uid     = doc["uid"]        | "";
                String devName = doc["deviceName"] | "";
                if (uid.length() > 0) {
                    s_entries[s_count].uid  = uid;
                    s_entries[s_count].name = devName.length() ? devName : uid;
                    s_entries[s_count].path = String("/devices/") + fname;
                    s_count++;
                }
            }
        }
        f.close();
    }
    dir.close();
    return true;
}

bool deviceConfig_LoadAtIndex(size_t index, DeviceConfig& out, String& error) {
    if (index >= s_count) { error = "Index OOB"; return false; }
    File f = SD.open(s_entries[index].path.c_str());
    if (!f) { error = "Cannot open " + s_entries[index].path; return false; }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { error = String("JSON: ") + err.c_str(); return false; }

    out.path       = s_entries[index].path;
    out.deviceName = doc["deviceName"] | s_entries[index].name.c_str();
    out.uid        = doc["uid"]        | "";
    out.typeId     = doc["typeId"]     | "";
    out.maintType  = doc["maintType"]  | "GENERIC";
    out.baud       = doc["baud"]       | (uint32_t)115200;
    out.sensors.clear();
    out.actions.clear();

    for (JsonObject s : doc["sensors"].as<JsonArray>()) {
        SensorConfig sc;
        sc.name        = (const char*)s["name"]        ? String((const char*)s["name"])        : String();
        sc.unit        = (const char*)s["unit"]        ? String((const char*)s["unit"])        : String();
        sc.type        = (const char*)s["type"]        ? String((const char*)s["type"])        : String("analog");
        sc.pin         = s["pin"] | -1;
        sc.maintenance = s["maintenance"] | false;
        out.sensors.push_back(sc);
    }
    for (JsonObject a : doc["maintenanceActions"].as<JsonArray>()) {
        MaintenanceAction ma;
        ma.label   = (const char*)a["label"]   ? String((const char*)a["label"])   : String();
        ma.command = (const char*)a["command"] ? String((const char*)a["command"]) : String();
        out.actions.push_back(ma);
    }
    return true;
}

bool deviceConfig_LoadByUid(const String& uid, DeviceConfig& out, String& error) {
    for (size_t i = 0; i < s_count; i++) {
        if (s_entries[i].uid == uid)
            return deviceConfig_LoadAtIndex(i, out, error);
    }
    error = "UID not found: " + uid;
    return false;
}

size_t deviceConfig_Count() { return s_count; }

String deviceConfig_LabelAt(size_t i) {
    return i < s_count ? s_entries[i].name : String();
}
String deviceConfig_PathAt(size_t i) {
    return i < s_count ? s_entries[i].path : String();
}

bool deviceConfig_ParseIdentity(const String& line, DeviceIdentity& out) {
    // "DeviceName;UID;TypeID;maintType"
    int p1 = line.indexOf(';');
    if (p1 < 0) return false;
    int p2 = line.indexOf(';', p1 + 1);
    if (p2 < 0) return false;
    int p3 = line.indexOf(';', p2 + 1);

    out.deviceName = line.substring(0, p1);
    out.uid        = line.substring(p1 + 1, p2);
    out.typeId     = line.substring(p2 + 1, p3 >= 0 ? p3 : (int)line.length());
    out.maintType  = p3 >= 0 ? line.substring(p3 + 1) : String("GENERIC");

    out.deviceName.trim(); out.uid.trim();
    out.typeId.trim();     out.maintType.trim();

    return out.uid.length() > 0;
}
