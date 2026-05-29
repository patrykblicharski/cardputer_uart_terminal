#pragma once
// device_maint.h — single-header Arduino module for ESP32 devices.
// Drop this file into your ESP32 project and #include it.
//
// Usage:
//   DeviceMaint maint(Serial1, "MyNode", "ABC123DEF456", "SENSOR_NODE_V1", "SENSOR");
//
//   void setup() {
//       Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
//       maint.addSensor("Temperature", "°C", 34, []() -> float {
//           return analogRead(34) * 3.3f / 4095.0f * 100.0f;
//       });
//       maint.addCommand("Reset", []() -> String {
//           ESP.restart(); return "Restarting...";
//       });
//       maint.begin();
//   }
//
//   void loop() {
//       // your logic...
//       maint.loop();
//   }

#include <Arduino.h>
#include <functional>

class DeviceMaint {
public:
    static constexpr int MAX_CMDS    = 9;
    static constexpr int MAX_SENSORS = 16;

    DeviceMaint(Stream& uart, const char* name, const char* uid,
                const char* typeId, const char* maintType)
        : _uart(uart), _name(name), _uid(uid),
          _typeId(typeId), _maintType(maintType),
          _cmdCount(0), _sensorCount(0),
          _identified(false), _lastHello(0) {}

    void addCommand(const char* name, std::function<String()> handler) {
        if (_cmdCount >= MAX_CMDS) return;
        strncpy(_cmds[_cmdCount].name, name, sizeof(_cmds[0].name) - 1);
        _cmds[_cmdCount].fn = handler;
        _cmdCount++;
    }

    void addSensor(const char* name, const char* unit, int pin,
                   std::function<float()> reader) {
        if (_sensorCount >= MAX_SENSORS) return;
        strncpy(_sensors[_sensorCount].name, name, sizeof(_sensors[0].name) - 1);
        strncpy(_sensors[_sensorCount].unit, unit, sizeof(_sensors[0].unit) - 1);
        _sensors[_sensorCount].pin = pin;
        _sensors[_sensorCount].fn  = reader;
        _sensorCount++;
    }

    void begin() {
        _rxBuf      = "";
        _identified = false;
        _lastHello  = 0;
    }

    void loop() {
        while (_uart.available()) {
            char c = _uart.read();
            if (c == '\n') {
                _rxBuf.trim();
                _processLine(_rxBuf);
                _rxBuf = "";
            } else if (c != '\r') {
                _rxBuf += c;
            }
        }
    }

private:
    Stream&     _uart;
    const char* _name;
    const char* _uid;
    const char* _typeId;
    const char* _maintType;

    struct Cmd    { char name[24]; std::function<String()> fn; };
    struct Sensor { char name[24]; char unit[8]; int pin; std::function<float()> fn; };

    Cmd    _cmds[MAX_CMDS];
    int    _cmdCount;
    Sensor _sensors[MAX_SENSORS];
    int    _sensorCount;

    String        _rxBuf;
    bool          _identified;
    unsigned long _lastHello;

    void _processLine(const String& line) {
        if (line == "HELLO") {
            _uart.print("HELLO SIR\r\n");
            delay(10);
            _sendIdent();
            _identified = true;
            return;
        }
        if (line == "?CMD") {
            _sendCmdList();
            return;
        }
        if (line == "?SENSORS") {
            _sendSensorData();
            return;
        }
        // Check for dynamic commands by name
        for (int i = 0; i < _cmdCount; i++) {
            if (line == _cmds[i].name) {
                String result = _cmds[i].fn();
                _uart.print(result);
                _uart.print("\r\n");
                return;
            }
        }
    }

    void _sendIdent() {
        // Format: "DeviceName;UID;TypeID;maintType\r\n"
        _uart.print(_name);
        _uart.print(';');
        _uart.print(_uid);
        _uart.print(';');
        _uart.print(_typeId);
        _uart.print(';');
        _uart.print(_maintType);
        _uart.print("\r\n");
    }

    void _sendCmdList() {
        // Format: "!CMD:func1,func2,...!\r\n"
        _uart.print("!CMD:");
        for (int i = 0; i < _cmdCount; i++) {
            if (i) _uart.print(',');
            _uart.print(_cmds[i].name);
        }
        _uart.print("!\r\n");
    }

    void _sendSensorData() {
        // Format: "!SENSORS:Name=val,Name2=val2,...!\r\n"
        _uart.print("!SENSORS:");
        for (int i = 0; i < _sensorCount; i++) {
            if (i) _uart.print(',');
            float v = _sensors[i].fn();
            char buf[48];
            snprintf(buf, sizeof(buf), "%s=%.2f", _sensors[i].name, v);
            _uart.print(buf);
        }
        _uart.print("!\r\n");
    }
};
