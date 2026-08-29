/*
 * app_ble_uart.cpp — BLE UART client for M5Stack Cardputer
 *
 * Nordic UART Service (NUS) client: scan → select → connect → terminal I/O.
 * UX mirrors UART Terminal / I2C Scanner (dual display, FN shortcuts, ;/. nav).
 *
 * External ILI9341:
 *   Idle / Scan / Results  — full-screen list UI (like I2C)
 *   Terminal               — topbar + scrolling sprite + input line (like UART)
 *
 * Internal ST7789: status (scan count / peer name / RSSI / echo flags)
 *
 * FN shortcuts (terminal):
 *   FN+1  = disconnect → results
 *   FN+2  = disconnect → rescan
 *   FN+3  = toggle local echo
 *   FN+4  = toggle ANSI filter
 *   FN+5  = clear terminal
 *   FN+6  = toggle CRLF
 *   FN+7  = font 1x/2x
 *   FN+DEL= disconnect + menu
 *
 * Scan / results:
 *   ENTER = start scan / connect
 *   FN+R  = rescan
 *   ;/.   = navigate list
 *   DEL   = back
 *   FN+DEL= menu
 */

#include "app_ble_uart.h"
#include "../display/display_ext.h"
#include "../display/display_int.h"
#include "../config.h"
#include "../app_state.h"
#include <M5Cardputer.h>
#include <NimBLEDevice.h>

// ─── Nordic UART Service UUIDs ────────────────────────────────────────────────

static NimBLEUUID NUS_SVC("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID NUS_RX ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");  // write (central→peripheral)
static NimBLEUUID NUS_TX ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");  // notify (peripheral→central)

// ─── Layout (list screens) ────────────────────────────────────────────────────

static constexpr int ROW_H      = 22;
static constexpr int LIST_TOP_Y = 26;
static constexpr int LIST_BOT_Y = EXT_H - 14;
static constexpr int LIST_VIS   = (LIST_BOT_Y - LIST_TOP_Y) / ROW_H;

// ─── Scan result store ────────────────────────────────────────────────────────

static constexpr int MAX_DEVICES = 16;
static constexpr int NAME_LEN    = 28;

struct BleDev {
    NimBLEAddress addr;
    char          name[NAME_LEN];
    int           rssi;
    bool          hasNus;
};

// ─── App sub-states ───────────────────────────────────────────────────────────

enum BleUiState {
    BLE_IDLE,
    BLE_SCANNING,
    BLE_RESULTS,
    BLE_CONNECTING,
    BLE_TERMINAL
};

// ─── RX ring buffer (notify CB → main loop) ───────────────────────────────────

static constexpr size_t RX_RING = 2048;
static uint8_t  s_rx[RX_RING];
static volatile size_t s_rxHead = 0;
static volatile size_t s_rxTail = 0;
static portMUX_TYPE s_rxMux = portMUX_INITIALIZER_UNLOCKED;

static void rxPush(const uint8_t* data, size_t len) {
    portENTER_CRITICAL(&s_rxMux);
    for (size_t i = 0; i < len; i++) {
        size_t next = (s_rxHead + 1) % RX_RING;
        if (next == s_rxTail) break;  // drop on overflow
        s_rx[s_rxHead] = data[i];
        s_rxHead = next;
    }
    portEXIT_CRITICAL(&s_rxMux);
}

static bool rxPop(uint8_t& out) {
    portENTER_CRITICAL(&s_rxMux);
    if (s_rxHead == s_rxTail) {
        portEXIT_CRITICAL(&s_rxMux);
        return false;
    }
    out = s_rx[s_rxTail];
    s_rxTail = (s_rxTail + 1) % RX_RING;
    portEXIT_CRITICAL(&s_rxMux);
    return true;
}

static void rxClear() {
    portENTER_CRITICAL(&s_rxMux);
    s_rxHead = s_rxTail = 0;
    portEXIT_CRITICAL(&s_rxMux);
}

// ─── State ────────────────────────────────────────────────────────────────────

static BleUiState app_state = BLE_IDLE;
static BleDev     devices[MAX_DEVICES];
static int        device_count = 0;
static int        sel_idx      = 0;
static int        scroll_off   = 0;
static bool       int_dirty    = true;
static bool       scan_nus_only = false;  // FN+1 on idle: filter NUS advertisers

static String   input_buf;
static bool     local_echo  = true;
static bool     filter_ansi = true;
static bool     send_crlf   = true;
static bool     ansi_seq    = false;
static uint8_t  font_scale  = 1;
static bool     peer_alive  = false;
static char     peer_name[NAME_LEN] = "";
static int      peer_rssi = 0;

static NimBLEClient*               s_client = nullptr;
static NimBLERemoteCharacteristic* s_rxChar = nullptr;  // NUS RX = write
static NimBLERemoteCharacteristic* s_txChar = nullptr;  // NUS TX = notify
static bool                        s_bleInited = false;
static volatile bool               s_scanDone  = false;
static volatile bool               s_wantDisconnect = false;

// ─── Forward decls ────────────────────────────────────────────────────────────

static void extDrawIdle();
static void extDrawScanning();
static void extDrawResults();
static void extDrawConnecting();
static void setupTerminalUi();
static void pushTerminal();
static void pushIntStatus();
static void startScan();
static void stopScan();
static void disconnectPeer();
static bool connectSelected();
static void cleanupBle();

// ─── NimBLE callbacks ─────────────────────────────────────────────────────────

class BleClientCbs : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* /*c*/) override {
        peer_alive = true;
        int_dirty  = true;
    }
    void onDisconnect(NimBLEClient* /*c*/, int /*reason*/) override {
        peer_alive = false;
        s_rxChar   = nullptr;
        s_txChar   = nullptr;
        s_wantDisconnect = true;
        int_dirty  = true;
    }
} s_clientCbs;

class BleScanCbs : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* adv) override {
        if (device_count >= MAX_DEVICES) return;

        bool hasNus = adv->isAdvertisingService(NUS_SVC);
        if (scan_nus_only && !hasNus) return;

        // Deduplicate by address
        NimBLEAddress addr = adv->getAddress();
        for (int i = 0; i < device_count; i++) {
            if (devices[i].addr == addr) {
                devices[i].rssi = adv->getRSSI();
                if (hasNus) devices[i].hasNus = true;
                if (adv->haveName() && devices[i].name[0] == '\0') {
                    std::string n = adv->getName();
                    strncpy(devices[i].name, n.c_str(), NAME_LEN - 1);
                    devices[i].name[NAME_LEN - 1] = '\0';
                }
                return;
            }
        }

        BleDev& d = devices[device_count];
        d.addr   = addr;
        d.rssi   = adv->getRSSI();
        d.hasNus = hasNus;
        d.name[0] = '\0';
        if (adv->haveName()) {
            std::string n = adv->getName();
            strncpy(d.name, n.c_str(), NAME_LEN - 1);
            d.name[NAME_LEN - 1] = '\0';
        } else {
            std::string a = addr.toString();
            snprintf(d.name, NAME_LEN, "%s", a.c_str());
        }
        device_count++;
        int_dirty = true;
    }

    void onScanEnd(const NimBLEScanResults& /*results*/, int /*reason*/) override {
        s_scanDone = true;
    }
} s_scanCbs;

static void notifyCB(NimBLERemoteCharacteristic* /*c*/, uint8_t* data, size_t length, bool /*isNotify*/) {
    if (data && length) rxPush(data, length);
}

// ─── External display helpers ─────────────────────────────────────────────────

static void extDrawHeader(const char* left, const char* right) {
    extDisplay.fillRect(0, 0, EXT_W, 22, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(4, 7);
    extDisplay.print(left);
    if (right && right[0]) {
        extDisplay.setTextColor(TFT_YELLOW, TFT_DARKGREY);
        int tw = extDisplay.textWidth(right);
        extDisplay.setCursor(EXT_W - tw - 4, 7);
        extDisplay.print(right);
    }
}

static void extDrawFooter(const char* hint) {
    extDisplay.fillRect(0, EXT_H - 14, EXT_W, 14, TFT_BLACK);
    extDisplay.drawFastHLine(0, EXT_H - 14, EXT_W, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(4, EXT_H - 11);
    extDisplay.print(hint);
}

static void extDrawIdle() {
    extDisplay.fillScreen(TFT_BLACK);
    extDrawHeader("BLE UART", scan_nus_only ? "NUS only" : "All BLE");
    extDisplay.setTextSize(2);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    int tw = extDisplay.textWidth("BLE UART CLIENT");
    extDisplay.setCursor((EXT_W - tw) / 2, 56);
    extDisplay.print("BLE UART CLIENT");

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    extDisplay.setCursor(24, 100);
    extDisplay.print("Nordic UART Service (NUS)");
    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(24, 118);
    extDisplay.print("Scan nearby BLE devices, connect,");
    extDisplay.setCursor(24, 130);
    extDisplay.print("and exchange UART-like data.");

    extDisplay.setTextColor(C_DIM, TFT_BLACK);
    extDisplay.setCursor(24, 160);
    extDisplay.print("FN+1 = Filter: All / NUS-only");
    extDisplay.setCursor(24, 172);
    extDisplay.print("ENTER = Start scan");
    extDrawFooter("FN+1=Filter  ENTER=Scan  FN+DEL=Menu");
}

static void extDrawScanning() {
    extDisplay.fillScreen(TFT_BLACK);
    extDrawHeader("BLE UART", scan_nus_only ? "NUS" : "ALL");
    extDisplay.setTextSize(2);
    extDisplay.setTextColor(C_AMBER, TFT_BLACK);
    int tw = extDisplay.textWidth("SCANNING...");
    extDisplay.setCursor((EXT_W - tw) / 2, 80);
    extDisplay.print("SCANNING...");

    extDisplay.setTextSize(1);
    char buf[32];
    snprintf(buf, sizeof(buf), "Found: %d", device_count);
    extDisplay.setTextColor(C_GREEN, TFT_BLACK);
    tw = extDisplay.textWidth(buf);
    extDisplay.setCursor((EXT_W - tw) / 2, 120);
    extDisplay.print(buf);

    extDisplay.setTextColor(C_GRAY, TFT_BLACK);
    extDisplay.setCursor(24, 150);
    extDisplay.print("Looking for BLE advertisers...");
    extDrawFooter("Wait...  FN+DEL=Cancel");
}

static void extDrawResultRow(int listIdx, bool selected) {
    const BleDev& d = devices[listIdx];
    int ry = LIST_TOP_Y + (listIdx - scroll_off) * ROW_H;
    uint16_t bg = selected ? C_DIM : TFT_BLACK;
    extDisplay.fillRect(2, ry, EXT_W - 4, ROW_H - 2, bg);
    if (selected) extDisplay.drawRect(2, ry, EXT_W - 4, ROW_H - 2, C_GREEN);

    int ty = ry + (ROW_H - 2) / 2 - 3;
    extDisplay.setTextSize(1);

    char nameBuf[22];
    strncpy(nameBuf, d.name, 21);
    nameBuf[21] = '\0';
    extDisplay.setTextColor(selected ? C_WHITE : C_GREEN, bg);
    extDisplay.setCursor(6, ty);
    extDisplay.print(nameBuf);

    extDisplay.setTextColor(d.hasNus ? C_AMBER : C_GRAY, bg);
    extDisplay.setCursor(190, ty);
    extDisplay.print(d.hasNus ? "NUS" : "---");

    char rssiBuf[8];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d", d.rssi);
    extDisplay.setTextColor(C_BLUE, bg);
    int tw = extDisplay.textWidth(rssiBuf);
    extDisplay.setCursor(EXT_W - tw - 8, ty);
    extDisplay.print(rssiBuf);
}

static void extDrawResults() {
    extDisplay.fillScreen(TFT_BLACK);
    char right[24];
    snprintf(right, sizeof(right), "%d found", device_count);
    extDrawHeader("BLE UART / SELECT", right);

    if (device_count == 0) {
        extDisplay.setTextSize(1);
        extDisplay.setTextColor(C_GRAY, TFT_BLACK);
        extDisplay.setCursor(24, 100);
        extDisplay.print("No devices found.");
        extDisplay.setCursor(24, 116);
        extDisplay.print("FN+R = Rescan");
    } else {
        if (sel_idx < scroll_off) scroll_off = sel_idx;
        if (sel_idx >= scroll_off + LIST_VIS) scroll_off = sel_idx - LIST_VIS + 1;
        for (int i = scroll_off; i < device_count && i < scroll_off + LIST_VIS; i++)
            extDrawResultRow(i, i == sel_idx);
    }
    extDrawFooter(";=Up .=Down ENTER=Connect FN+R=Rescan FN+DEL=Menu");
}

static void extDrawConnecting() {
    extDisplay.fillScreen(TFT_BLACK);
    extDrawHeader("BLE UART", "CONNECT");
    extDisplay.setTextSize(2);
    extDisplay.setTextColor(C_AMBER, TFT_BLACK);
    int tw = extDisplay.textWidth("CONNECTING...");
    extDisplay.setCursor((EXT_W - tw) / 2, 80);
    extDisplay.print("CONNECTING...");

    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_WHITE, TFT_BLACK);
    tw = extDisplay.textWidth(peer_name);
    extDisplay.setCursor((EXT_W - tw) / 2, 120);
    extDisplay.print(peer_name);
    extDrawFooter("Please wait...");
}

// ─── Terminal UI ──────────────────────────────────────────────────────────────

static void bleDrawTopbar() {
    extDisplay.fillRect(0, 0, EXT_W, TOP_H, TFT_DARKGREY);
    extDisplay.setTextSize(1);
    extDisplay.setTextColor(C_GREEN, TFT_DARKGREY);
    extDisplay.setCursor(3, 3);
    extDisplay.print("BLE UART");

    char buf[40];
    snprintf(buf, sizeof(buf), "%s  %ddBm",
             peer_alive ? "CONN" : "LOST", peer_rssi);
    extDisplay.setTextColor(peer_alive ? TFT_YELLOW : C_RED, TFT_DARKGREY);
    int tw = extDisplay.textWidth(buf);
    extDisplay.setCursor(EXT_W - tw - 3, 3);
    extDisplay.print(buf);
}

static void termWrite(char c) {
    if (filter_ansi) {
        if (ansi_seq) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ansi_seq = false;
            return;
        }
        if (c == 0x1b) { ansi_seq = true; return; }
    }
    if (c == '\r') { terminal.setCursor(0, terminal.getCursorY()); return; }
    if (c == '\n') { terminal.println(); return; }
    if (c == '\b') {
        int cx = terminal.getCursorX();
        int cy = terminal.getCursorY();
        int cw = terminal.textWidth("W");
        if (cx >= cw) {
            terminal.setCursor(cx - cw, cy);
            terminal.print(' ');
            terminal.setCursor(cx - cw, cy);
        }
        return;
    }
    terminal.print(c);
}

static void pushTerminal() {
    terminal.pushSprite(TERM_X, TERM_Y);
    extTerm_DrawInputLine(input_buf);
}

static void setupTerminalUi() {
    if (!terminal.width() || terminal.height() != TERM_H_FULL) {
        terminal.deleteSprite();
        terminal.setColorDepth(16);
        terminal.createSprite(TERM_W, TERM_H_FULL);
    }
    terminal.setTextColor(C_GREEN, TFT_BLACK);
    terminal.setTextScroll(true);
    terminal.setTextWrap(true, true);
    terminal.setTextSize(font_scale);
    terminal.fillScreen(TFT_BLACK);
    terminal.setCursor(0, 0);

    extDisplay.fillScreen(TFT_BLACK);
    bleDrawTopbar();
    extDisplay.drawFastHLine(0, TOP_H, EXT_W, TFT_DARKGREY);

    terminal.printf("Connected: %s\n", peer_name);
    terminal.println("FN+1 Disc  FN+2 Rescan  FN+3 Echo");
    terminal.println("FN+4 ANSI  FN+5 Clear   FN+6 CRLF");
    terminal.println("FN+7 Font  FN+DEL=Menu");
    terminal.println();
    pushTerminal();
    int_dirty = true;
}

// ─── Internal status ──────────────────────────────────────────────────────────

static void pushIntStatus() {
    intSprite.fillSprite(C_BG);
    intSprite.setTextSize(1);

    intSprite.setTextColor(C_DIM, C_BG);
    intSprite.setCursor(4, 4);
    intSprite.print("BLE UART");

    switch (app_state) {
    case BLE_IDLE:
        intSprite.setTextSize(2);
        intSprite.setTextColor(C_GREEN, C_BG);
        intSprite.setCursor(4, 28);
        intSprite.print("READY");
        intSprite.setTextSize(1);
        intSprite.setTextColor(C_GRAY, C_BG);
        intSprite.setCursor(4, 60);
        intSprite.printf("Filter: %s", scan_nus_only ? "NUS" : "ALL");
        intSprite.setCursor(4, 76);
        intSprite.print("ENTER = Scan");
        break;

    case BLE_SCANNING:
        intSprite.setTextSize(2);
        intSprite.setTextColor(C_AMBER, C_BG);
        intSprite.setCursor(4, 28);
        intSprite.print("SCAN");
        intSprite.setTextSize(1);
        intSprite.setTextColor(C_GREEN, C_BG);
        intSprite.setCursor(4, 60);
        intSprite.printf("Found: %d", device_count);
        break;

    case BLE_RESULTS:
        intSprite.setTextSize(2);
        intSprite.setTextColor(C_GREEN, C_BG);
        intSprite.setCursor(4, 28);
        intSprite.print("SELECT");
        intSprite.setTextSize(1);
        intSprite.setTextColor(C_WHITE, C_BG);
        intSprite.setCursor(4, 60);
        if (device_count > 0) {
            intSprite.print(devices[sel_idx].name);
            intSprite.setTextColor(C_BLUE, C_BG);
            intSprite.setCursor(4, 76);
            intSprite.printf("RSSI %d  %s", devices[sel_idx].rssi,
                             devices[sel_idx].hasNus ? "NUS" : "no NUS");
        } else {
            intSprite.print("(empty)");
        }
        intSprite.setTextColor(C_GRAY, C_BG);
        intSprite.setCursor(4, INT_H - 12);
        intSprite.printf("[%d/%d]", device_count ? sel_idx + 1 : 0, device_count);
        break;

    case BLE_CONNECTING:
        intSprite.setTextSize(2);
        intSprite.setTextColor(C_AMBER, C_BG);
        intSprite.setCursor(4, 28);
        intSprite.print("LINK...");
        intSprite.setTextSize(1);
        intSprite.setTextColor(C_WHITE, C_BG);
        intSprite.setCursor(4, 60);
        intSprite.print(peer_name);
        break;

    case BLE_TERMINAL:
        intSprite.setTextSize(2);
        intSprite.setTextColor(peer_alive ? C_GREEN : C_RED, C_BG);
        intSprite.setCursor(4, 22);
        intSprite.print(peer_alive ? "ONLINE" : "LOST");
        intSprite.setTextSize(1);
        intSprite.setTextColor(C_WHITE, C_BG);
        intSprite.setCursor(4, 48);
        intSprite.print(peer_name);
        intSprite.setTextColor(C_BLUE, C_BG);
        intSprite.setCursor(4, 64);
        intSprite.printf("RSSI %d dBm", peer_rssi);
        intSprite.setTextColor(C_DIM, C_BG);
        intSprite.setCursor(4, 84);
        intSprite.printf("Echo:%s ANSI:%s CRLF:%s Fx%d",
                         local_echo ? "ON" : "off",
                         filter_ansi ? "ON" : "off",
                         send_crlf ? "ON" : "off",
                         font_scale);
        intSprite.setCursor(4, INT_H - 12);
        intSprite.print("FN+1=Disc  FN+DEL=Menu");
        break;
    }

    intSprite.pushSprite(0, 0);
    int_dirty = false;
}

// ─── BLE operations ───────────────────────────────────────────────────────────

static void ensureBleInit() {
    if (s_bleInited) return;
    NimBLEDevice::init("Cardputer");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    s_bleInited = true;
}

static void startScan() {
    ensureBleInit();
    stopScan();
    disconnectPeer();

    device_count = 0;
    sel_idx      = 0;
    scroll_off   = 0;
    s_scanDone   = false;
    app_state    = BLE_SCANNING;
    int_dirty    = true;
    extDrawScanning();
    pushIntStatus();

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCbs, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);
    scan->setMaxResults(0);
    // duration=8s; returns immediately, onScanEnd fires when done
    scan->start(8, false);
}

static void stopScan() {
    if (!s_bleInited) return;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
}

static void disconnectPeer() {
    s_rxChar = nullptr;
    s_txChar = nullptr;
    peer_alive = false;
    if (s_client) {
        if (s_client->isConnected()) s_client->disconnect();
        NimBLEDevice::deleteClient(s_client);
        s_client = nullptr;
    }
    rxClear();
}

static void cleanupBle() {
    stopScan();
    disconnectPeer();
}

static bool connectSelected() {
    if (sel_idx < 0 || sel_idx >= device_count) return false;

    ensureBleInit();
    stopScan();

    BleDev& d = devices[sel_idx];
    strncpy(peer_name, d.name, NAME_LEN - 1);
    peer_name[NAME_LEN - 1] = '\0';
    peer_rssi = d.rssi;

    app_state = BLE_CONNECTING;
    int_dirty = true;
    extDrawConnecting();
    pushIntStatus();

    disconnectPeer();

    s_client = NimBLEDevice::createClient();
    if (!s_client) return false;
    s_client->setClientCallbacks(&s_clientCbs, false);
    s_client->setConnectionParams(12, 12, 0, 150);
    s_client->setConnectTimeout(8);

    if (!s_client->connect(d.addr)) {
        terminal.deleteSprite();  // not yet in term mode
        NimBLEDevice::deleteClient(s_client);
        s_client = nullptr;
        return false;
    }

    NimBLERemoteService* svc = s_client->getService(NUS_SVC);
    if (!svc) {
        // Fallback: try any service with RX/TX-like chars by UUID alone
        // Some firmwares omit service in discovery cache — still fail cleanly
        s_client->disconnect();
        NimBLEDevice::deleteClient(s_client);
        s_client = nullptr;
        return false;
    }

    s_rxChar = svc->getCharacteristic(NUS_RX);
    s_txChar = svc->getCharacteristic(NUS_TX);

    if (!s_rxChar || !s_txChar) {
        s_client->disconnect();
        NimBLEDevice::deleteClient(s_client);
        s_client = nullptr;
        s_rxChar = s_txChar = nullptr;
        return false;
    }

    if (s_txChar->canNotify()) {
        if (!s_txChar->subscribe(true, notifyCB)) {
            // continue anyway — TX may still work via read (rare)
        }
    }

    peer_alive = true;
    rxClear();
    input_buf = "";
    app_state = BLE_TERMINAL;
    setupTerminalUi();
    pushIntStatus();
    return true;
}

static void sendBleText(const char* s) {
    if (!s_rxChar || !peer_alive) return;
    size_t len = strlen(s);
    // NUS typically accepts up to MTU-3 bytes per write
    const size_t chunk = 20;
    while (len) {
        size_t n = len > chunk ? chunk : len;
        if (s_rxChar->canWrite())
            s_rxChar->writeValue((const uint8_t*)s, n, false);
        else if (s_rxChar->canWriteNoResponse())
            s_rxChar->writeValue((const uint8_t*)s, n, false);
        s += n;
        len -= n;
    }
}

static void sendBleByte(uint8_t v) {
    if (!s_rxChar || !peer_alive) return;
    s_rxChar->writeValue(&v, 1, false);
}

// ─── Keyboard handlers ────────────────────────────────────────────────────────

static void handleFnTerminal(char key) {
    switch (key) {
    case '1':
        disconnectPeer();
        app_state = BLE_RESULTS;
        int_dirty = true;
        extDrawResults();
        pushIntStatus();
        break;
    case '2':
        disconnectPeer();
        startScan();
        break;
    case '3':
        local_echo = !local_echo;
        int_dirty = true;
        break;
    case '4':
        filter_ansi = !filter_ansi;
        ansi_seq = false;
        int_dirty = true;
        break;
    case '5':
        terminal.fillScreen(TFT_BLACK);
        terminal.setCursor(0, 0);
        pushTerminal();
        break;
    case '6':
        send_crlf = !send_crlf;
        int_dirty = true;
        break;
    case '7':
        font_scale = (font_scale == 1) ? 2 : 1;
        terminal.setTextSize(font_scale);
        terminal.fillScreen(TFT_BLACK);
        terminal.setCursor(0, 0);
        terminal.println("Font size changed.");
        pushTerminal();
        int_dirty = true;
        break;
    }
}

static void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.fn && st.del) {
        cleanupBle();
        // Restore terminal sprite height for other apps
        if (terminal.height() != TERM_H_FULL) {
            terminal.deleteSprite();
            terminal.setColorDepth(16);
            terminal.createSprite(TERM_W, TERM_H_FULL);
        }
        transitionTo(STATE_MENU);
        return;
    }

    switch (app_state) {
    case BLE_IDLE:
        if (st.fn) {
            for (auto c : st.word) {
                if (c == '1') {
                    scan_nus_only = !scan_nus_only;
                    int_dirty = true;
                    extDrawIdle();
                    pushIntStatus();
                }
            }
            return;
        }
        if (st.enter) startScan();
        break;

    case BLE_SCANNING:
        // FN+DEL handled above; ignore other keys while scanning
        break;

    case BLE_RESULTS:
        if (st.fn) {
            for (auto c : st.word) {
                if (c == 'r' || c == 'R' || c == '1') startScan();
            }
            return;
        }
        if (st.del) {
            app_state = BLE_IDLE;
            int_dirty = true;
            extDrawIdle();
            pushIntStatus();
            return;
        }
        {
            bool redraw = false;
            for (auto c : st.word) {
                if (c == ';' || c == 'k') {
                    if (device_count) {
                        sel_idx = (sel_idx - 1 + device_count) % device_count;
                        redraw = true;
                    }
                } else if (c == '.' || c == 'j') {
                    if (device_count) {
                        sel_idx = (sel_idx + 1) % device_count;
                        redraw = true;
                    }
                }
            }
            if (redraw) { extDrawResults(); int_dirty = true; pushIntStatus(); }
        }
        if (st.enter && device_count > 0) {
            if (!connectSelected()) {
                app_state = BLE_RESULTS;
                int_dirty = true;
                extDrawResults();
                // Flash error on internal
                intSprite.fillSprite(C_BG);
                intSprite.setTextColor(C_RED, C_BG);
                intSprite.setTextSize(2);
                intSprite.setCursor(4, 40);
                intSprite.print("FAIL");
                intSprite.setTextSize(1);
                intSprite.setCursor(4, 70);
                intSprite.print("No NUS service");
                intSprite.pushSprite(0, 0);
                delay(800);
                pushIntStatus();
            }
        }
        break;

    case BLE_CONNECTING:
        break;

    case BLE_TERMINAL:
        if (st.fn) {
            for (auto c : st.word) handleFnTerminal(c);
            return;
        }
        if (st.ctrl) {
            for (auto c : st.word) {
                char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
                if (u >= 'A' && u <= 'Z') sendBleByte(u - 'A' + 1);
                else sendBleByte((uint8_t)c);
            }
            if (st.del)   sendBleByte(0x08);
            if (st.enter) sendBleText(send_crlf ? "\r\n" : "\r");
            return;
        }
        if (st.del) {
            if (input_buf.length() > 0) {
                input_buf.remove(input_buf.length() - 1);
                if (local_echo) termWrite('\b');
            }
        }
        for (auto c : st.word) {
            if (c >= 0x20 && c < 0x7F) {
                input_buf += c;
                if (local_echo) termWrite(c);
            }
        }
        if (st.enter) {
            if (input_buf.length() > 0) {
                sendBleText(input_buf.c_str());
                input_buf.clear();
            }
            sendBleText(send_crlf ? "\r\n" : "\r");
            if (local_echo) termWrite('\n');
        }
        pushTerminal();
        break;
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void bleUartApp_Setup() {
    app_state     = BLE_IDLE;
    device_count  = 0;
    sel_idx       = 0;
    scroll_off    = 0;
    int_dirty     = true;
    input_buf     = "";
    peer_alive    = false;
    peer_name[0]  = '\0';
    peer_rssi     = 0;
    s_wantDisconnect = false;
    s_scanDone    = false;
    local_echo    = true;
    filter_ansi   = true;
    send_crlf     = true;
    ansi_seq      = false;
    font_scale    = 1;

    ensureBleInit();
    extDrawIdle();
    pushIntStatus();
}

void bleUartApp_Loop() {
    if (g_session.forceRedraw) {
        switch (app_state) {
            case BLE_IDLE:       extDrawIdle(); break;
            case BLE_SCANNING:   extDrawScanning(); break;
            case BLE_RESULTS:    extDrawResults(); break;
            case BLE_CONNECTING: extDrawConnecting(); break;
            case BLE_TERMINAL:   bleDrawTopbar(); pushTerminal(); break;
        }
        int_dirty = true;
        g_session.forceRedraw = false;
    }

    handleKeyboard();
    if (g_session.app != STATE_APP_BLE_UART) return;

    // Scan finished → results
    if (app_state == BLE_SCANNING && s_scanDone) {
        s_scanDone = false;
        app_state  = BLE_RESULTS;
        int_dirty  = true;
        extDrawResults();
        pushIntStatus();
    }

    // Live refresh of found count while scanning
    if (app_state == BLE_SCANNING && int_dirty) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Found: %d", device_count);
        extDisplay.fillRect(0, 115, EXT_W, 20, TFT_BLACK);
        extDisplay.setTextSize(1);
        extDisplay.setTextColor(C_GREEN, TFT_BLACK);
        int tw = extDisplay.textWidth(buf);
        extDisplay.setCursor((EXT_W - tw) / 2, 120);
        extDisplay.print(buf);
        pushIntStatus();
    }

    // Unexpected disconnect while in terminal
    if (s_wantDisconnect) {
        s_wantDisconnect = false;
        if (app_state == BLE_TERMINAL) {
            terminal.setTextColor(C_RED, TFT_BLACK);
            terminal.println("\n[BLE disconnected]");
            terminal.setTextColor(C_GREEN, TFT_BLACK);
            pushTerminal();
            bleDrawTopbar();
            int_dirty = true;
        }
    }

    // Drain BLE RX into terminal
    if (app_state == BLE_TERMINAL) {
        bool dirty = false;
        uint8_t b;
        while (rxPop(b)) {
            termWrite((char)b);
            dirty = true;
        }
        if (dirty) pushTerminal();

        // Periodic RSSI refresh
        static uint32_t lastRssiMs = 0;
        uint32_t now = millis();
        if (peer_alive && s_client && (now - lastRssiMs > 2000)) {
            lastRssiMs = now;
            peer_rssi = s_client->getRssi();
            bleDrawTopbar();
            int_dirty = true;
        }
    }

    if (int_dirty) pushIntStatus();
}
