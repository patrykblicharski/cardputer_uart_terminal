/*
 * Wemos D1 Mini ESP32 — Nordic UART Service (NUS) peripheral
 *
 * Cardputer BLE UART client can scan, connect, and send line commands.
 * Same NUS UUIDs as src/apps/app_ble_uart.cpp.
 *
 * USB Serial (115200) mirrors BLE: type the same commands from a PC.
 *
 * Commands (CRLF):
 *   HELP / ?          list commands
 *   PING              PONG
 *   HELLO             HELLO SIR + identity (UART-style)
 *   ID / STATUS       chip, heap, uptime, BLE clients
 *   UPTIME / HEAP
 *   ADC               analog GPIO34
 *   TEMP              internal temperature
 *   LED ON|OFF|TOGGLE onboard LED (GPIO2, active-low)
 *   SENSORS / ?SENSORS  demo sensor line
 *   STREAM ON [ms]    periodic STATUS (default 1000)
 *   STREAM OFF
 *   ECHO <text>
 *   RESET
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_system.h>

static const char* DEVICE_NAME = "Wemos-UART";
static const char* DEVICE_UID  = "WEMOS-D1-ESP32";
static const char* TYPE_ID     = "BLE_UART_SRC";

static NimBLEUUID NUS_SVC("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID NUS_RX ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID NUS_TX ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

#ifndef LED_PIN
#define LED_PIN 2
#endif

static NimBLEServer*         s_server = nullptr;
static NimBLECharacteristic* s_tx     = nullptr;
static bool                  s_ledOn  = false;
static bool                  s_stream = false;
static uint32_t              s_streamMs = 1000;
static uint32_t              s_lastStream = 0;
static String                s_bleLine;
static String                s_usbLine;
static String                s_pendingCmd;
static volatile bool         s_cmdReady = false;
static bool                  s_welcome  = false;
static uint32_t              s_welcomeAt = 0;

static void ledWrite(bool on) {
    s_ledOn = on;
    digitalWrite(LED_PIN, on ? LOW : HIGH);
}

static void reply(const char* line) {
    Serial.println(line);
    if (!s_tx || !s_server || s_server->getConnectedCount() == 0) return;
    size_t n = strlen(line);
    const size_t chunk = 20;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(line);
    while (n) {
        size_t c = n > chunk ? chunk : n;
        s_tx->setValue(p, c);
        s_tx->notify();
        p += c;
        n -= c;
        delay(5);
    }
    const uint8_t crlf[] = {'\r', '\n'};
    s_tx->setValue(crlf, 2);
    s_tx->notify();
}

static void sendHelp() {
    reply("Wemos-UART commands:");
    reply("  HELP / ?");
    reply("  PING");
    reply("  HELLO");
    reply("  ID / STATUS");
    reply("  UPTIME  HEAP  ADC  TEMP");
    reply("  LED ON|OFF|TOGGLE");
    reply("  SENSORS");
    reply("  STREAM ON [ms] | STREAM OFF");
    reply("  ECHO <text>");
    reply("  RESET");
}

static void sendHello() {
    reply("HELLO SIR");
    char ident[96];
    snprintf(ident, sizeof(ident), "%s;%s;%s;SENSOR",
             DEVICE_NAME, DEVICE_UID, TYPE_ID);
    reply(ident);
}

static void sendStatus() {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "STATUS name=%s heap=%u uptime_ms=%lu clients=%u led=%s stream=%s",
             DEVICE_NAME,
             (unsigned)ESP.getFreeHeap(),
             (unsigned long)millis(),
             (unsigned)(s_server ? s_server->getConnectedCount() : 0),
             s_ledOn ? "ON" : "OFF",
             s_stream ? "ON" : "OFF");
    reply(buf);
}

static void sendSensors() {
    uint32_t adc = analogRead(34);
    float temp = temperatureRead();
    float vbat = adc * 3.3f / 4095.0f;
    char buf[128];
    snprintf(buf, sizeof(buf),
             "!SENSORS:Uptime=%.0f,Heap=%.0f,ADC=%.2f,Temp=%.1f!",
             millis() / 1000.0f,
             (float)ESP.getFreeHeap(),
             vbat,
             temp);
    reply(buf);
}

static void handleLine(String line) {
    line.trim();
    if (line.isEmpty()) return;

    String up = line;
    up.toUpperCase();

    if (up == "HELP" || up == "?") {
        sendHelp();
        return;
    }
    if (up == "PING") {
        reply("PONG");
        return;
    }
    if (up == "HELLO") {
        sendHello();
        return;
    }
    if (up == "ID" || up == "STATUS") {
        sendStatus();
        return;
    }
    if (up == "UPTIME") {
        char buf[40];
        snprintf(buf, sizeof(buf), "UPTIME %lu ms", (unsigned long)millis());
        reply(buf);
        return;
    }
    if (up == "HEAP") {
        char buf[40];
        snprintf(buf, sizeof(buf), "HEAP %u", (unsigned)ESP.getFreeHeap());
        reply(buf);
        return;
    }
    if (up == "ADC") {
        uint32_t raw = analogRead(34);
        char buf[48];
        snprintf(buf, sizeof(buf), "ADC gpio34=%u v=%.2f",
                 (unsigned)raw, raw * 3.3f / 4095.0f);
        reply(buf);
        return;
    }
    if (up == "TEMP") {
        char buf[32];
        snprintf(buf, sizeof(buf), "TEMP %.1f C", temperatureRead());
        reply(buf);
        return;
    }
    if (up == "LED ON") {
        ledWrite(true);
        reply("LED ON");
        return;
    }
    if (up == "LED OFF") {
        ledWrite(false);
        reply("LED OFF");
        return;
    }
    if (up == "LED TOGGLE" || up == "LED") {
        ledWrite(!s_ledOn);
        reply(s_ledOn ? "LED ON" : "LED OFF");
        return;
    }
    if (up == "SENSORS" || up == "?SENSORS") {
        sendSensors();
        return;
    }
    if (up == "STREAM OFF") {
        s_stream = false;
        reply("STREAM OFF");
        return;
    }
    if (up == "STREAM ON" || up.startsWith("STREAM ON ")) {
        s_streamMs = 1000;
        if (up.startsWith("STREAM ON ")) {
            int ms = up.substring(10).toInt();
            if (ms >= 200) s_streamMs = (uint32_t)ms;
        }
        s_stream = true;
        s_lastStream = 0;
        char buf[32];
        snprintf(buf, sizeof(buf), "STREAM ON %lu", (unsigned long)s_streamMs);
        reply(buf);
        return;
    }
    if (up.startsWith("ECHO ")) {
        reply(line.substring(5).c_str());
        return;
    }
    if (up == "RESET") {
        reply("RESET");
        delay(200);
        ESP.restart();
        return;
    }

    reply("ERR unknown cmd - type HELP");
}

class ServerCbs : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
        (void)server;
        Serial.printf("BLE connect %s\n", info.getAddress().toString().c_str());
        s_welcome = true;
        s_welcomeAt = millis() + 400;
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
        (void)info;
        Serial.printf("BLE disconnect reason=%d\n", reason);
        s_stream = false;
        s_welcome = false;
        s_bleLine = "";
        s_cmdReady = false;
        server->startAdvertising();
    }
};

class RxCbs : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
        (void)info;
        NimBLEAttValue v = chr->getValue();
        const uint8_t* p = v.data();
        size_t n = v.size();
        for (size_t i = 0; i < n; i++) {
            char c = (char)p[i];
            if (c == '\n') {
                s_pendingCmd = s_bleLine;
                s_bleLine = "";
                s_cmdReady = true;
            } else if (c != '\r') {
                if (s_bleLine.length() < 200) s_bleLine += c;
            }
        }
    }
};

static ServerCbs s_serverCbs;
static RxCbs     s_rxCbs;

static void startBle() {
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(256);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_serverCbs);

    NimBLEService* svc = s_server->createService(NUS_SVC);

    s_tx = svc->createCharacteristic(
        NUS_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

    NimBLECharacteristic* rx = svc->createCharacteristic(
        NUS_RX,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(&s_rxCbs);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SVC);
    adv->setName(DEVICE_NAME);
    adv->enableScanResponse(true);
    adv->start();

    Serial.printf("BLE advertising as %s (NUS)\n", DEVICE_NAME);
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    ledWrite(false);

    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("Wemos-UART NUS source");
    Serial.println("USB and BLE share the same commands. Type HELP.");

    analogReadResolution(12);
    startBle();
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            handleLine(s_usbLine);
            s_usbLine = "";
        } else if (c != '\r') {
            if (s_usbLine.length() < 200) s_usbLine += c;
        }
    }

    if (s_cmdReady) {
        String cmd = s_pendingCmd;
        s_cmdReady = false;
        handleLine(cmd);
    }

    if (s_welcome && (int32_t)(millis() - s_welcomeAt) >= 0) {
        s_welcome = false;
        reply("READY Wemos-UART - type HELP");
    }

    if (s_stream && millis() - s_lastStream >= s_streamMs) {
        s_lastStream = millis();
        sendStatus();
    }

    delay(5);
}
