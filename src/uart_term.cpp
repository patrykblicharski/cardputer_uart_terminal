#include "uart_term.h"
#include "app_state.h"
#include "display_ext.h"
#include "display_int.h"
#include "cmd_grid.h"
#include "maint_menu.h"
#include "config.h"
#include <M5Cardputer.h>

// ─── Port mode ────────────────────────────────────────────────────────────────

enum PortMode { PORT_USB, PORT_UART, PORT_BOTH };

static const char* portLabel(PortMode m) {
    switch (m) {
        case PORT_USB:  return "USB";
        case PORT_UART: return "UART";
        case PORT_BOTH: return "BOTH";
    }
    return "?";
}

// ─── State ────────────────────────────────────────────────────────────────────

static Stream*  usb_stream  = &Serial;
static PortMode active_port = PORT_BOTH;
static PortMode last_rx     = PORT_UART;
static size_t   baud_idx    = 4;        // default: 115200

static bool    local_echo  = true;
static bool    filter_ansi = true;
static bool    send_crlf   = true;
static bool    ansi_seq    = false;
static uint8_t font_scale  = 1;

static String input_buf;
static bool   usb_connected = false;
static bool   int_dirty     = true;

// Height of terminal sprite (changes when cmd grid toggled)
static int term_h = TERM_H_FULL;

// ─── Serial setup ─────────────────────────────────────────────────────────────

static void setupSerial() {
    uint32_t baud = g_session.configLoaded ? g_session.config.baud : kBaudRates[baud_idx];
    Serial.begin(baud);
    usb_stream = &Serial;
    Serial1.begin(baud, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
}

// ─── Send helpers ─────────────────────────────────────────────────────────────

static void sendByte(uint8_t v) {
    if (active_port == PORT_USB  || active_port == PORT_BOTH) usb_stream->write(v);
    if (active_port == PORT_UART || active_port == PORT_BOTH) Serial1.write(v);
}

static void sendText(const char* s) {
    while (*s) sendByte((uint8_t)*s++);
}

// ─── Terminal character write ─────────────────────────────────────────────────

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

// ─── Display push ─────────────────────────────────────────────────────────────

static void pushDisplay() {
    terminal.pushSprite(TERM_X, TERM_Y);
    extTerm_DrawInputLine(input_buf);
    if (g_session.cmdGridVisible) cmdGrid_Draw();
}

static void pushStatus() {
    TermStatus s;
    s.portLabel    = portLabel(active_port);
    s.baud         = g_session.configLoaded ? g_session.config.baud : kBaudRates[baud_idx];
    s.lastRxLabel  = portLabel(last_rx);
    s.usbConnected = usb_connected;
    s.localEcho    = local_echo;
    s.filterAnsi   = filter_ansi;
    s.sendCrlf     = send_crlf;
    s.fontScale    = font_scale;
    s.deviceName   = g_session.deviceIdentified ? g_session.config.deviceName.c_str() : nullptr;
    intStatus_Draw(s);
    int_dirty = false;
}

static void drawChrome() {
    const char* name = g_session.deviceIdentified ? g_session.config.deviceName.c_str() : nullptr;
    uint32_t baud = g_session.configLoaded ? g_session.config.baud : kBaudRates[baud_idx];
    extTerm_DrawTopbar(portLabel(active_port), baud, name);
    extDisplay.drawFastHLine(0, TOP_H, EXT_W, TFT_DARKGREY);
}

// ─── Sprite resize on grid toggle ────────────────────────────────────────────

static void applyGridVisibility() {
    int new_h = g_session.cmdGridVisible ? TERM_H_GRID : TERM_H_FULL;
    if (new_h == term_h) return;
    term_h = new_h;
    terminal.deleteSprite();
    terminal.createSprite(TERM_W, term_h);
    terminal.setTextColor(C_GREEN, TFT_BLACK);
    terminal.setTextScroll(true);
    terminal.setTextWrap(true, true);
    terminal.setTextSize(font_scale);
    terminal.setCursor(0, 0);
    terminal.println("[grid toggled]");
    // Clear area between old and new boundary
    if (!g_session.cmdGridVisible) {
        extDisplay.fillRect(0, CMD_GRID_Y, EXT_W, CMD_GRID_H, TFT_BLACK);
    }
    pushDisplay();
}

// ─── FN key shortcuts ─────────────────────────────────────────────────────────

static void handleFn(char key) {
    switch (key) {
    case '1':
        active_port = (PortMode)((active_port + 1) % 3);
        drawChrome(); int_dirty = true;
        break;
    case '2':
        if (!g_session.configLoaded) {
            baud_idx = (baud_idx + 1) % kBaudRateCount;
            setupSerial();
        }
        drawChrome(); int_dirty = true;
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
        pushDisplay();
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
        pushDisplay(); int_dirty = true;
        break;
    case 'g': case 'G':
        g_session.cmdGridVisible = !g_session.cmdGridVisible;
        applyGridVisibility();
        break;
    case 'h': case 'H':
        transitionTo(STATE_MAINT_MENU);
        break;
    }
}

// ─── CTRL+key dynamic command mapping ────────────────────────────────────────
// CTRL+A,S,D,F,G,H,J,K,L → cmds[0..8]
static const char DYN_KEYS[] = "ASDFGHJKL";

static bool sendDynamicCmd(char upper) {
    for (int i = 0; i < 9; i++) {
        if (DYN_KEYS[i] == upper && i < g_session.dynamicCmdCount) {
            String cmd = g_session.dynamicCmds[i] + "\r\n";
            sendText(cmd.c_str());
            if (local_echo) {
                terminal.setTextColor(C_AMBER, TFT_BLACK);
                terminal.println(">> " + g_session.dynamicCmds[i]);
                terminal.setTextColor(C_GREEN, TFT_BLACK);
            }
            pushDisplay();
            return true;
        }
    }
    return false;
}

// ─── Keyboard handler ─────────────────────────────────────────────────────────

static void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto& st = M5Cardputer.Keyboard.keysState();

    if (st.fn) {
        for (auto c : st.word) handleFn(c);
        return;
    }

    if (st.ctrl) {
        for (auto c : st.word) {
            char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
            if (u == '[') {
                // CTRL+[ = go back to menu
                transitionTo(STATE_MENU);
                return;
            }
            // Try dynamic command first
            if (!sendDynamicCmd(u)) {
                // Fall back to raw control character
                if (u >= 'A' && u <= 'Z') sendByte(u - 'A' + 1);
                else sendByte((uint8_t)c);
            }
        }
        if (st.del)   sendByte(0x08);
        if (st.enter) send_crlf ? sendText("\r\n") : sendText("\r");
        return;
    }

    if (st.del) {
        if (input_buf.length() > 0) {
            input_buf.remove(input_buf.length() - 1);
            if (local_echo) termWrite('\b');
        }
    }

    for (auto c : st.word) {
        input_buf += c;
        if (local_echo) termWrite(c);
    }

    if (st.enter) {
        if (input_buf.length() > 0) {
            sendText(input_buf.c_str());
            input_buf.clear();
        }
        send_crlf ? sendText("\r\n") : sendText("\r");
        if (local_echo) termWrite('\n');
    }

    pushDisplay();
}

// ─── USB connection monitor ───────────────────────────────────────────────────

static void updateUsb() {
    bool now = (bool)Serial;
    if (now == usb_connected) return;
    usb_connected = now;
    terminal.setTextColor(TFT_YELLOW, TFT_BLACK);
    terminal.printf("[USB %s]\n", usb_connected ? "connected" : "disconnected");
    terminal.setTextColor(C_GREEN, TFT_BLACK);
    int_dirty = true;
    pushDisplay();
}

// ─── Serial receive + protocol line detection ─────────────────────────────────

static String s_lineBuf;

static void processLine(const String& line) {
    if (cmdGrid_ParseResponse(line)) {
        if (g_session.cmdGridVisible) cmdGrid_Draw();
        return;
    }
    if (maint_ParseSensors(line)) {
        return; // consumed by sensor view/chart loops
    }
    // Otherwise display as regular terminal output (already printed char by char)
}

static void readSerial() {
    bool dirty   = false;
    PortMode prev = last_rx;

    auto handleChar = [&](char c, PortMode src) {
        last_rx = src;
        dirty   = true;
        s_lineBuf += c;
        if (c == '\n') {
            processLine(s_lineBuf);
            s_lineBuf = "";
        }
        termWrite(c);
    };

    while (usb_stream->available()) handleChar((char)usb_stream->read(), PORT_USB);
    while (Serial1.available())     handleChar((char)Serial1.read(),     PORT_UART);

    if (last_rx != prev) int_dirty = true;
    if (dirty) pushDisplay();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void uartTerm_Setup() {
    // Reset terminal sprite to full height
    term_h = TERM_H_FULL;
    if (!terminal.width()) {
        terminal.setColorDepth(16);
        terminal.createSprite(TERM_W, TERM_H_FULL);
    } else if (terminal.height() != TERM_H_FULL) {
        terminal.deleteSprite();
        terminal.createSprite(TERM_W, TERM_H_FULL);
    }
    terminal.setTextColor(C_GREEN, TFT_BLACK);
    terminal.setTextScroll(true);
    terminal.setTextWrap(true, true);
    terminal.setTextSize(font_scale);
    terminal.fillScreen(TFT_BLACK);
    terminal.setCursor(0, 0);

    setupSerial();
    drawChrome();

    if (g_session.deviceIdentified) {
        terminal.println("Device: " + g_session.config.deviceName);
        terminal.println("UID: "    + g_session.config.uid);
        if (g_session.dynamicCmdCount > 0) {
            terminal.println("Cmds loaded. FN+G=grid  FN+H=maint");
        } else {
            terminal.println("FN+G=cmdgrid  FN+H=maintenance");
            // Request commands
            Serial1.print("?CMD\r\n");
        }
    } else {
        terminal.println("Cardputer UART Terminal v2");
        terminal.println("FN+1 Port  FN+2 Baud  FN+3 Echo");
        terminal.println("FN+4 ANSI  FN+5 Clear FN+6 CRLF");
        terminal.println("FN+7 Font  FN+G Grid  FN+H Maint");
        terminal.println("CTRL+[ = menu");
    }
    terminal.println();

    pushDisplay();
    pushStatus();
    s_lineBuf = "";
    input_buf = "";
}

void uartTerm_Loop() {
    updateUsb();
    handleKeyboard();
    if (g_session.app != STATE_TERMINAL) return; // may have transitioned
    readSerial();
    if (int_dirty) pushStatus();
}
