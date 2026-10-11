// 05-powerwall-display
// Live Tesla Powerwall 3 dashboard on the 4" panel, read from the gateway's local LAN API.
// Network polling runs on core 0 (powerwall.cpp); LVGL runs in loop() on core 1.
//
// Screens (swipe left/right): power flow, today, battery & grid, weather, moon, aurora, sun, El Nino,
// settings, about (see ui::ScreenIndex).
//
// SCREEN_ROTATION: 0 = native, 1 = 90 deg clockwise, 2 = 180, 3 = 90 deg counter-clockwise.
// 3 puts the USB-C port at the bottom. Touch is rotated to match.
//
// Nothing is written to flash at runtime: today's totals live in RTC memory (energy.cpp), backed up
// with the minute log and settings on the SD card (history.cpp).
//
// Serial commands (115200 baud, CH340C on UART0). They drive the UI for testing and are read without
// blocking in loop(). One line in, one line out, ending in "#ok" or "#err <reason>":
//   s <n>      go to screen n (0..SCREEN_COUNT-1) the way a swipe does (refresh, slide, onShow)
//   n / p      next / previous screen (like a swipe left / right)
//   l          list the screens: "index name", the current one marked with *
//   b <0-255>  set the backlight now (not saved; the panel driver cuts out below ~100)
//   m          print the memory / heap line now
//   x          screenshot: framebuffer as "#px" base64 lines at 921600 baud (tools-dev/screendump.py)
//   v <0|1>    privacy mode for screenshots: placeholder Wi-Fi name and town (not saved)
//   c          list the SD cache records (name, size, version, age)
//   r          reboot (ESP.restart; RTC data and the system clock survive)
//   h          help
#include <Arduino.h>
#include <Board4848S040.h>
#include <Board4848S040_lvgl.h>
#include <esp_rom_crc.h>
#include <mbedtls/base64.h>
#include <stdlib.h>
#include <string.h>
#include "cache.h"
#include "energy.h"
#include "history.h"
#include "net_http.h"
#include "powerwall.h"
#include "screens.h"
#include "ui_common.h"

#ifndef SCREEN_ROTATION
#define SCREEN_ROTATION 3
#endif

// Entry animations (fades, grows) start after the screen slide instead of on top of it: they re-render
// whole cards while the slide already redraws the full screen every frame.
#ifndef UI_FADE_AFTER_SLIDE
#define UI_FADE_AFTER_SLIDE 1
#endif

// LVGL renders in loopTask: the default 8 KB left under 1 KB free (and overflowed once with LVGL's
// JPEG decoder registered). The heartbeat prints what is left ("stack free ... lvgl").
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

struct ScreenDef {
    const char *name;
    lv_obj_t *(*create)();
    void (*refresh)();
    void (*onShow)();
    lv_obj_t *obj;
};

static ScreenDef screens[ui::SCREEN_COUNT] = {
    {"house", houseCreate, houseRefresh, nullptr, nullptr},
    {"today", todayCreate, todayRefresh, todayOnShow, nullptr},
    {"health", healthCreate, healthRefresh, healthOnShow, nullptr},
    {"weather", weatherCreate, weatherRefresh, weatherOnShow, nullptr},
    {"moon", moonCreate, moonRefresh, moonOnShow, nullptr},
    {"aurora", auroraCreate, auroraRefresh, auroraOnShow, nullptr},
    {"sun", sunCreate, sunRefresh, sunOnShow, nullptr},
    {"enso", ensoCreate, ensoRefresh, ensoOnShow, nullptr},
    {"settings", settingsCreate, settingsRefresh, settingsOnShow, nullptr},
    {"about", aboutCreate, aboutRefresh, nullptr, nullptr},
};
static_assert(sizeof(screens) / sizeof(screens[0]) == ui::SCREEN_COUNT, "one entry per screen, in ScreenIndex order");
static int current = 0;

// ---- navigation ----

static constexpr uint32_t SLIDE_MS = 300;

// Show screen `next`; direction > 0 slides it in from the right (like a swipe left), else from the left.
static void gotoScreen(int next, int direction) {
    if (next == current) return;
    ui::pauseScreenAnimations(screens[current].obj, true); // hidden screens don't animate
    current = next;
    ui::pauseScreenAnimations(screens[current].obj, false);
    screens[current].refresh(); // fresh data before it slides in
    lv_screen_load_anim(screens[current].obj, direction > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        SLIDE_MS, 0, false);
    // onShow starts fades and grows. They take their start state now (no pop) but only begin once the
    // slide is over: the delay is added inside ui::animFadeIn / animValue / barChartGrow.
    if (screens[current].onShow) {
        ui::setShowDelay(UI_FADE_AFTER_SLIDE ? SLIDE_MS : 0);
        screens[current].onShow();
        ui::setShowDelay(0);
    }
}

// Called from ui_common.cpp's gesture handler
void navSwipe(int direction) { gotoScreen((current + direction + ui::SCREEN_COUNT) % ui::SCREEN_COUNT, direction); }

extern volatile uint8_t g_netPhase; // DIAG stutter: 0 idle, 1 gateway poll, 2 background job

static void refreshCb(lv_timer_t *) {
    uint32_t t0 = millis(); // DIAG
    energyUpdate(powerwallData()); // always, so today's totals keep counting
    uint32_t t1 = millis();
    screens[current].refresh();
    ui::topBarUpdateFor(current); // screens made with screenCreate(index) don't update their own top bar
    uint32_t t2 = millis();
    if (t2 - t0 > 50) Serial.printf("  [slow] refresh: energy %lu ms, screen %lu ms (net %u)\n", (unsigned long)(t1 - t0),
                                    (unsigned long)(t2 - t1), g_netPhase);
}

// Frame counter for the serial heartbeat (display refreshes that actually rendered something)
static uint32_t s_frames; // only touched on the LVGL core
static uint32_t s_maxGapMs;   // longest gap between rendered frames in the heartbeat window
// DIAG stutter: invalidated areas of the frame being rendered (count, pixels, bounding box)
static uint32_t s_invN, s_invPx, s_refrStart;
static lv_area_t s_invBox;
static constexpr uint32_t DIAG_AREAS = 16;
static lv_area_t s_invList[DIAG_AREAS]; // first areas of the frame, printed with a slow render
// O13 redraw load over the heartbeat window: invalidated pixels (overlaps counted twice) and flushes
static uint32_t s_invPxWindow, s_flushWindow;
static void invalidateCb(lv_event_t *e) {
    const lv_area_t *a = static_cast<const lv_area_t *>(lv_event_get_param(e));
    if (!a) return;
    if (s_invN < DIAG_AREAS) s_invList[s_invN] = *a;
    if (s_invN++ == 0) s_invBox = *a;
    else {
        s_invBox.x1 = LV_MIN(s_invBox.x1, a->x1);
        s_invBox.y1 = LV_MIN(s_invBox.y1, a->y1);
        s_invBox.x2 = LV_MAX(s_invBox.x2, a->x2);
        s_invBox.y2 = LV_MAX(s_invBox.y2, a->y2);
    }
    uint32_t px = lv_area_get_size(a);
    s_invPx += px;
    s_invPxWindow += px;
}
static void refrStartCb(lv_event_t *) { s_refrStart = millis(); }
static void flushStartCb(lv_event_t *) { s_flushWindow++; }
static void frameCb(lv_event_t *) {
    uint32_t renderMs = millis() - s_refrStart;
    if (renderMs > 100)
        Serial.printf("  [slow] render %lu ms: %lu areas, %lu px, box %ld,%ld-%ld,%ld\n", (unsigned long)renderMs,
                      (unsigned long)s_invN, (unsigned long)s_invPx, (long)s_invBox.x1, (long)s_invBox.y1,
                      (long)s_invBox.x2, (long)s_invBox.y2);
    if (renderMs > 100 && s_invN > 30) {
        Serial.print("  [slow] first areas:");
        for (uint32_t i = 0; i < s_invN && i < DIAG_AREAS; i++)
            Serial.printf(" %ld,%ld %ldx%ld", (long)s_invList[i].x1, (long)s_invList[i].y1,
                          (long)lv_area_get_width(&s_invList[i]), (long)lv_area_get_height(&s_invList[i]));
        Serial.println();
    }
    s_invN = s_invPx = 0;
    static uint32_t last;
    uint32_t now = millis(), gap = now - last;
    last = now;
    s_frames++;
    if (gap > s_maxGapMs) s_maxGapMs = gap;
    // While something animates, frames come every ~20-30 ms: a long gap is a visible stutter
    if (gap > 120 && gap < 2000) Serial.printf("  [stall] %lu ms at %lu\n", (unsigned long)gap, (unsigned long)now);
}

// Boot trace of internal RAM per step, to see who uses it
static void memStep(const char *what) {
    static size_t prev = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("  [mem] %-10s internal free %6u (%+d)\n", what, (unsigned)now, (int)now - (int)prev);
    prev = now;
}

static void halt(const char *msg) {
    Serial.println(msg);
    while (true) delay(1000);
}

// ---- serial commands (see the comment block at the top) ----

static void cmdReply(const char *msg) { Serial.printf("%s\n", msg); }

// Parses a whole-string integer in [lo, hi]; returns false if there is no number, junk after it, or a
// value out of range.
static bool parseInt(const char *s, long lo, long hi, long &out) {
    if (!s || !*s) return false;
    char *end = nullptr;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < lo || v > hi) return false;
    out = v;
    return true;
}

static void cmdGoto(int target) {
    char msg[64];
    if (target == current) {
        snprintf(msg, sizeof(msg), "screen %d %s (already shown) #ok", target, screens[target].name);
    } else {
        gotoScreen(target, target > current ? +1 : -1);
        snprintf(msg, sizeof(msg), "screen %d %s #ok", target, screens[target].name);
    }
    cmdReply(msg);
}

static void cmdList() {
    String line = "screens:";
    for (int i = 0; i < ui::SCREEN_COUNT; i++) {
        line += ' ';
        line += i;
        line += i == current ? '*' : ' ';
        line += screens[i].name;
        line += i == ui::SCREEN_COUNT - 1 ? "" : ",";
    }
    line += " #ok";
    cmdReply(line.c_str());
}

static void cmdMem() {
    lv_mem_monitor_t lm;
    lv_mem_monitor(&lm);
    char msg[220];
    snprintf(msg, sizeof(msg),
             "mem: internal free %lu, largest block %lu, min ever %lu | stack free net %lu lvgl %lu | lvgl pool %u%% "
             "used, %lu free, largest %lu | psram free %lu #ok",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned long)netTaskStackFree(),
             (unsigned long)uxTaskGetStackHighWaterMark(nullptr), (unsigned)lm.used_pct, (unsigned long)lm.free_size,
             (unsigned long)lm.free_biggest_size, (unsigned long)ESP.getFreePsram());
    cmdReply(msg);
}

// Screenshot: the native (unrotated) RGB565 framebuffer as one base64 line per row, "#px <row> <b64>",
// at SHOT_BAUD. Each line is a single Serial.write, so log lines from other tasks can interleave but
// never split a row. Runs in loop(), so LVGL isn't drawing meanwhile. Host side: tools-dev/screendump.py.
static constexpr uint32_t SHOT_BAUD = 921600;
static void cmdShot() {
    const uint16_t *fb = gfx->getFramebuffer();
    static char line[16 + 4 * ((B4848_LCD_WIDTH * 2 + 2) / 3) + 2]; // "#px nnn " + base64 + "\n"
    Serial.printf("#shot %d %d rgb565le native rot %d baud %lu\n", B4848_LCD_WIDTH, B4848_LCD_HEIGHT, SCREEN_ROTATION,
                  (unsigned long)SHOT_BAUD);
    Serial.flush();
    delay(200); // host switches its baud rate after reading the header
    Serial.updateBaudRate(SHOT_BAUD);
    delay(50);
    uint32_t crc = 0;
    for (int r = 0; r < B4848_LCD_HEIGHT; r++) {
        const uint8_t *row = reinterpret_cast<const uint8_t *>(fb + (size_t)r * B4848_LCD_WIDTH);
        crc = esp_rom_crc32_le(crc, row, B4848_LCD_WIDTH * 2); // = zlib crc32, chained over the rows
        int n = snprintf(line, sizeof(line), "#px %d ", r);
        size_t olen = 0;
        mbedtls_base64_encode(reinterpret_cast<unsigned char *>(line + n), sizeof(line) - n - 1, &olen, row,
                              B4848_LCD_WIDTH * 2);
        line[n + olen] = '\n';
        Serial.write(reinterpret_cast<const uint8_t *>(line), n + olen + 1);
    }
    Serial.printf("#shot end crc %08lx\n", (unsigned long)crc);
    Serial.flush();
    delay(200);
    Serial.updateBaudRate(115200);
    delay(50);
    cmdReply("shot #ok");
}

static void handleCommand(char *line) {
    while (*line == ' ' || *line == '\t') line++;
    char cmd = *line;
    if (!cmd) return; // empty line
    char *arg = line + 1;
    while (*arg == ' ' || *arg == '\t') arg++;
    // trim trailing blanks of the argument
    size_t n = strlen(arg);
    while (n && (arg[n - 1] == ' ' || arg[n - 1] == '\t')) arg[--n] = '\0';
    // "s3" works like "s 3", but a word such as "screens" is not "s"
    if ((line[1] >= 'a' && line[1] <= 'z') || (line[1] >= 'A' && line[1] <= 'Z')) {
        cmdReply("#err unknown command (h for help)");
        return;
    }
    long v = 0;
    switch (cmd) {
        case 's':
            if (!parseInt(arg, 0, ui::SCREEN_COUNT - 1, v)) cmdReply("#err usage: s <0..9>");
            else cmdGoto(static_cast<int>(v));
            break;
        case 'n':
            cmdGoto((current + 1) % ui::SCREEN_COUNT);
            break;
        case 'p':
            cmdGoto((current + ui::SCREEN_COUNT - 1) % ui::SCREEN_COUNT);
            break;
        case 'l':
            cmdList();
            break;
        case 'b':
            if (!parseInt(arg, 0, 255, v)) {
                cmdReply("#err usage: b <0-255>");
            } else {
                board4848::setBacklight(static_cast<uint8_t>(v)); // not persisted
                char msg[48];
                snprintf(msg, sizeof(msg), "backlight %ld #ok", v);
                cmdReply(msg);
            }
            break;
        case 'm':
            cmdMem();
            break;
        case 'x':
            cmdShot();
            break;
        case 'v':
            if (!parseInt(arg, 0, 1, v)) {
                cmdReply("#err usage: v <0|1>");
            } else {
                ui::setPrivacy(v != 0);
                screens[current].refresh();
                cmdReply(v ? "privacy on #ok" : "privacy off #ok");
            }
            break;
        case 'c':
            cache::printStatus();
            cmdReply("cache #ok");
            break;
        case 'r':
            cmdReply("rebooting #ok");
            Serial.flush();
            delay(100);
            ESP.restart();
            break;
        case 'h':
            cmdReply("commands: s <0..9> go to screen | n next | p previous | l list | b <0-255> backlight | m memory | "
                     "x screenshot | v <0|1> privacy | c cache | r reboot | h help #ok");
            break;
        default:
            cmdReply("#err unknown command (h for help)");
            break;
    }
}

// Collect characters without blocking; run a command at each end of line.
static void pollSerialCommands() {
    static char buf[40];
    static size_t len = 0;
    static bool overflow = false;
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (overflow) cmdReply("#err line too long");
            else if (len) {
                buf[len] = '\0';
                handleCommand(buf);
            }
            len = 0;
            overflow = false;
        } else if (len + 1 < sizeof(buf)) {
            buf[len++] = static_cast<char>(c);
        } else {
            overflow = true;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    memStep("boot");

    // Bounce buffers 2x the library default (480x20 px, ~38 KB internal): more slack for the panel's
    // PSRAM reads while TLS/JPEG work also uses PSRAM (480x10 showed crackling).
    gfx = board4848::initDisplay(SCREEN_ROTATION, B4848_LCD_WIDTH * 20);
    if (!gfx) halt("Display init failed (PSRAM missing? check memory_type = qio_opi)");
    gfx->fillScreen(RGB565_BLACK);
    memStep("display");
    board4848::setBacklight(settingsGet().brightness); // default until the card's settings are read

    // After initDisplay(): the SD card shares the LCD init-bus pins. Mounts in the background.
    historyStart();
    memStep("sd task");

    bool hasTouch = touch.begin();
    touch.setRotation(SCREEN_ROTATION);
    lv_display_t *disp = board4848::lvglBegin(gfx, hasTouch ? &touch : nullptr);
    if (!disp) halt("LVGL draw buffer alloc failed");
    // LV_USE_TJPGD only for space.cpp's raw decoder: don't let LVGL sniff every image it draws for JPEG
    lv_tjpgd_deinit();
    memStep("lvgl");
    lv_display_add_event_cb(disp, frameCb, LV_EVENT_REFR_READY, nullptr);
    lv_display_add_event_cb(disp, refrStartCb, LV_EVENT_REFR_START, nullptr);       // DIAG
    lv_display_add_event_cb(disp, invalidateCb, LV_EVENT_INVALIDATE_AREA, nullptr); // DIAG + redraw metric
    lv_display_add_event_cb(disp, flushStartCb, LV_EVENT_FLUSH_START, nullptr);     // redraw metric

    for (int i = 0; i < ui::SCREEN_COUNT; i++) {
        screens[i].obj = screens[i].create();
        memStep(screens[i].name);
    }
    lv_screen_load(screens[0].obj);
    for (int i = 1; i < ui::SCREEN_COUNT; i++) ui::pauseScreenAnimations(screens[i].obj, true);

    powerwallStart(2000);
    memStep("net task");
    lv_timer_create(refreshCb, 500, nullptr);

    Serial.printf("Free internal heap: %lu, PSRAM: %lu\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)ESP.getFreePsram());
}

void loop() {
    pollSerialCommands();

    // Sleep only until the next LVGL timer is due (capped), so frames aren't delayed
    static uint32_t loopEnd; // DIAG
    uint32_t h0 = millis();
    if (loopEnd && h0 - loopEnd > 100)
        Serial.printf("  [slow] loop not run for %lu ms (net %u)\n", (unsigned long)(h0 - loopEnd), g_netPhase);
    uint32_t idle = lv_timer_handler();
    uint32_t h1 = millis();
    if (h1 - h0 > 100) Serial.printf("  [slow] lv_timer_handler %lu ms (net %u)\n", (unsigned long)(h1 - h0), g_netPhase);
    delay(idle < 5 ? idle : 5);
    loopEnd = millis();

    // Serial heartbeat every 10 s
    static uint32_t last;
    if (millis() - last > 10000) {
        uint32_t windowMs = millis() - last; // the first window since boot is shorter than 10 s
        last = millis();
        uint32_t frames = s_frames, maxGap = s_maxGapMs;
        uint32_t invPx = s_invPxWindow, flushes = s_flushWindow;
        s_frames = 0;
        s_maxGapMs = 0;
        s_invPxWindow = 0;
        s_flushWindow = 0;
        PwData d = powerwallData();
        NetStatus n = networkStatus();
        TodayStats t = energyToday();
        uint32_t netStack = netTaskStackFree();
        Serial.printf("[%s %d dBm] solar %.0f home %.0f grid %.0f batt %.0f soc %.1f%% %s %s | today solar %.2f home %.2f "
                      "kWh | screen %d | %.1f fps, max gap %lu ms\n",
                      n.wifiUp ? n.ssid : "offline", n.rssi, d.solarW, d.loadW, d.siteW, d.batteryW, d.soc,
                      d.gridConnected ? "on-grid" : "OFF-GRID", d.error, t.solarKWh, t.homeKWh, current, frames / 10.0f,
                      (unsigned long)maxGap);
        // Long-uptime health: fragmentation shows as a shrinking largest block
        Serial.printf("  mem: internal free %lu, largest block %lu, min ever %lu | stack free net %lu lvgl %lu\n",
                      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned long)netStack,
                      (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
        lv_mem_monitor_t lm;
        lv_mem_monitor(&lm);
        SdStatus sd = historyStatus();
        Serial.printf("  lvgl pool: %u%% used, %lu free, largest %lu | sd %s %lu MHz, %lu lines, io err %lu\n",
                      (unsigned)lm.used_pct, (unsigned long)lm.free_size, (unsigned long)lm.free_biggest_size,
                      sd.mounted ? "ok" : "none", (unsigned long)sd.clockMHz, (unsigned long)sd.linesToday,
                      (unsigned long)sd.ioErrors);
        // Redraw load (the acceptance metric for idle-redraw work): invalidated px and flushes per second
        if (windowMs == 0) windowMs = 1;
        net::Stats ns = net::stats();
        Serial.printf("  redraw: %lu px/s, %lu flushes/s | net: %lu TLS connects, %lu requests, %lu failures\n",
                      (unsigned long)((uint64_t)invPx * 1000 / windowMs), (unsigned long)((uint64_t)flushes * 1000 / windowMs),
                      (unsigned long)ns.tlsConnects, (unsigned long)ns.requests, (unsigned long)ns.failures);
    }
}
