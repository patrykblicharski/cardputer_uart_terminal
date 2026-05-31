/*
 * SD Card Configuration Test — M5Stack Cardputer
 *
 * Problem: SPI.begin() uzywa domyslnie FSPI (SPI2), ktory M5GFX/LGFX
 * zajmuje dla ILI9341. W main app dochodzi do konfliktu.
 * Ten test sprawdza 3 podejscia (bez display — brak konfliktu z LGFX):
 *
 *   [1] SPIClass(FSPI)  — SPI2, te same piny           (ryzyko konfliktu z LGFX)
 *   [2] SPIClass(HSPI)  — SPI3, te same piny via GPIO matrix (zalecane w main app)
 *   [3] global SPI + SPI.begin z explicit pinami
 *
 * Piny SD Cardputer: CS=12, SCK=40, MISO=39, MOSI=14
 *
 * Wyniki: Serial 115200 + display wewnetrzny
 * Build:  pio run -e test-sd -t upload
 */

#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>

static constexpr int PIN_CS   = 12;
static constexpr int PIN_SCK  = 40;
static constexpr int PIN_MISO = 39;
static constexpr int PIN_MOSI = 14;

static constexpr uint32_t SPI_FREQ = 4000000;

// ── Result store ──────────────────────────────────────────────────────────────

struct TestResult {
    const char* label;
    bool        mounted;
    char        detail[80];
};
static TestResult s_results[3];
static int        s_count = 0;

// ── Single mount attempt ──────────────────────────────────────────────────────

static bool mountWith(SPIClass& spi, TestResult& r) {
    spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
    delay(150);

    if (!SD.begin(PIN_CS, spi, SPI_FREQ)) {
        snprintf(r.detail, sizeof(r.detail), "SD.begin() failed");
        r.mounted = false;
        SD.end();
        return false;
    }

    uint8_t ct = SD.cardType();
    if (ct == CARD_NONE) {
        snprintf(r.detail, sizeof(r.detail), "CARD_NONE (no card?)");
        r.mounted = false;
        SD.end();
        return false;
    }

    const char* typeName = (ct == CARD_MMC)  ? "MMC"  :
                           (ct == CARD_SD)   ? "SD"   :
                           (ct == CARD_SDHC) ? "SDHC" : "UNK";
    uint64_t mb = SD.cardSize() / (1024ULL * 1024ULL);

    File devDir = SD.open("/devices");
    bool hasDevices = devDir && devDir.isDirectory();
    if (devDir) devDir.close();

    snprintf(r.detail, sizeof(r.detail), "%s %lluMB /devices=%s",
             typeName, mb, hasDevices ? "OK" : "missing");
    r.mounted = true;
    SD.end();
    return true;
}

// ── Display final summary ─────────────────────────────────────────────────────

static void showDisplay() {
    auto& disp = M5Cardputer.Display;
    disp.fillScreen(TFT_BLACK);
    disp.setTextSize(1);
    disp.setCursor(0, 0);
    disp.setTextColor(TFT_CYAN, TFT_BLACK);
    disp.println("=== SD CONFIG TEST ===");
    disp.printf("Pins CS=%d SCK=%d\n", PIN_CS, PIN_SCK);
    disp.printf("     MISO=%d MOSI=%d\n\n", PIN_MISO, PIN_MOSI);

    for (int i = 0; i < s_count; i++) {
        disp.setTextColor(s_results[i].mounted ? TFT_GREEN : TFT_RED, TFT_BLACK);
        disp.printf("[%d] %s\n", i + 1, s_results[i].mounted ? "OK" : "FAIL");
        disp.setTextColor(TFT_WHITE, TFT_BLACK);
        disp.printf("    %s\n", s_results[i].label);
        disp.setTextColor(s_results[i].mounted ? TFT_GREEN : 0x632C, TFT_BLACK);
        disp.printf("    %s\n\n", s_results[i].detail);
    }

    bool anyOk = false;
    for (int i = 0; i < s_count; i++) anyOk |= s_results[i].mounted;
    disp.setTextColor(anyOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
    disp.println(anyOk ? "=> Sprawdz Serial 115200" : "=> Brak karty lub zle piny");
}

// ── Arduino setup ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    Serial.println("\r\n=== SD CARD CONFIGURATION TEST ===");
    Serial.printf("Pins: CS=%d  SCK=%d  MISO=%d  MOSI=%d\r\n",
                  PIN_CS, PIN_SCK, PIN_MISO, PIN_MOSI);
    Serial.println("Testuje 3 konfiguracje...\r\n");

    // Test 1: FSPI (SPI2) — domyslny SPI, konflikt z LGFX w main app
    {
        TestResult& r = s_results[s_count];
        r.label = "SPIClass(FSPI) = SPI2";
        SPIClass spi2(FSPI);
        bool ok = mountWith(spi2, r);
        Serial.printf("[1] %s: %s — %s\r\n", r.label, ok ? "OK" : "FAIL", r.detail);
        s_count++;
        delay(300);
    }

    // Test 2: HSPI (SPI3) — osobny host, bezpieczny gdy LGFX uzywa SPI2
    {
        TestResult& r = s_results[s_count];
        r.label = "SPIClass(HSPI) = SPI3";
        SPIClass spi3(HSPI);
        bool ok = mountWith(spi3, r);
        Serial.printf("[2] %s: %s — %s\r\n", r.label, ok ? "OK" : "FAIL", r.detail);
        s_count++;
        delay(300);
    }

    // Test 3: globalny SPI (= FSPI na ESP32-S3) z explicit SPI.begin()
    {
        TestResult& r = s_results[s_count];
        r.label = "global SPI (stary sposob)";
        SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
        delay(150);
        bool ok;
        if (!SD.begin(PIN_CS, SPI, SPI_FREQ)) {
            snprintf(r.detail, sizeof(r.detail), "SD.begin() failed");
            r.mounted = false;
            ok = false;
        } else {
            uint8_t ct = SD.cardType();
            snprintf(r.detail, sizeof(r.detail), "cardType=%d size=%lluMB",
                     ct, SD.cardSize() / (1024ULL * 1024ULL));
            r.mounted = (ct != CARD_NONE);
            ok = r.mounted;
        }
        SD.end();
        Serial.printf("[3] %s: %s — %s\r\n", r.label, ok ? "OK" : "FAIL", r.detail);
        s_count++;
    }

    Serial.println("\r\n=== PODSUMOWANIE ===");
    for (int i = 0; i < s_count; i++) {
        Serial.printf("[%d] %-30s %s\r\n",
                      i + 1, s_results[i].label,
                      s_results[i].mounted ? "OK  <- uzywaj tego" : "FAIL");
        if (s_results[i].mounted) Serial.printf("     %s\r\n", s_results[i].detail);
    }
    Serial.println("\r\nDla main app z LGFX/ILI9341: wybierz SPIClass(HSPI) jesli dziala.");

    showDisplay();
}

void loop() {
    delay(2000);
    // Static result display — nothing to poll
}
