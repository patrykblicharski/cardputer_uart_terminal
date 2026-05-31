/*
 * Keyboard Diagnostic — M5Stack Cardputer
 *
 * 3 metody detekcji testowane równolegle:
 *   A) isChange() + keysState().word
 *   B) isChange() + isPressed() + keysState().word
 *   C) isKeyPressed(KEY_ENTER) / isKeyPressed(KEY_BACKSPACE) (template API)
 *
 * Wyjście: Serial 115200 + display wewnętrzny
 * Naciśnij dowolny klawisz — za 5 s bez wejścia wypisuje TICK.
 */

#include <M5Cardputer.h>

// ── counters per method ───────────────────────────────────────────────────────
static int cntA = 0, cntB = 0, cntC = 0;
static uint32_t lastTick = 0;
static uint32_t lastDraw = 0;

static void drawDisplay() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.println("== KB DIAG ==");
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    bool chg = M5Cardputer.Keyboard.isChange();
    bool prs = M5Cardputer.Keyboard.isPressed();
    auto& st = M5Cardputer.Keyboard.keysState();

    M5Cardputer.Display.printf("isChange=%-3s  isPressed=%-3s\n",
        chg ? "YES" : "no", prs ? "YES" : "no");
    M5Cardputer.Display.printf("word.size=%d  enter=%d  del=%d\n",
        (int)st.word.size(), (int)st.enter, (int)st.del);
    M5Cardputer.Display.printf("fn=%d  opt=%d  ctrl=%d\n",
        (int)st.fn, (int)st.opt, (int)st.ctrl);

    if (!st.word.empty()) {
        M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5Cardputer.Display.printf("CHAR: 0x%02X '%c'\n",
            (uint8_t)st.word[0], (char)st.word[0]);
    }

    M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5Cardputer.Display.printf("\nA(chg+word)=%d\n", cntA);
    M5Cardputer.Display.printf("B(chg+prs+word)=%d\n", cntB);
    M5Cardputer.Display.printf("C(isKeyPressed)=%d\n", cntC);
    M5Cardputer.Display.printf("uptime=%lus\n", millis() / 1000);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\r\n\r\n=== KEYBOARD DIAGNOSTIC TEST ===");
    Serial.println("Nacisnij dowolny klawisz");

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    drawDisplay();
}

void loop() {
    M5Cardputer.update();

    uint32_t now = millis();

    // ── Method A: isChange() only + keysState().word ──────────────────────────
    if (M5Cardputer.Keyboard.isChange()) {
        auto& st = M5Cardputer.Keyboard.keysState();
        cntA++;
        Serial.printf("[A] isChange=1 isPressed=%d word.sz=%d enter=%d del=%d fn=%d opt=%d ctrl=%d\r\n",
            (int)M5Cardputer.Keyboard.isPressed(),
            (int)st.word.size(),
            (int)st.enter, (int)st.del,
            (int)st.fn, (int)st.opt, (int)st.ctrl);
        for (auto c : st.word) {
            Serial.printf("  [A] char=0x%02X '%c'\r\n", (uint8_t)c, (char)c);
        }

        // ── Method B: same but also requires isPressed() ──────────────────────
        if (M5Cardputer.Keyboard.isPressed()) {
            cntB++;
            Serial.printf("[B] isPressed=1 => klawisz faktycznie wcisniety\r\n");
        } else {
            Serial.printf("[B] isPressed=0 => stan zmienil sie ale nie 'pressed'\r\n");
        }

        // ── Method C: isKeyPressed for specific keys ──────────────────────────
        bool anyC = false;
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER))     { cntC++; anyC = true; Serial.println("[C] KEY_ENTER"); }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE))  {         anyC = true; Serial.println("[C] KEY_BACKSPACE (DEL)"); }
        if (M5Cardputer.Keyboard.isKeyPressed(';'))            { cntC++; anyC = true; Serial.println("[C] ';' (up arrow)"); }
        if (M5Cardputer.Keyboard.isKeyPressed('.'))            { cntC++; anyC = true; Serial.println("[C] '.' (down arrow)"); }
        if (!anyC && !st.word.empty()) {
            if (M5Cardputer.Keyboard.isKeyPressed((uint8_t)st.word[0])) {
                cntC++; Serial.printf("[C] isKeyPressed(0x%02X) OK\r\n", (uint8_t)st.word[0]);
            } else {
                Serial.printf("[C] isKeyPressed(0x%02X) FAIL\r\n", (uint8_t)st.word[0]);
            }
        }
    }

    // Refresh display every 250ms
    if (now - lastDraw >= 250) {
        lastDraw = now;
        drawDisplay();
    }

    // Tick every 5s
    if (now - lastTick >= 5000) {
        lastTick = now;
        Serial.printf("[TICK %lus] cntA=%d cntB=%d cntC=%d\r\n",
            now / 1000, cntA, cntB, cntC);
    }
}
