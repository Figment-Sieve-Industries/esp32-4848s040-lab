// 05-powerwall-display
// Live Tesla Powerwall 3 dashboard on the 4" panel, read from the gateway's local LAN API.
// Network polling runs on core 0 (powerwall.cpp); LVGL runs in loop() on core 1.
//
// Screens (swipe left/right): power flow, today, battery & grid, weather, moon, aurora, settings,
// about (see ui::ScreenIndex).
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

static void refreshCb(lv_timer_t *) {
    energyUpdate(powerwallData()); // always, so today's totals keep counting
    screens[current].refresh();
}

// Frame counter for the serial heartbeat (display refreshes that actually rendered something)
static uint32_t s_frames; // only touched on the LVGL core
static void frameCb(lv_event_t *) { s_frames++; }

static void halt(const char *msg) {
    Serial.println(msg);
    while (true) delay(1000);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    gfx = board4848::initDisplay(SCREEN_ROTATION);
    if (!gfx) halt("Display init failed (PSRAM missing? check memory_type = qio_opi)");
    gfx->fillScreen(RGB565_BLACK);
    board4848::setBacklight(settingsGet().brightness); // default until the card's settings are read

    // After initDisplay(): the SD card shares the LCD init-bus pins. Mounts in the background.
    historyStart();

    bool hasTouch = touch.begin();
    touch.setRotation(SCREEN_ROTATION);
    lv_display_t *disp = board4848::lvglBegin(gfx, hasTouch ? &touch : nullptr);
    if (!disp) halt("LVGL draw buffer alloc failed");
    lv_display_add_event_cb(disp, frameCb, LV_EVENT_REFR_READY, nullptr);

    for (auto &s : screens) s.obj = s.create();
    lv_screen_load(screens[0].obj);
    for (int i = 1; i < ui::SCREEN_COUNT; i++) ui::pauseScreenAnimations(screens[i].obj, true);

    powerwallStart(2000);
    lv_timer_create(refreshCb, 500, nullptr);

    Serial.printf("Free internal heap: %lu, PSRAM: %lu\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)ESP.getFreePsram());
}

void loop() {
    // Sleep only until the next LVGL timer is due (capped), so frames aren't delayed
    uint32_t idle = lv_timer_handler();
    delay(idle < 5 ? idle : 5);

    // Serial heartbeat every 10 s
    static uint32_t last;
    if (millis() - last > 10000) {
        last = millis();
        uint32_t frames = s_frames;
        s_frames = 0;
        PwData d = powerwallData();
        NetStatus n = networkStatus();
        TodayStats t = energyToday();
        uint32_t gwStack, wxStack;
        netTaskStackFree(gwStack, wxStack);
        Serial.printf("[%s %d dBm] solar %.0f home %.0f grid %.0f batt %.0f soc %.1f%% %s %s | today solar %.2f home %.2f "
                      "kWh | screen %d | %.1f fps\n",
                      n.wifiUp ? n.ssid : "offline", n.rssi, d.solarW, d.loadW, d.siteW, d.batteryW, d.soc,
                      d.gridConnected ? "on-grid" : "OFF-GRID", d.error, t.solarKWh, t.homeKWh, current, frames / 10.0f);
        // Long-uptime health: fragmentation shows as a shrinking largest block
        Serial.printf("  mem: internal free %lu, largest block %lu, min ever %lu | stack free gw %lu wx %lu\n",
                      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned long)gwStack,
                      (unsigned long)wxStack);
        lv_mem_monitor_t lm;
        lv_mem_monitor(&lm);
        SdStatus sd = historyStatus();
        Serial.printf("  lvgl pool: %u%% used, %lu free, largest %lu | sd %s %lu MHz, %lu lines, io err %lu\n",
                      (unsigned)lm.used_pct, (unsigned long)lm.free_size, (unsigned long)lm.free_biggest_size,
                      sd.mounted ? "ok" : "none", (unsigned long)sd.clockMHz, (unsigned long)sd.linesToday,
                      (unsigned long)sd.ioErrors);
    }
}
