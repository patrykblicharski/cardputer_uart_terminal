/*
 * app_lora.cpp — LoRa channel application for M5Stack Cardputer
 *
 * Communicates with a Heltec WiFi LoRa 32 v2 over Serial1 (UART, G1/G2).
 *
 * External ILI9341 layout:
 *   Topbar   (0..12px)    : "LoRa CHANNEL" | local node ID | SF | MHz
 *   Terminal (14..186px)  : scrolling RX/TX log
 *   Target   (187..225px) : TO:[target]  keyboard hints
 *   Input    (226..239px) : |> typed buffer_
 *
 * Internal ST7789 (240x135): node ID, target, SF/freq, last RX RSSI/SNR
 *
 * FN shortcuts:
 *   FN+1  = cycle send target (ALL, known nodes)
 *   FN+2  = request REPORT from target
 *   FN+3  = toggle MAINT on target
 *   FN+4  = request local STATUS
 *   FN+5  = toggle local MAINT
 *   FN+6  = cycle SF (7..12)
 *   FN+7  = cycle frequency preset
 *   FN+DEL= exit to menu
 */

#include "app_lora.h"
#include "../display/display_ext.h"
#include "../display/display_int.h"
#include "../config.h"
#include "../app_state.h"
#include <M5Cardputer.h>

// ─── Layout (LoRa-specific — shorter terminal to fit target bar) ──────────────

static constexpr int LORA_CMDBAR_H = 38;
static constexpr int LORA_CMDBAR_Y = EXT_H - LORA_CMDBAR_H - INPUT_H - 1;  // 187
static constexpr int LORA_TERM_H   = LORA_CMDBAR_Y - TERM_Y;                 // 173

// ─── UART config ──────────────────────────────────────────────────────────────

static constexpr uint32_t LORA_BAUD = 115200;

// ─── Frequency presets ────────────────────────────────────────────────────────

static constexpr long FREQ_PRESETS[] = { 433100000L, 433500000L, 434000000L };
static constexpr int  FREQ_COUNT     = 3;

// ─── State ────────────────────────────────────────────────────────────────────

static constexpr int MAX_NODES = 8;

static String   input_buf;
static String   uart_line_buf;
static bool     int_dirty   = true;

static char     local_id[16]    = "---";
static char     local_sf_str[4] = "?";
static char     local_mhz[10]   = "?";

static char     known_nodes[MAX_NODES][16];
static int      known_count = 0;
static int      target_idx  = 0;
static bool     maint_state[MAX_NODES];
static bool     local_maint = false;

static int      last_rssi = 0;
static float    last_snr  = 0.0f;
static char     last_from[16] = "---";

static int      cur_sf_idx   = 2;
static int      cur_freq_idx = 1;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static const char* currentTarget() {
    return (target_idx == 0) ? "ALL" : known_nodes[target_idx - 1];
}

static void addKnownNode(const char* id) {
    if (strcmp(id, local_id) == 0) return;
    for (int i = 0; i < known_count; i++)
        if (strcmp(known_nodes[i], id) == 0) return;
    if (known_count < MAX_NODES) {
        strncpy(known_nodes[known_count], id, 15);
        known_nodes[known_count][15] = '\0';
        known_count++;
    }
}

static void setMaintState(const char* nodeId, bool on) {
    for (int i = 0; i < known_count; i++)
        if (strcmp(known_nodes[i], nodeId) == 0) { maint_state[i] = on; return; }
}

static void sendToHeltec(const char* line) { Serial1.println(line); }

// ─── Terminal print ───────────────────────────────────────────────────────────

static void termPrint(const char* text, uint16_t color) {
    terminal.setTextColor(color, TFT_BLACK);
    terminal.println(text);
}

// ─── Chrome draw ──────────────────────────────────────────────────────────────

static void loraDrawTopbar() {
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    extDisplay.print("LoRa CHANNEL");
    char buf[40];
    snprintf(buf, sizeof(buf), "%s  SF%s  %sMHz", local_id, local_sf_str, local_mhz);
    extDisplay.setTextColor(TFT_YELLOW, TFT_DARKGREY);
    int tw = extDisplay.textWidth(buf);
    extDisplay.setCursor(EXT_W - tw - 3, 3);
    extDisplay.print(buf);
}

static void loraDrawTargetBar() {
    static constexpr uint16_t BG = 0x1082u;
    extDisplay.fillRect(0, LORA_CMDBAR_Y, EXT_W, LORA_CMDBAR_H, BG);
    extDisplay.drawFastHLine(0, LORA_CMDBAR_Y, EXT_W, TFT_DARKGREY);
    extDisplay.setTextSize(1);

    extDisplay.setTextColor(C_GRAY, BG);
    extDisplay.setCursor(4, LORA_CMDBAR_Y + 4);
    extDisplay.print("TO:");
    char tgt[20];
    snprintf(tgt, sizeof(tgt), "[%s]", currentTarget());
    extDisplay.setTextColor(C_AMBER, BG);
    extDisplay.print(tgt);

    if (target_idx > 0 && maint_state[target_idx - 1]) {
        extDisplay.setTextColor(C_GREEN, BG);
        extDisplay.print(" MAINT-ON");
    }

    extDisplay.setTextColor(C_DIM, BG);
    extDisplay.setCursor(4, LORA_CMDBAR_Y + 20);
    extDisplay.print("1=Tgt 2=Rpt 3=Maint 4=Stat 5=LcMaint 6=SF 7=Frq  FN+DEL=Exit");
}

static void loraDrawInputLine() {
    extDisplay.fillRect(0, INPUT_Y, EXT_W, INPUT_H, TFT_BLACK);
    extDisplay.drawFastHLine(0, INPUT_Y - 1, EXT_W, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    extDisplay.setCursor(3, INPUT_Y + 3);
    extDisplay.print("|> ");
    extDisplay.setTextColor(C_WHITE, TFT_BLACK);
    String disp = input_buf;
    while (disp.length() > 1 && extDisplay.textWidth(disp.c_str()) > EXT_W - 46)
        disp.remove(0, 1);
    extDisplay.print(disp);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    extDisplay.print("_");
}

static void loraPushAll() {
    terminal.pushSprite(TERM_X, TERM_Y);
    loraDrawTargetBar();
    loraDrawInputLine();
}

// ─── Internal display ─────────────────────────────────────────────────────────

static void intLoraStatus_Draw() {
    intSprite.fillSprite(C_BG);
    intSprite.setTextSize(1);

    intSprite.setTextColor(C_DIM, C_BG);
    intSprite.setCursor(4, 4);
    intSprite.print("LoRa CHANNEL");

    intSprite.setTextColor(C_GREEN, C_BG);
    intSprite.setTextSize(2);
    intSprite.setCursor(4, 18);
    intSprite.print(local_id);

    intSprite.setTextSize(1);
    char line[48];

    intSprite.setTextColor(C_AMBER, C_BG);
    intSprite.setCursor(4, 42);
    snprintf(line, sizeof(line), "TO: %s", currentTarget());
    intSprite.print(line);

    intSprite.setTextColor(C_DIM, C_BG);
    intSprite.setCursor(4, 55);
    snprintf(line, sizeof(line), "SF%s  %sMHz", local_sf_str, local_mhz);
    intSprite.print(line);

    intSprite.setTextColor(C_GREEN, C_BG);
    intSprite.setCursor(4, 70);
    snprintf(line, sizeof(line), "RX: %s", last_from);
    intSprite.print(line);

    intSprite.setTextColor(C_DIM, C_BG);
    intSprite.setCursor(4, 83);
    snprintf(line, sizeof(line), "RSSI:%ddBm  SNR:%.1f", last_rssi, last_snr);
    intSprite.print(line);

    intSprite.setCursor(4, 96);
    if (local_maint) {
        intSprite.setTextColor(C_GREEN, C_BG);
        intSprite.print("LOCAL MAINT ON");
    } else {
        intSprite.setTextColor(C_DIM, C_BG);
        intSprite.print("local maint off");
    }

    intSprite.pushSprite(0, 0);
    int_dirty = false;
}

// ─── UART line parser ─────────────────────────────────────────────────────────

static void parseHeltecLine(const String& line) {
    if (line.startsWith("BOOT:")) {
        int c = line.indexOf(':', 5);
        String id  = (c > 0) ? line.substring(5, c)  : line.substring(5);
        String ver = (c > 0) ? line.substring(c + 1) : "";
        strncpy(local_id, id.c_str(), 15); local_id[15] = '\0';
        char buf[48];
        snprintf(buf, sizeof(buf), "BOOT: %s v%s", id.c_str(), ver.c_str());
        termPrint(buf, C_GREEN);
        loraPushAll();
        int_dirty = true;
        loraDrawTopbar();

    } else if (line.startsWith("RX:")) {
        String s = line.substring(3);
        int c1 = s.indexOf(':');
        int c2 = s.indexOf(':', c1 + 1);
        int c3 = s.indexOf(':', c2 + 1);
        int c4 = s.indexOf(':', c3 + 1);
        if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) return;
        String from    = s.substring(0, c1);
        int    rssi    = s.substring(c1 + 1, c2).toInt();
        float  snr     = s.substring(c2 + 1, c3).toFloat();
        String type    = s.substring(c3 + 1, c4);
        String payload = s.substring(c4 + 1);
        addKnownNode(from.c_str());
        strncpy(last_from, from.c_str(), 15); last_from[15] = '\0';
        last_rssi = rssi;
        last_snr  = snr;
        char buf[250];
        if (type == "MSG") {
            snprintf(buf, sizeof(buf), "[RX<-%s %ddBm] %s", from.c_str(), rssi, payload.c_str());
            termPrint(buf, C_GREEN);
        } else if (type == "RPT") {
            snprintf(buf, sizeof(buf), "[RPT<-%s] %s", from.c_str(), payload.c_str());
            termPrint(buf, C_BLUE);
            if (payload.indexOf("MAINT=ON")  >= 0) setMaintState(from.c_str(), true);
            if (payload.indexOf("MAINT=OFF") >= 0) setMaintState(from.c_str(), false);
        } else if (type == "ACK") {
            snprintf(buf, sizeof(buf), "[ACK<-%s] %s", from.c_str(), payload.c_str());
            termPrint(buf, C_DIM);
            if (strcmp(payload.c_str(), "MAINT:ON")  == 0) setMaintState(from.c_str(), true);
            if (strcmp(payload.c_str(), "MAINT:OFF") == 0) setMaintState(from.c_str(), false);
        } else {
            snprintf(buf, sizeof(buf), "[%s<-%s] %s", type.c_str(), from.c_str(), payload.c_str());
            termPrint(buf, C_GRAY);
        }
        loraPushAll();
        int_dirty = true;

    } else if (line.startsWith("TX:")) {
        if (line.substring(3) == "FAIL") termPrint("  TX FAIL", C_RED);
        else                             termPrint("  sent",    C_DIM);
        loraPushAll();

    } else if (line.startsWith("INFO:")) {
        int c = line.indexOf(':', 5);
        if (c < 0) return;
        String key = line.substring(5, c);
        String val = line.substring(c + 1);
        char buf[64];
        snprintf(buf, sizeof(buf), "  %s: %s", key.c_str(), val.c_str());
        termPrint(buf, C_GRAY);
        loraPushAll();
        if      (key == "SF")   { strncpy(local_sf_str, val.c_str(), 3); local_sf_str[3] = '\0'; loraDrawTopbar(); }
        else if (key == "FREQ") { long hz = val.toInt(); snprintf(local_mhz, sizeof(local_mhz), "%.1f", hz / 1e6f); loraDrawTopbar(); }
        else if (key == "MAINT"){ local_maint = (val == "ON"); }
        else if (key == "ID")   { strncpy(local_id, val.c_str(), 15); local_id[15] = '\0'; loraDrawTopbar(); }
        int_dirty = true;

    } else if (line.startsWith("HB:")) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[HB] %s", line.substring(3).c_str());
        termPrint(buf, C_DIM);
        loraPushAll();
    }
}

// ─── FN shortcuts ─────────────────────────────────────────────────────────────

static void handleFn(char key) {
    switch (key) {
    case '1':
        target_idx = (target_idx + 1) % (known_count + 1);
        loraDrawTargetBar();
        int_dirty = true;
        break;

    case '2': {
        char cmd[32], buf[48];
        snprintf(cmd, sizeof(cmd), "CMD:%s:REPORT", currentTarget());
        sendToHeltec(cmd);
        snprintf(buf, sizeof(buf), "[CMD] REPORT -> %s", currentTarget());
        termPrint(buf, C_AMBER); loraPushAll();
        break;
    }

    case '3': {
        bool newOn = (target_idx > 0) ? !maint_state[target_idx - 1] : true;
        char cmd[48], buf[56];
        snprintf(cmd, sizeof(cmd), "CMD:%s:MAINT:%s", currentTarget(), newOn ? "ON" : "OFF");
        sendToHeltec(cmd);
        snprintf(buf, sizeof(buf), "[CMD] MAINT:%s -> %s", newOn ? "ON" : "OFF", currentTarget());
        termPrint(buf, C_AMBER); loraPushAll();
        break;
    }

    case '4':
        sendToHeltec("STATUS");
        termPrint("[CMD] STATUS", C_DIM); loraPushAll();
        break;

    case '5':
        local_maint = !local_maint;
        sendToHeltec(local_maint ? "MAINT:ON" : "MAINT:OFF");
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "[LOCAL] MAINT:%s", local_maint ? "ON" : "OFF");
            termPrint(buf, C_AMBER); loraPushAll();
        }
        int_dirty = true;
        break;

    case '6': {
        cur_sf_idx = (cur_sf_idx + 1) % 6;
        int sf = 7 + cur_sf_idx;
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "SET:SF:%d", sf);
        sendToHeltec(cmd);
        snprintf(local_sf_str, sizeof(local_sf_str), "%d", sf);
        loraDrawTopbar();
        int_dirty = true;
        break;
    }

    case '7': {
        cur_freq_idx = (cur_freq_idx + 1) % FREQ_COUNT;
        char cmd[28];
        snprintf(cmd, sizeof(cmd), "SET:FREQ:%ld", FREQ_PRESETS[cur_freq_idx]);
        sendToHeltec(cmd);
        snprintf(local_mhz, sizeof(local_mhz), "%.1f", FREQ_PRESETS[cur_freq_idx] / 1e6f);
        loraDrawTopbar();
        int_dirty = true;
        break;
    }
    }
}

// ─── Keyboard ─────────────────────────────────────────────────────────────────

static void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.fn && st.del) {
        Serial1.end();
        transitionTo(STATE_MENU);
        return;
    }

    if (st.fn) {
        for (auto c : st.word) handleFn(c);
        return;
    }

    if (st.enter) {
        if (input_buf.length() == 0) return;
        char cmd[270], echo[280];
        snprintf(cmd,  sizeof(cmd),  "SEND:%s:%s", currentTarget(), input_buf.c_str());
        snprintf(echo, sizeof(echo), "[TX->%s] %s", currentTarget(), input_buf.c_str());
        sendToHeltec(cmd);
        termPrint(echo, C_AMBER);
        input_buf = "";
        loraPushAll();
        return;
    }

    if (st.del) {
        if (input_buf.length() > 0) {
            input_buf.remove(input_buf.length() - 1);
            loraDrawInputLine();
        }
        return;
    }

    for (auto c : st.word) {
        if (c >= 0x20 && c < 0x7F && input_buf.length() < 200)
            input_buf += c;
    }
    loraDrawInputLine();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void loraApp_Setup() {
    int_dirty     = true;
    input_buf     = "";
    uart_line_buf = "";
    known_count   = 0;
    target_idx    = 0;
    local_maint   = false;
    last_rssi     = 0;
    last_snr      = 0.0f;
    cur_sf_idx    = 2;
    cur_freq_idx  = 1;
    memset(maint_state, 0, sizeof(maint_state));
    memcpy(local_id,     "---", 4);
    memcpy(local_sf_str, "?",   2);
    memcpy(local_mhz,    "?",   2);
    memcpy(last_from,    "---", 4);

    Serial1.begin(LORA_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    // Resize terminal sprite to fit target bar below it
    terminal.deleteSprite();
    terminal.setColorDepth(16);
    terminal.createSprite(TERM_W, LORA_TERM_H);
    terminal.fillScreen(TFT_BLACK);
    terminal.setTextColor(C_GREEN, TFT_BLACK);
    terminal.setTextScroll(true);
    terminal.setTextWrap(true, true);
    terminal.setTextSize(1);
    terminal.setCursor(0, 0);

    extDisplay.fillScreen(TFT_BLACK);
    loraDrawTopbar();
    extDisplay.drawFastHLine(0, TOP_H, EXT_W, TFT_DARKGREY);

    loraPushAll();

    sendToHeltec("STATUS");
}

void loraApp_Loop() {
    if (g_session.forceRedraw) { loraDrawTopbar(); loraPushAll(); int_dirty = true; g_session.forceRedraw = false; }
    handleKeyboard();

    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n' || c == '\r') {
            uart_line_buf.trim();
            if (uart_line_buf.length()) parseHeltecLine(uart_line_buf);
            uart_line_buf = "";
        } else if (uart_line_buf.length() < 250) {
            uart_line_buf += c;
        }
    }

    if (int_dirty) intLoraStatus_Draw();
}
