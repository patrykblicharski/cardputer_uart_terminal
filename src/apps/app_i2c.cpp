#include "app_i2c.h"
#include "../display/display_ext.h"
#include "../display/display_int.h"
#include "../config.h"
#include "../app_state.h"
#include "i2c_db.h"
#include "i2c_sensors.h"
#include <M5Cardputer.h>
#include <Wire.h>

// ─── Soft I2C scan ────────────────────────────────────────────────────────────
// Uses bit-bang only — never touches Wire or Wire1.
// Wire.end()/Wire.begin() corrupts M5Unified's internal bus state and breaks
// the keyboard permanently (no recovery without full M5Cardputer.begin()).
// Bit-bang on scan pins (G3/G4 by default) leaves GPIO8/9 (keyboard I2C) untouched.

static const struct { uint8_t sda; uint8_t scl; const char* label; } kPins[] = {
    {  3,  4, "G3/G4"        },
    {  4,  3, "G4/G3"        },
    {  6,  3, "G6/G3"        },
    {  8,  9, "G8/G9 (!kbd)" },
};
static constexpr int kPinComboCount = (int)(sizeof(kPins) / sizeof(kPins[0]));

// ─── Layout constants ─────────────────────────────────────────────────────────

static constexpr int ROW_H      = 22;
static constexpr int LIST_TOP_Y = 26;
static constexpr int LIST_BOT_Y = EXT_H - 14;
static constexpr int LIST_VIS   = (LIST_BOT_Y - LIST_TOP_Y) / ROW_H;

static constexpr int DET_HEAD_H  = 22;
static constexpr int DET_LIVE_H  = 62;
static constexpr int DET_SEP_Y   = DET_HEAD_H + DET_LIVE_H;
static constexpr int DET_PARAM_Y = DET_SEP_Y + 2;
static constexpr int DET_FOOT_Y  = EXT_H - 16;
static constexpr int DET_ROW_H   = 19;
static constexpr int DET_VIS     = (DET_FOOT_Y - DET_PARAM_Y) / DET_ROW_H;

// ─── State ────────────────────────────────────────────────────────────────────

enum I2CState { I2C_IDLE, I2C_SCANNING, I2C_RESULTS, I2C_DETAIL };

static constexpr int MAX_FOUND = 24;

static I2CState app_state  = I2C_IDLE;
static int      pin_combo  = 0;

static uint8_t found_addrs[MAX_FOUND];
static int     found_count = 0;
static int     sel_idx     = 0;
static int     scroll_off  = 0;

static SensorDetail det_info;
static int      det_param_sel    = 0;
static int      det_param_scroll = 0;
static uint32_t det_last_read    = 0;
static bool     det_blink        = false;

// ─── External display: header / footer helpers ────────────────────────────────

static void extDrawHeader(const char* left, const char* right) {
    extDisplay.fillRect(0, 0, EXT_W, 22, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(4, 7); extDisplay.print(left);
    if (right && right[0]) {
        extDisplay.setTextColor(TFT_YELLOW, TFT_DARKGREY);
        int tw = extDisplay.textWidth(right);
        extDisplay.setCursor(EXT_W - tw - 4, 7); extDisplay.print(right);
    }
}

static void extDrawFooter(const char* hint) {
    extDisplay.fillRect(0, EXT_H - 14, EXT_W, 14, TFT_BLACK);
    extDisplay.drawFastHLine(0, EXT_H - 14, EXT_W, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(4, EXT_H - 11); extDisplay.print(hint);
}

// ─── External display: idle / scanning / results ──────────────────────────────

static void extDrawIdle() {
    extDisplay.fillScreen(TFT_BLACK);
    extDrawHeader("I2C SCANNER", kPins[pin_combo].label);
    extDisplay.setTextSize(2);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    int tw = extDisplay.textWidth("I2C BUS SCANNER");
    extDisplay.setCursor((EXT_W - tw) / 2, 60); extDisplay.print("I2C BUS SCANNER");
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    extDisplay.setCursor(24, 105);
    extDisplay.printf("SDA: G%d         SCL: G%d", kPins[pin_combo].sda, kPins[pin_combo].scl);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(24, 140); extDisplay.print("Connect device to SDA, SCL,");
    extDisplay.setCursor(24, 152); extDisplay.print("3.3V and GND on ext board.");
    extDisplay.setTextColor(C_DIM, TFT_BLACK);
    extDisplay.setCursor(24, 185); extDisplay.print("FN+1 = Cycle pin pair");
    extDisplay.setCursor(24, 197); extDisplay.print("ENTER = Start scan");
    extDrawFooter("FN+1=Pins  ENTER=Scan  FN+DEL=Menu");
}

static void extDrawScanProgress(int addr) {
    extDisplay.fillRect(0, 120, EXT_W, 50, TFT_BLACK);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    extDisplay.setCursor((EXT_W - extDisplay.textWidth("SCANNING...")) / 2, 125);
    extDisplay.print("SCANNING...");
    char buf[20];
    snprintf(buf, sizeof(buf), "addr 0x%02X / 0x7F", addr);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    int tw = extDisplay.textWidth(buf);
    extDisplay.setCursor((EXT_W - tw) / 2, 138); extDisplay.print(buf);
    int barW = EXT_W - 48, filled = (addr * barW) / 127;
    extDisplay.fillRect(24, 152, filled, 10, C_GREEN);
    extDisplay.drawRect(24, 152, barW, 10, TFT_DARKGREY);
    snprintf(buf, sizeof(buf), "Found: %d", found_count);
    extDisplay.setTextColor(C_BLUE, TFT_BLACK);
    tw = extDisplay.textWidth(buf);
    extDisplay.setCursor((EXT_W - tw) / 2, 168); extDisplay.print(buf);
}

static void extDrawScanScreen() {
    extDisplay.fillScreen(TFT_BLACK);
    extDrawHeader("I2C SCANNER", kPins[pin_combo].label);
    extDisplay.setTextSize(2);
    extDisplay.setTextColor(C_AMBER, TFT_BLACK);
    int tw = extDisplay.textWidth("SCANNING BUS");
    extDisplay.setCursor((EXT_W - tw) / 2, 88); extDisplay.print("SCANNING BUS");
}

static void extDrawResultRow(int listIdx, bool selected) {
    uint8_t addr = found_addrs[listIdx];
    const I2CDeviceInfo* dev = i2cdb_lookup(addr);
    int ry = LIST_TOP_Y + (listIdx - scroll_off) * ROW_H;
    uint16_t bg = selected ? C_DIM : TFT_BLACK;
    extDisplay.fillRect(2, ry, EXT_W - 4, ROW_H - 2, bg);
    if (selected) extDisplay.drawRect(2, ry, EXT_W - 4, ROW_H - 2, C_GREEN);
    int ty = ry + (ROW_H - 2) / 2 - 3;
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_AMBER, bg);
    extDisplay.setCursor(6, ty); extDisplay.printf("0x%02X", addr);
    extDisplay.setTextColor(selected ? C_WHITE : C_GREEN, bg);
    extDisplay.setCursor(44, ty);
    if (dev) { char nb[17]; snprintf(nb, sizeof(nb), "%-16s", dev->name); extDisplay.print(nb); }
    else       extDisplay.print("Unknown device  ");
    extDisplay.setTextColor(C_GRAY, bg);
    extDisplay.setCursor(148, ty);
    if (dev) extDisplay.print(dev->cat);
    else     extDisplay.print("Check datasheet");
}

static void extDrawResults() {
    extDisplay.fillScreen(TFT_BLACK);
    char sub[24];
    snprintf(sub, sizeof(sub), "%d device%s found", found_count, found_count == 1 ? "" : "s");
    extDrawHeader("I2C SCANNER", sub);
    if (found_count == 0) {
        extDisplay.setTextSize(2); extDisplay.setTextColor(C_AMBER, TFT_BLACK);
        int tw = extDisplay.textWidth("NO DEVICES FOUND");
        extDisplay.setCursor((EXT_W - tw) / 2, 70); extDisplay.print("NO DEVICES FOUND");
        extDisplay.setTextSize(1); extDisplay.setTextColor(C_GRAY, TFT_BLACK);
        extDisplay.setCursor(24, 110); extDisplay.print("Check wiring:");
        extDisplay.setCursor(24, 124); extDisplay.print("SDA and SCL connected?");
        extDisplay.setCursor(24, 136); extDisplay.print("3.3V power to device?");
        extDisplay.setCursor(24, 148); extDisplay.print("4.7k pull-ups on SDA+SCL?");
        extDisplay.setCursor(24, 160); extDisplay.print("Correct pin pair selected?");
        extDrawFooter("FN+1=New pins  ENTER=Rescan  FN+DEL=Menu");
        return;
    }
    int visible = min(found_count - scroll_off, LIST_VIS);
    for (int i = 0; i < visible; i++)
        extDrawResultRow(scroll_off + i, (scroll_off + i) == sel_idx);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    if (scroll_off > 0) { extDisplay.setCursor(EXT_W - 12, LIST_TOP_Y); extDisplay.print("^"); }
    if (scroll_off + LIST_VIS < found_count) { extDisplay.setCursor(EXT_W - 12, LIST_BOT_Y - 10); extDisplay.print("v"); }
    extDrawFooter(";=Up  .=Down  ENTER=Detail  FN+R=Rescan  FN+DEL=Menu");
}

// ─── External display: detail view ───────────────────────────────────────────

static void extDrawDetailHeader() {
    uint8_t addr = found_addrs[sel_idx];
    const I2CDeviceInfo* dev = i2cdb_lookup(addr);
    char left[28];
    snprintf(left, sizeof(left), "0x%02X  %s", addr, dev ? dev->name : "Unknown");
    extDrawHeader(left, dev ? dev->cat : "");
}

static void extDrawDetailReadings() {
    extDisplay.fillRect(0, DET_HEAD_H, EXT_W, DET_LIVE_H, TFT_BLACK);
    extDisplay.drawFastHLine(0, DET_HEAD_H, EXT_W, 0x2945);

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_DIM, TFT_BLACK);
    extDisplay.setCursor(4, DET_HEAD_H + 3); extDisplay.print("LIVE");
    uint16_t dotCol = det_blink ? C_GREEN : C_DIM;
    extDisplay.fillCircle(EXT_W - 8, DET_HEAD_H + 6, 4, dotCol);

    if (!det_info.read_ok) {
        extDisplay.setTextColor(C_RED, TFT_BLACK);
        extDisplay.setCursor(4, DET_HEAD_H + 20); extDisplay.print("Read error \x14 check wiring");
        return;
    }

    int n = det_info.num_readings;
    for (int i = 0; i < n && i < SENS_MAX_READINGS; i++) {
        int col = (n > 2) ? i % 2 : 0;
        int row = (n > 2) ? i / 2 : i;
        int x = col == 0 ? 6 : (EXT_W / 2 + 4);
        int y = DET_HEAD_H + 14 + row * 22;
        extDisplay.setTextColor(C_AMBER, TFT_BLACK);
        extDisplay.setCursor(x, y); extDisplay.print(det_info.readings[i].label);
        extDisplay.print(":");
        extDisplay.setTextColor(C_WHITE, TFT_BLACK);
        extDisplay.setCursor(x, y + 10); extDisplay.print(det_info.readings[i].value);
    }

    extDisplay.drawFastHLine(0, DET_SEP_Y, EXT_W, TFT_DARKGREY);
}

static void extDrawDetailParams() {
    extDisplay.fillRect(0, DET_PARAM_Y, EXT_W, DET_FOOT_Y - DET_PARAM_Y, TFT_BLACK);

    if (det_info.num_params == 0) {
        extDisplay.setTextSize(1);
        extDisplay.setTextColor(C_DIM, TFT_BLACK);
        extDisplay.setCursor(6, DET_PARAM_Y + 8);
        extDisplay.print("No adjustable parameters");
        return;
    }

    int vis = min(det_info.num_params - det_param_scroll, DET_VIS);
    for (int i = 0; i < vis; i++) {
        int pidx = det_param_scroll + i;
        bool sel = (pidx == det_param_sel);
        auto& p = det_info.params[pidx];
        int ry = DET_PARAM_Y + i * DET_ROW_H;
        uint16_t bg = sel ? C_DIM : TFT_BLACK;
        extDisplay.fillRect(2, ry, EXT_W - 4, DET_ROW_H - 1, bg);
        if (sel) extDisplay.drawRect(2, ry, EXT_W - 4, DET_ROW_H - 1, C_GREEN);
        int ty = ry + (DET_ROW_H - 1) / 2 - 3;
        extDisplay.setTextSize(1);
        extDisplay.setTextColor(sel ? C_GREEN : C_DIM, bg);
        extDisplay.setCursor(4, ty); extDisplay.print(sel ? ">" : " ");
        extDisplay.setTextColor(sel ? C_WHITE : C_GRAY, bg);
        extDisplay.setCursor(14, ty); extDisplay.print(p.name);
        if (p.num_vals && !p.ro) {
            char vbuf[16];
            snprintf(vbuf, sizeof(vbuf), "[%s]", p.vals[p.idx]);
            extDisplay.setTextColor(sel ? C_AMBER : C_GREEN, bg);
            int vw = extDisplay.textWidth(vbuf);
            extDisplay.setCursor(EXT_W - vw - 6, ty); extDisplay.print(vbuf);
        } else if (p.ro) {
            extDisplay.setTextColor(C_DIM, bg);
            extDisplay.setCursor(EXT_W - 40, ty); extDisplay.print("[ro]");
        }
    }

    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    if (det_param_scroll > 0) { extDisplay.setCursor(EXT_W - 10, DET_PARAM_Y); extDisplay.print("^"); }
    if (det_param_scroll + DET_VIS < det_info.num_params) { extDisplay.setCursor(EXT_W - 10, DET_FOOT_Y - 10); extDisplay.print("v"); }
}

static void extDrawDetailFooter() {
    extDisplay.fillRect(0, DET_FOOT_Y, EXT_W, EXT_H - DET_FOOT_Y, TFT_BLACK);
    extDisplay.drawFastHLine(0, DET_FOOT_Y, EXT_W, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(4, DET_FOOT_Y + 3);
    extDisplay.print(";/.=Nav  [/]=Chg  DEL=Back  FN+DEL=Menu");
}

static void extDrawDetail() {
    extDisplay.fillScreen(TFT_BLACK);
    extDrawDetailHeader();
    extDrawDetailReadings();
    extDrawDetailParams();
    extDrawDetailFooter();
}

// ─── Internal display ─────────────────────────────────────────────────────────

static void intDrawIdle() {
    intSprite.fillScreen(TFT_BLACK);
    intSprite.fillRect(0, 0, INT_W, 18, TFT_DARKGREY);
    intSprite.setTextSize(1); intSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    intSprite.setCursor(4, 5); intSprite.print("I2C SCANNER \x14 CONFIG");
    intSprite.setTextSize(2); intSprite.setTextColor(C_GREEN, TFT_BLACK);
    intSprite.setCursor(4, 30); intSprite.printf("SDA: G%d", kPins[pin_combo].sda);
    intSprite.setCursor(4, 54); intSprite.printf("SCL: G%d", kPins[pin_combo].scl);
    intSprite.setTextSize(1); intSprite.setTextColor(C_GRAY, TFT_BLACK);
    intSprite.setCursor(4, 90);  intSprite.print("FN+1 = Cycle pin pair");
    intSprite.setCursor(4, 102); intSprite.print("ENTER = Start scan");
    intSprite.setCursor(4, 114); intSprite.print("FN+DEL = Back to menu");
    intSprite.pushSprite(0, 0);
}

static void intDrawScanning() {
    intSprite.fillScreen(TFT_BLACK);
    intSprite.fillRect(0, 0, INT_W, 18, TFT_DARKGREY);
    intSprite.setTextSize(1); intSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    intSprite.setCursor(4, 5); intSprite.print("I2C SCANNER \x14 SCANNING");
    intSprite.setTextSize(2); intSprite.setTextColor(C_AMBER, TFT_BLACK);
    intSprite.setCursor(4, 50); intSprite.print("Scanning...");
    intSprite.pushSprite(0, 0);
}

static void intDrawDeviceDetail() {
    intSprite.fillScreen(TFT_BLACK);
    intSprite.fillRect(0, 0, INT_W, 18, TFT_DARKGREY);
    intSprite.setTextSize(1); intSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    intSprite.setCursor(4, 5);
    if (found_count == 0) {
        intSprite.print("I2C SCANNER \x14 NO DEVICES");
        intSprite.setTextColor(C_GRAY, TFT_BLACK);
        intSprite.setCursor(4, 30); intSprite.print("No I2C devices responded.");
        intSprite.setCursor(4, 42); intSprite.print("Verify wiring and power.");
        intSprite.pushSprite(0, 0); return;
    }
    intSprite.printf("I2C SCANNER \x14 %d FOUND", found_count);
    uint8_t addr = found_addrs[sel_idx];
    const I2CDeviceInfo* dev = i2cdb_lookup(addr);
    intSprite.setTextSize(2); intSprite.setTextColor(C_AMBER, TFT_BLACK);
    intSprite.setCursor(4, 24); intSprite.printf("0x%02X", addr);
    if (dev) {
        intSprite.setTextColor(C_WHITE, TFT_BLACK); intSprite.setCursor(52, 24); intSprite.print(dev->name);
        intSprite.drawFastHLine(0, 46, INT_W, TFT_DARKGREY);
        intSprite.setTextSize(1); intSprite.setTextColor(C_GREEN, TFT_BLACK);
        intSprite.setCursor(4, 52); intSprite.printf("Type: %s", dev->cat);
        if (dev->note[0]) { intSprite.setTextColor(C_GRAY, TFT_BLACK); intSprite.setCursor(4, 66); intSprite.print(dev->note); }
        intSprite.setTextColor(C_DIM, TFT_BLACK);
        intSprite.setCursor(4, 82); intSprite.printf("[%d/%d]  ENTER=Details", sel_idx + 1, found_count);
    } else {
        intSprite.setTextColor(C_GRAY, TFT_BLACK); intSprite.setCursor(52, 24); intSprite.print("Unknown device");
        intSprite.drawFastHLine(0, 46, INT_W, TFT_DARKGREY);
        intSprite.setTextSize(1); intSprite.setTextColor(C_GRAY, TFT_BLACK);
        intSprite.setCursor(4, 52); intSprite.print("Not in database.");
        intSprite.setCursor(4, 64); intSprite.print("ENTER=raw registers");
    }
    intSprite.pushSprite(0, 0);
}

static void intDrawSensorDetail() {
    intSprite.fillScreen(TFT_BLACK);
    intSprite.fillRect(0, 0, INT_W, 18, TFT_DARKGREY);
    intSprite.setTextSize(1); intSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    uint8_t addr = found_addrs[sel_idx];
    intSprite.setCursor(4, 5); intSprite.printf("0x%02X \x14 SENSOR DETAIL", addr);

    if (det_info.num_params == 0) {
        intSprite.setTextColor(C_GRAY, TFT_BLACK);
        intSprite.setCursor(4, 26); intSprite.print(det_info.note);
        intSprite.setCursor(4, 40); intSprite.print("No adjustable params.");
        intSprite.setCursor(4, 54); intSprite.print("DEL = back to list");
        intSprite.pushSprite(0, 0); return;
    }

    auto& p = det_info.params[det_param_sel];
    intSprite.setTextColor(C_GREEN, TFT_BLACK);
    intSprite.setCursor(4, 24); intSprite.print(p.name);
    if (p.num_vals && !p.ro) {
        intSprite.setTextSize(2); intSprite.setTextColor(C_AMBER, TFT_BLACK);
        intSprite.setCursor(4, 42); intSprite.print(p.vals[p.idx]);
        intSprite.setTextSize(1); intSprite.setTextColor(C_DIM, TFT_BLACK);
        char avail[48] = ""; int off = 0;
        for (int i = 0; i < p.num_vals && off < (int)sizeof(avail) - 4; i++)
            off += snprintf(avail + off, sizeof(avail) - off, i == p.idx ? "[%s] " : "%s ", p.vals[i]);
        intSprite.setCursor(4, 68); intSprite.print(avail);
        intSprite.setTextColor(C_GRAY, TFT_BLACK);
        intSprite.setCursor(4, 82); intSprite.print("[/]=Change  ;/.=Navigate");
        intSprite.setCursor(4, 96); intSprite.print("DEL=back to list");
    } else {
        intSprite.setTextColor(C_DIM, TFT_BLACK);
        intSprite.setCursor(4, 42); intSprite.print("(read only)");
    }
    intSprite.setTextColor(C_DIM, TFT_BLACK);
    intSprite.setCursor(4, 118); intSprite.print(det_info.note);
    intSprite.pushSprite(0, 0);
}

// ─── Soft I2C bit-bang primitives ────────────────────────────────────────────

static inline void sdaSet(uint8_t pin, bool hi) {
    if (hi) pinMode(pin, INPUT_PULLUP);
    else { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
}
static inline void sclSet(uint8_t pin, bool hi) {
    if (hi) { pinMode(pin, INPUT_PULLUP); uint32_t t = micros(); while (!digitalRead(pin) && micros() - t < 5000) {} }
    else { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
}

static bool softProbe(uint8_t sda, uint8_t scl, uint8_t addr) {
    sdaSet(sda, true); sclSet(scl, true); delayMicroseconds(4);
    sdaSet(sda, false); delayMicroseconds(4);
    sclSet(scl, false); delayMicroseconds(4);
    uint8_t b = (uint8_t)(addr << 1);
    for (int i = 7; i >= 0; i--) {
        sdaSet(sda, (b >> i) & 1); delayMicroseconds(2);
        sclSet(scl, true); delayMicroseconds(4);
        sclSet(scl, false); delayMicroseconds(2);
    }
    sdaSet(sda, true); delayMicroseconds(2);
    sclSet(scl, true); delayMicroseconds(4);
    bool ack = (digitalRead(sda) == LOW);
    sclSet(scl, false); delayMicroseconds(2);
    sdaSet(sda, false); delayMicroseconds(2);
    sclSet(scl, true); delayMicroseconds(4);
    sdaSet(sda, true); delayMicroseconds(4);
    return ack;
}

// ─── Scan ─────────────────────────────────────────────────────────────────────

static void doScan() {
    app_state = I2C_SCANNING; found_count = 0; sel_idx = 0; scroll_off = 0;
    extDrawScanScreen(); intDrawScanning();
    uint8_t sda = kPins[pin_combo].sda, scl = kPins[pin_combo].scl;
    sdaSet(sda, true); sclSet(scl, true); delayMicroseconds(200);
    for (int addr = 1; addr < 128; addr++) {
        if (softProbe(sda, scl, (uint8_t)addr) && found_count < MAX_FOUND)
            found_addrs[found_count++] = (uint8_t)addr;
        if ((addr & 0x0F) == 0) extDrawScanProgress(addr);
    }
    pinMode(sda, INPUT); pinMode(scl, INPUT);
    app_state = I2C_RESULTS;
    extDrawResults(); intDrawDeviceDetail();
}

// ─── Detail view enter / exit ─────────────────────────────────────────────────

static void enterDetail() {
    app_state = I2C_DETAIL;
    det_param_sel = 0; det_param_scroll = 0; det_blink = false;
    Wire1.begin(kPins[pin_combo].sda, kPins[pin_combo].scl, 100000UL);
    sensor_init(found_addrs[sel_idx], Wire1, det_info);
    sensor_read(found_addrs[sel_idx], Wire1, det_info);
    det_last_read = millis();
    extDrawDetail();
    intDrawSensorDetail();
}

static void exitDetail() {
    Wire1.end();
    app_state = I2C_RESULTS;
    extDrawResults(); intDrawDeviceDetail();
}

// ─── Keyboard ─────────────────────────────────────────────────────────────────

static void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.fn && st.del) {
        if (app_state == I2C_DETAIL) Wire1.end();
        pinMode(kPins[pin_combo].sda, INPUT);
        pinMode(kPins[pin_combo].scl, INPUT);
        transitionTo(STATE_MENU);
        return;
    }

    if (st.fn) {
        for (auto c : st.word) {
            if (c == '1') {
                pin_combo = (pin_combo + 1) % kPinComboCount;
                if (app_state == I2C_IDLE) { extDrawIdle(); intDrawIdle(); }
            }
            if (c == 'r' || c == 'R') {
                if (app_state == I2C_RESULTS) doScan();
            }
        }
        return;
    }

    switch (app_state) {
    case I2C_IDLE:
        if (st.enter) doScan();
        break;

    case I2C_RESULTS:
        if (st.enter && found_count > 0) { enterDetail(); return; }
        for (auto c : st.word) {
            bool changed = false;
            if      (c == ';' && sel_idx > 0)               { sel_idx--; changed = true; }
            else if (c == '.' && sel_idx < found_count - 1) { sel_idx++; changed = true; }
            if (changed) {
                if (sel_idx < scroll_off) scroll_off = sel_idx;
                if (sel_idx >= scroll_off + LIST_VIS) scroll_off = sel_idx - LIST_VIS + 1;
                extDrawResults(); intDrawDeviceDetail();
            }
        }
        break;

    case I2C_DETAIL:
        if (st.del) { exitDetail(); return; }
        for (auto c : st.word) {
            if (c == ';' && det_param_sel > 0) {
                det_param_sel--;
                if (det_param_sel < det_param_scroll) det_param_scroll = det_param_sel;
                extDrawDetailParams(); intDrawSensorDetail();
            } else if (c == '.' && det_param_sel < det_info.num_params - 1) {
                det_param_sel++;
                if (det_param_sel >= det_param_scroll + DET_VIS) det_param_scroll = det_param_sel - DET_VIS + 1;
                extDrawDetailParams(); intDrawSensorDetail();
            } else if (c == '[' && det_info.num_params > 0) {
                sensor_set_param(found_addrs[sel_idx], Wire1, det_info, det_param_sel, -1);
                extDrawDetailParams(); intDrawSensorDetail();
            } else if (c == ']' && det_info.num_params > 0) {
                sensor_set_param(found_addrs[sel_idx], Wire1, det_info, det_param_sel, +1);
                extDrawDetailParams(); intDrawSensorDetail();
            }
        }
        break;

    default: break;
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void i2cApp_Setup() {
    app_state = I2C_IDLE;
    found_count = 0; sel_idx = 0; scroll_off = 0;
    extDrawIdle(); intDrawIdle();
}

void i2cApp_Loop() {
    if (g_session.forceRedraw) {
        switch (app_state) {
            case I2C_IDLE:    extDrawIdle();    intDrawIdle();          break;
            case I2C_RESULTS: extDrawResults(); intDrawDeviceDetail();  break;
            case I2C_DETAIL:  extDrawDetail();  intDrawSensorDetail();  break;
            default: break;
        }
        g_session.forceRedraw = false;
    }
    handleKeyboard();

    if (app_state == I2C_DETAIL) {
        uint32_t now = millis();
        if (now - det_last_read >= 500) {
            det_last_read = now;
            det_blink = !det_blink;
            sensor_read(found_addrs[sel_idx], Wire1, det_info);
            extDrawDetailReadings();
        }
    }
}
