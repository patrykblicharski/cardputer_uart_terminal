#include "uart_term.h"
#include "display_ext.h"
#include "display_int.h"
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
static bool    ansi_seq    = false;     // inside ANSI escape when true
static uint8_t font_scale  = 1;

static String input_buf;
static bool   usb_connected = false;
static bool   int_dirty     = true;

// ─── Serial setup ─────────────────────────────────────────────────────────────

static void setupSerial() {
    Serial.begin(kBaudRates[baud_idx]);
    usb_stream = &Serial;
    Serial1.begin(kBaudRates[baud_idx], SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
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
        // Swallow everything from ESC until a letter terminates the sequence
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
    extTerm_Push(input_buf);
}

static void pushStatus() {
    TermStatus s;
    s.portLabel    = portLabel(active_port);
    s.baud         = kBaudRates[baud_idx];
    s.lastRxLabel  = portLabel(last_rx);
    s.usbConnected = usb_connected;
    s.localEcho    = local_echo;
    s.filterAnsi   = filter_ansi;
    s.sendCrlf     = send_crlf;
    s.fontScale    = font_scale;
    intStatus_Draw(s);
    int_dirty = false;
}

static void drawChrome() {
    extTerm_DrawTopbar(portLabel(active_port), kBaudRates[baud_idx]);
    extDisplay.drawFastHLine(0, TOP_H, EXT_W, TFT_DARKGREY);
}

// ─── FN key shortcuts ─────────────────────────────────────────────────────────

static void handleFn(char key) {
    switch (key) {
    case '1':
        active_port = (PortMode)((active_port + 1) % 3);
        drawChrome(); int_dirty = true;
        break;
    case '2':
        baud_idx = (baud_idx + 1) % kBaudRateCount;
        setupSerial(); drawChrome(); int_dirty = true;
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
    }
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
            if (u >= 'A' && u <= 'Z')  sendByte(u - 'A' + 1);
            else if (c == '[')         sendByte(0x1b);
            else                       sendByte((uint8_t)c);
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

// ─── Serial receive ───────────────────────────────────────────────────────────

static void readSerial() {
    bool dirty    = false;
    PortMode prev = last_rx;

    while (usb_stream->available()) {
        termWrite((char)usb_stream->read());
        last_rx = PORT_USB;
        dirty = true;
    }

    while (Serial1.available()) {
        termWrite((char)Serial1.read());
        last_rx = PORT_UART;
        dirty = true;
    }

    if (last_rx != prev) int_dirty = true;
    if (dirty) pushDisplay();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void uartTerm_Setup() {
    extTerm_Init();
    drawChrome();
    terminal.pushSprite(TERM_X, TERM_Y);
    extTerm_DrawInputLine(input_buf);

    setupSerial();

    terminal.println("Cardputer UART Terminal");
    terminal.println("FN+1 Port   FN+2 Baud   FN+3 Echo");
    terminal.println("FN+4 ANSI   FN+5 Clear  FN+6 CRLF");
    terminal.println("FN+7 Font   CTRL+key = ctrl char");
    terminal.println();
    pushDisplay();
    pushStatus();
}

void uartTerm_Loop() {
    updateUsb();
    handleKeyboard();
    readSerial();
    if (int_dirty) pushStatus();
}
