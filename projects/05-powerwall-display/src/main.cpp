// 05-powerwall-display
// Live Tesla Powerwall 3 dashboard on the 4" panel, read from the gateway's local LAN API.
// Network polling runs on core 0 (powerwall.cpp); LVGL runs in loop() on core 1.
//
// Screens (swipe left/right): B power flow, G today, H battery & grid, I weather.
// Tap anywhere to cycle the backlight brightness.
//
// SCREEN_ROTATION: 0 = native, 1 = 90 deg clockwise, 2 = 180, 3 = 90 deg counter-clockwise.
// 3 puts the USB-C port at the bottom. Touch is rotated to match.
//
// Nothing is written to flash at runtime: today's totals live in RTC memory (energy.cpp).
#include <Arduino.h>
#include <Board4848S040.h>
#include <Board4848S040_lvgl.h>
#include "energy.h"
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
};
static int current = 0;

// ---- navigation hooks (called from ui_common.cpp) ----
void navSwipe(int direction) {
    int next = (current + direction + ui::SCREEN_COUNT) % ui::SCREEN_COUNT;
    if (next == current) return;
    current = next;
    screens[current].refresh(); // fresh data before it slides in
    lv_screen_load_anim(screens[current].obj, direction > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        300, 0, false);
    if (screens[current].onShow) screens[current].onShow();
}

static uint8_t brightness = 255;
void navTap() {
    brightness = brightness == 255 ? 160 : brightness == 160 ? 100 : 255;
    board4848::setBacklight(brightness);
}

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
    board4848::setBacklight(255);

    bool hasTouch = touch.begin();
    touch.setRotation(SCREEN_ROTATION);
    lv_display_t *disp = board4848::lvglBegin(gfx, hasTouch ? &touch : nullptr);
    if (!disp) halt("LVGL draw buffer alloc failed");
    lv_display_add_event_cb(disp, frameCb, LV_EVENT_REFR_READY, nullptr);

    for (auto &s : screens) s.obj = s.create();
    lv_screen_load(screens[0].obj);

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
        Serial.printf("[%s %d dBm] solar %.0f home %.0f grid %.0f batt %.0f soc %.1f%% %s %s | today solar %.2f home %.2f "
                      "kWh | screen %d | heap %lu | %.1f fps\n",
                      n.wifiUp ? n.ssid : "offline", n.rssi, d.solarW, d.loadW, d.siteW, d.batteryW, d.soc,
                      d.gridConnected ? "on-grid" : "OFF-GRID", d.error, t.solarKWh, t.homeKWh, current,
                      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), frames / 10.0f);
    }
}
