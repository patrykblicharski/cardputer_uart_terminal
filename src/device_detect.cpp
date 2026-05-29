#include "device_detect.h"
#include "app_state.h"
#include "device_config.h"
#include "display_ext.h"
#include "display_int.h"
#include "config.h"
#include <M5Cardputer.h>

static constexpr int    MAX_ATTEMPTS  = 5;
static constexpr unsigned long HELLO_INTERVAL = 1000;

static int           s_attempt   = 0;
static unsigned long s_lastHello = 0;
static bool          s_waitIdent = false; // received "HELLO SIR", expecting ident line
static String        s_rxBuf;
static bool          s_done      = false;
static bool          s_found     = false;

static void drawStatus(const char* line1, const char* line2 = nullptr) {
    extDisplay.fillRect(0, TOP_H + 1, EXT_W, EXT_H - TOP_H - 1, TFT_BLACK);

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    extDisplay.setCursor(3, 60);
    extDisplay.print("Wykrywanie hosta...");

    // Progress dots
    extDisplay.setCursor(3, 80);
    extDisplay.setTextColor(C_AMBER, TFT_BLACK);
    char pbuf[32];
    snprintf(pbuf, sizeof(pbuf), "Proba %d/%d", s_attempt, MAX_ATTEMPTS);
    extDisplay.print(pbuf);

    extDisplay.setCursor(3, 100);
    extDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    extDisplay.print(line1);
    if (line2) {
        extDisplay.setCursor(3, 115);
        extDisplay.print(line2);
    }

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(TFT_DARKGREY, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 2);
    extDisplay.print("CTRL+[ = wstecz   ENTER = kontynuuj");
}

static void drawTopbar() {
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    extDisplay.print("WYKRYWANIE HOSTA");
}

void detect_Setup() {
    s_attempt   = 0;
    s_lastHello = 0;
    s_waitIdent = false;
    s_rxBuf     = "";
    s_done      = false;
    s_found     = false;

    Serial1.begin(g_session.configLoaded ? g_session.config.baud : 115200,
                  SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    extDisplay.fillScreen(TFT_BLACK);
    drawTopbar();
    drawStatus("Wysylanie HELLO...");

    intSprite.fillScreen(TFT_BLACK);
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    intSprite.setTextSize(1);
    intSprite.setCursor(4, 4);
    intSprite.print("WYKRYWANIE HOSTA");
    intSprite.pushSprite(0, 0);
}

static void processIdent(const String& line) {
    DeviceIdentity ident;
    if (!deviceConfig_ParseIdentity(line, ident)) {
        drawStatus("Nieprawidlowa odpowiedz", line.c_str());
        s_done = true; s_found = false;
        return;
    }
    g_session.deviceIdentified = true;
    // Try to load config from SD
    String err;
    if (deviceConfig_LoadByUid(ident.uid, g_session.config, err)) {
        g_session.configLoaded = true;
    } else {
        // Build minimal config from ident
        g_session.config = DeviceConfig{};
        g_session.config.deviceName = ident.deviceName;
        g_session.config.uid        = ident.uid;
        g_session.config.typeId     = ident.typeId;
        g_session.config.maintType  = ident.maintType;
        g_session.configLoaded      = false;
    }
    drawStatus(("Znaleziono: " + ident.deviceName).c_str(),
               ("UID: " + ident.uid).c_str());
    s_done = true; s_found = true;
}

void detect_Loop() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) goto read_serial;
    {
        auto& st = M5Cardputer.Keyboard.keysState();
        if (st.ctrl) {
            for (auto c : st.word) {
                if (c == '[') { transitionTo(STATE_MENU); return; }
            }
        }
        if (st.enter && s_done) {
            transitionTo(STATE_TERMINAL);
            return;
        }
    }

read_serial:
    // Drain Serial1 into line buffer
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            s_rxBuf.trim();
            if (!s_waitIdent) {
                if (s_rxBuf == "HELLO SIR") {
                    s_waitIdent = true;
                    drawStatus("HELLO SIR odebrane", "Czekam na identyfikacje...");
                }
            } else {
                processIdent(s_rxBuf);
            }
            s_rxBuf = "";
        } else if (c != '\r') {
            s_rxBuf += c;
        }
    }

    if (s_done) return;

    unsigned long now = millis();
    if (s_lastHello == 0 || now - s_lastHello >= HELLO_INTERVAL) {
        if (s_attempt >= MAX_ATTEMPTS) {
            drawStatus("Brak odpowiedzi", "Urzadzenie nie wykryte");
            s_done = true; s_found = false;
            return;
        }
        s_attempt++;
        s_lastHello = now;
        Serial1.print("HELLO\r\n");
        drawStatus("Wysylanie HELLO...");
    }
}
