// 06-sd-card-test
// microSD bring-up tool: status panel, four touch buttons and a scrolling log.
// Everything is mirrored to Serial (115200, UART0). Serial commands: t s k r i h.
#include <Arduino.h>
#include <Board4848S040.h>
#include "sd_store.h"
#include "sd_tests.h"

#ifndef SCREEN_ROTATION
#define SCREEN_ROTATION 3
#endif

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

static const int COLS = 40;      // 12 px chars at textSize 2
static const int LOG_ROWS = 14;
static const int LOG_Y = 212;
static const int LOG_STEP = 18;

static const int BTN_Y = 154;
static const int BTN_W = 112;
static const int BTN_H = 52;
static const int BTN_X[4] = {4, 124, 244, 364};
static const char *const BTN_LABEL[4] = {"TESTS", "SPEED", "SOAK", "REMOUNT"};
enum { BTN_TESTS, BTN_SPEED, BTN_SOAK, BTN_REMOUNT };

// ---- log ring ----
struct LogRow {
    char text[COLS + 1];
    uint16_t colour;
};
static LogRow logRows[LOG_ROWS];
static int logCount = 0; // rows in use (max LOG_ROWS), oldest first

static uint16_t colAmber, colGreen, colLight, colBtn, colBtnHi, colSoakOn;
static bool pressed = false;
static bool lastMounted = false;

static uint16_t colourFor(const char *s) {
    if (!strncmp(s, "PASS", 4)) return RGB565_GREEN;
    if (!strncmp(s, "FAIL", 4)) return RGB565_RED;
    if (!strncmp(s, "WARN", 4)) return colAmber;
    if (!strncmp(s, "==", 2)) return RGB565_WHITE;
    return colLight;
}

static void drawLog() {
    gfx->fillRect(0, LOG_Y, 480, 480 - LOG_Y, RGB565_BLACK);
    gfx->setTextSize(2);
    for (int i = 0; i < logCount; i++) {
        gfx->setTextColor(logRows[i].colour);
        gfx->setCursor(0, LOG_Y + 4 + i * LOG_STEP);
        gfx->print(logRows[i].text);
    }
}

static void pushRow(const char *s, size_t len, uint16_t colour) {
    if (logCount == LOG_ROWS) {
        memmove(&logRows[0], &logRows[1], sizeof(LogRow) * (LOG_ROWS - 1));
        logCount--;
    }
    LogRow &r = logRows[logCount++];
    if (len > COLS) len = COLS;
    memcpy(r.text, s, len);
    r.text[len] = 0;
    r.colour = colour;
}

// Matches sdtests::LogFn. Safe to call from inside the blocking test suites.
static void logLine(const char *line) {
    Serial.println(line);
    uint16_t colour = colourFor(line);
    size_t left = strlen(line);
    const char *p = line;
    if (left == 0) pushRow("", 0, colour);
    while (left > 0) {
        size_t take = left;
        if (take > (size_t)COLS) {
            take = COLS;
            // wrap at the last space if that leaves a sensible row
            for (size_t i = COLS; i > 20; i--) {
                if (p[i] == ' ') { take = i; break; }
            }
        }
        pushRow(p, take, colour);
        p += take;
        left -= take;
        if (left > 0 && *p == ' ') { p++; left--; } // drop the separating space
    }
    drawLog();
}

// ---- header ----
static void drawHeader() {
    gfx->fillRect(0, 0, 480, 40, RGB565_DARKGREY);
    gfx->setTextColor(RGB565_WHITE, RGB565_DARKGREY);
    gfx->setTextSize(2);
    gfx->setCursor(10, 12);
    gfx->print("06 SD card test");
}

static void drawHeap() {
    char b[16];
    snprintf(b, sizeof(b), "heap %5luK", (unsigned long)(ESP.getFreeHeap() / 1024));
    gfx->setTextColor(RGB565_WHITE, RGB565_DARKGREY);
    gfx->setTextSize(2);
    gfx->setCursor(480 - 10 - (int)strlen(b) * 12, 12);
    gfx->print(b);
}

// ---- buttons ----
static void drawButton(int i, bool pressed) {
    uint16_t fill = colBtn;
    if (i == BTN_SOAK && sdtests::soakRunning()) fill = colSoakOn;
    if (pressed) fill = colBtnHi;
    gfx->fillRoundRect(BTN_X[i], BTN_Y, BTN_W, BTN_H, 8, fill);
    gfx->drawRoundRect(BTN_X[i], BTN_Y, BTN_W, BTN_H, 8, RGB565_LIGHTGREY);
    int w = (int)strlen(BTN_LABEL[i]) * 12;
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(BTN_X[i] + (BTN_W - w) / 2, BTN_Y + (BTN_H - 16) / 2);
    gfx->print(BTN_LABEL[i]);
}

static void drawButtons() {
    for (int i = 0; i < 4; i++) drawButton(i, false);
}

// ---- status panel ----
static sdcard::Info cachedInfo;
static uint32_t infoAt = 0;
static bool infoValid = false;

static void refreshInfo() {
    cachedInfo = sdcard::info();
    infoAt = millis();
    infoValid = true;
}

static void statusLine(int row, uint16_t fg, const char *fmt, ...) {
    char b[COLS + 1];
    char t[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof(t), fmt, ap);
    va_end(ap);
    snprintf(b, sizeof(b), "%-40s", t);
    gfx->setTextColor(fg, RGB565_BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(0, 48 + row * 20);
    gfx->print(b);
}

static void drawStatus() {
    if (!infoValid) refreshInfo();
    const sdcard::Info &in = cachedInfo;
    bool m = sdcard::mounted();
    if (m) {
        statusLine(0, RGB565_GREEN, "MOUNTED  %s  %lu MHz", sdcard::cardTypeName(in.cardType),
                   (unsigned long)(in.freqHz / 1000000));
        statusLine(1, RGB565_WHITE, "card %lluMB  fat %llu/%lluMB", (unsigned long long)(in.cardBytes >> 20),
                   (unsigned long long)(in.usedBytes >> 20), (unsigned long long)(in.totalBytes >> 20));
    } else {
        statusLine(0, RGB565_RED, "NO CARD");
        statusLine(1, RGB565_WHITE, "card -  fat -");
    }
    statusLine(2, colLight, "mounts %lu fails %lu lost %lu io %lu", (unsigned long)in.mounts,
               (unsigned long)in.mountFails, (unsigned long)in.cardLost, (unsigned long)in.ioErrors);
    if (sdtests::soakRunning()) {
        sdtests::SoakStats s = sdtests::soakStats();
        statusLine(3, colAmber, "soak w=%lu v=%lu err=%lu max=%lums", (unsigned long)s.written,
                   (unsigned long)s.verified, (unsigned long)s.errors, (unsigned long)s.maxAppendMs);
    } else {
        statusLine(3, colLight, "soak OFF");
    }
    uint32_t up = millis() / 1000;
    statusLine(4, colLight, "uptime %lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60),
               (unsigned long)(up % 60));
}

// Refresh the status panel now; force = re-read info() (walks the FAT).
static void refreshStatus(bool force) {
    if (force) refreshInfo();
    drawStatus();
    drawHeap();
}

// ---- actions ----
static void describeMount(const char *prefix) {
    sdcard::Info in = sdcard::info();
    char b[64];
    snprintf(b, sizeof(b), "%s%s %lu MB @ %lu MHz", prefix, sdcard::cardTypeName(in.cardType),
             (unsigned long)(in.cardBytes >> 20), (unsigned long)(in.freqHz / 1000000));
    logLine(b);
}

static void doTests() {
    if (sdtests::soakRunning()) { logLine("WARN stop the soak first"); return; }
    logLine("== Functional tests");
    sdtests::Result r = sdtests::runAll(logLine);
    char b[48];
    snprintf(b, sizeof(b), "%s tests: %u passed, %u failed", r.failed ? "FAIL" : "PASS", r.passed, r.failed);
    logLine(b);
    refreshStatus(true);
}

static void doSpeed() {
    if (sdtests::soakRunning()) { logLine("WARN stop the soak first"); return; }
    logLine("== Speed sweep");
    sdtests::speedSweep(logLine);
    refreshStatus(true);
}

static void doSoak() {
    if (sdtests::soakRunning()) sdtests::soakStop(logLine);
    else sdtests::soakStart(logLine);
    drawButton(BTN_SOAK, false);
    refreshStatus(false);
}

static void doRemount() {
    logLine("== Remount");
    sdcard::end();
    if (sdcard::begin()) describeMount("PASS mounted ");
    else logLine("WARN no card - insert one");
    refreshStatus(true);
}

static void doInfo() {
    sdcard::Info in = sdcard::info();
    char b[96];
    snprintf(b, sizeof(b), "state %s type %s %lu MHz", in.state == sdcard::State::Mounted ? "mounted" : "nocard",
             sdcard::cardTypeName(in.cardType), (unsigned long)(in.freqHz / 1000000));
    logLine(b);
    snprintf(b, sizeof(b), "card %llu B  fat %llu B", (unsigned long long)in.cardBytes,
             (unsigned long long)in.totalBytes);
    logLine(b);
    snprintf(b, sizeof(b), "used %llu B", (unsigned long long)in.usedBytes);
    logLine(b);
    snprintf(b, sizeof(b), "mounts %lu fails %lu lost %lu io %lu", (unsigned long)in.mounts,
             (unsigned long)in.mountFails, (unsigned long)in.cardLost, (unsigned long)in.ioErrors);
    logLine(b);
    snprintf(b, sizeof(b), "clock steps down %lu, link tests failed %lu", (unsigned long)in.degrades,
             (unsigned long)in.verifyFails);
    logLine(b);
}

static void printHelp() {
    Serial.println("Commands: t tests, s speed sweep, k soak toggle, r remount, i info, c card registers, f forget card clock, b/B backlight off/on, h help");
    Serial.println("Clock: 1 / 4 / 9 (=10) / 2 (=20) / 5 (=25) remounts at that many MHz");
    Serial.println("Each command ends with a \"#done <c>\" line.");
}

static void runButton(int i) {
    drawButton(i, true);
    switch (i) {
    case BTN_TESTS: doTests(); break;
    case BTN_SPEED: doSpeed(); break;
    case BTN_SOAK: doSoak(); break;
    case BTN_REMOUNT: doRemount(); break;
    default: break;
    }
    drawButton(i, false);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("=== 06-sd-card-test ===");
    printHelp();

    gfx = board4848::initDisplay(SCREEN_ROTATION);
    if (!gfx) {
        Serial.println("Display init failed (PSRAM missing? check memory_type = qio_opi)");
        while (true) delay(1000);
    }
    board4848::setBacklight(200);

    touch.setRotation(SCREEN_ROTATION);
    bool touchOk = touch.begin();
    touch.setRotation(SCREEN_ROTATION);

    colAmber = gfx->color565(245, 183, 0);
    colGreen = gfx->color565(0, 140, 60);
    colLight = RGB565_LIGHTGREY;
    colBtn = gfx->color565(60, 60, 70);
    colBtnHi = gfx->color565(130, 130, 150);
    colSoakOn = colGreen;

    gfx->fillScreen(RGB565_BLACK);
    drawHeader();
    drawHeap();
    drawButtons();
    drawLog();

    if (touchOk) {
        char b[40];
        snprintf(b, sizeof(b), "GT911 found at 0x%02X", touch.address());
        logLine(b);
    } else {
        logLine("WARN GT911 not found");
    }

    // Must come after initDisplay(): SCK/MOSI are shared with the ST7701 3-wire init bus.
    if (sdcard::begin()) describeMount("PASS mounted ");
    else logLine("WARN no card - insert one");
    lastMounted = sdcard::mounted();
    refreshStatus(true);
}
static uint32_t lastTick = 0;

static void handleTap(uint16_t x, uint16_t y) {
    if (y < BTN_Y || y >= BTN_Y + BTN_H) return;
    for (int i = 0; i < 4; i++) {
        if (x >= BTN_X[i] && x < BTN_X[i] + BTN_W) {
            runButton(i);
            return;
        }
    }
}

static void handleSerial() {
    while (Serial.available()) {
        int c = Serial.read();
        switch (c) {
        case 't': doTests(); break;
        case 's': doSpeed(); break;
        case 'k': doSoak(); break;
        case 'r': doRemount(); break;
        case 'i': doInfo(); break;
        case 'c': sdtests::cardReport(logLine); break;
        case 'f':
            logLine(sdcard::forgetClock() ? "PASS forgot this card's clock" : "FAIL forget clock");
            break;
        case '1': case '4': case '9': case '2': case '5': { // remount at 1 / 4 / 10 / 20 / 25 MHz
            const uint32_t mhz = c == '9' ? 10 : c == '2' ? 20 : c == '5' ? 25 : (uint32_t)(c - '0');
            char b[48];
            bool ok = sdcard::remount(mhz * 1000000UL);
            snprintf(b, sizeof(b), "%s remount at %lu MHz", ok ? "PASS" : "FAIL", (unsigned long)mhz);
            logLine(b);
            refreshStatus(true);
            break;
        }
        case 'b': // backlight off: tests whether panel load affects SD reliability
            board4848::setBacklight(0);
            logLine("backlight off");
            break;
        case 'B':
            board4848::setBacklight(200);
            logLine("backlight on");
            break;
        case 'h': printHelp(); break;
        default: continue; // whitespace and anything else
        }
        // End marker for host scripts (sd_serial.py waits for it)
        Serial.printf("#done %c\n", (char)c);
    }
}

void loop() {
    board4848::TouchPoint p;
    int8_t n = touch.read(&p, 1);
    if (n > 0 && !pressed) {
        pressed = true;
        handleTap(p.x, p.y);
    } else if (n == 0) {
        pressed = false;
    }

    handleSerial();

    uint32_t now = millis();
    if (now - lastTick >= 1000) {
        lastTick = now;
        bool m = sdcard::poll();
        bool changed = (m != lastMounted);
        if (changed) {
            lastMounted = m;
            if (m) describeMount("PASS card mounted ");
            else logLine("WARN card removed");
        }
        refreshStatus(changed || !infoValid || now - infoAt >= 5000);
    }

    sdtests::soakStep(logLine);
    delay(5);
}
