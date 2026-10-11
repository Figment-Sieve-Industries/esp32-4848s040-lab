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
#include <Arduino.h>
#include <Board4848S040.h>
#include <Board4848S040_lvgl.h>
#include "energy.h"
#include "history.h"
#include "powerwall.h"
#include "screens.h"
#include "ui_common.h"

#ifndef SCREEN_ROTATION
#define SCREEN_ROTATION 3
#endif

// LVGL renders in loopTask: the default 8 KB left under 1 KB free (and overflowed once with LVGL's
// JPEG decoder registered). The heartbeat prints what is left ("stack free ... lvgl").
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

struct ScreenDef {
    lv_obj_t *(*create)();
    void (*refresh)();
    void (*onShow)();
    lv_obj_t *obj;
};

static ScreenDef screens[ui::SCREEN_COUNT] = {
    {houseCreate, houseRefresh, nullptr, nullptr},
    {todayCreate, todayRefresh, todayOnShow, nullptr},
    {healthCreate, healthRefresh, healthOnShow, nullptr},
    {weatherCreate, weatherRefresh, weatherOnShow, nullptr},
    {moonCreate, moonRefresh, moonOnShow, nullptr},
    {auroraCreate, auroraRefresh, auroraOnShow, nullptr},
    {sunCreate, sunRefresh, sunOnShow, nullptr},
    {ensoCreate, ensoRefresh, ensoOnShow, nullptr},
    {settingsCreate, settingsRefresh, settingsOnShow, nullptr},
    {aboutCreate, aboutRefresh, nullptr, nullptr},
};
static_assert(sizeof(screens) / sizeof(screens[0]) == ui::SCREEN_COUNT, "one entry per screen, in ScreenIndex order");
static int current = 0;

// ---- navigation hooks (called from ui_common.cpp) ----
void navSwipe(int direction) {
    int next = (current + direction + ui::SCREEN_COUNT) % ui::SCREEN_COUNT;
    if (next == current) return;
    ui::pauseScreenAnimations(screens[current].obj, true); // hidden screens don't animate
    current = next;
    ui::pauseScreenAnimations(screens[current].obj, false);
    screens[current].refresh(); // fresh data before it slides in
    lv_screen_load_anim(screens[current].obj, direction > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        300, 0, false);
    if (screens[current].onShow) screens[current].onShow();
}

// Brightness lives on the settings screen now; a tap does nothing.
void navTap() {}

extern volatile uint8_t g_netPhase; // DIAG stutter: 0 idle, 1 gateway poll, 2 background job

static void refreshCb(lv_timer_t *) {
    uint32_t t0 = millis(); // DIAG
    energyUpdate(powerwallData()); // always, so today's totals keep counting
    uint32_t t1 = millis();
    screens[current].refresh();
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
static void invalidateCb(lv_event_t *e) {
    const lv_area_t *a = static_cast<const lv_area_t *>(lv_event_get_param(e));
    if (!a) return;
    if (s_invN++ == 0) s_invBox = *a;
    else {
        s_invBox.x1 = LV_MIN(s_invBox.x1, a->x1);
        s_invBox.y1 = LV_MIN(s_invBox.y1, a->y1);
        s_invBox.x2 = LV_MAX(s_invBox.x2, a->x2);
        s_invBox.y2 = LV_MAX(s_invBox.y2, a->y2);
    }
    s_invPx += lv_area_get_size(a);
}
static void refrStartCb(lv_event_t *) { s_refrStart = millis(); }
static void frameCb(lv_event_t *) {
    uint32_t renderMs = millis() - s_refrStart;
    if (renderMs > 100)
        Serial.printf("  [slow] render %lu ms: %lu areas, %lu px, box %ld,%ld-%ld,%ld\n", (unsigned long)renderMs,
                      (unsigned long)s_invN, (unsigned long)s_invPx, (long)s_invBox.x1, (long)s_invBox.y1,
                      (long)s_invBox.x2, (long)s_invBox.y2);
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
    lv_display_add_event_cb(disp, refrStartCb, LV_EVENT_REFR_START, nullptr);     // DIAG
    lv_display_add_event_cb(disp, invalidateCb, LV_EVENT_INVALIDATE_AREA, nullptr); // DIAG

    static const char *const kNames[ui::SCREEN_COUNT] = {"house", "today", "health", "weather",
                                                        "moon", "aurora", "sun", "enso", "settings", "about"};
    for (int i = 0; i < ui::SCREEN_COUNT; i++) {
        screens[i].obj = screens[i].create();
        memStep(kNames[i]);
    }
    lv_screen_load(screens[0].obj);
    for (int i = 1; i < ui::SCREEN_COUNT; i++) ui::pauseScreenAnimations(screens[i].obj, true);

    powerwallStart(2000);
    memStep("net tasks");
    lv_timer_create(refreshCb, 500, nullptr);

    Serial.printf("Free internal heap: %lu, PSRAM: %lu\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)ESP.getFreePsram());
}

void loop() {
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
        last = millis();
        uint32_t frames = s_frames, maxGap = s_maxGapMs;
        s_frames = 0;
        s_maxGapMs = 0;
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
    }
}
