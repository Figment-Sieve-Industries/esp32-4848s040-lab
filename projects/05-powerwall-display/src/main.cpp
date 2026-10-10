// 05-powerwall-display
// Live Tesla Powerwall 3 power flow on the 4" panel, read from the gateway's local LAN API.
// Network polling runs on core 0 (powerwall.cpp); LVGL runs in loop() on core 1.
// Tap the screen to cycle the backlight brightness.
//
// SCREEN_ROTATION: 0 = native, 1 = 90 deg clockwise, 2 = 180, 3 = 90 deg counter-clockwise.
// 3 puts the USB-C port at the bottom. Touch is rotated to match.
#include <Arduino.h>
#include <Board4848S040.h>
#include <Board4848S040_lvgl.h>
#include "powerwall.h"
#include "ui.h"

#ifndef SCREEN_ROTATION
#define SCREEN_ROTATION 3
#endif

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

void setBacklightLevel(uint8_t level) { board4848::setBacklight(level); }

static void refreshCb(lv_timer_t *) { uiRefresh(); }

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
    if (!board4848::lvglBegin(gfx, hasTouch ? &touch : nullptr)) halt("LVGL draw buffer alloc failed");

    uiCreate();
    powerwallStart(2000);
    lv_timer_create(refreshCb, 500, nullptr);

    Serial.printf("Free internal heap: %lu, PSRAM: %lu\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)ESP.getFreePsram());
}

void loop() {
    lv_timer_handler();
    delay(5);

    // Serial heartbeat every 10 s
    static uint32_t last;
    if (millis() - last > 10000) {
        last = millis();
        PwData d = powerwallData();
        NetStatus n = networkStatus();
        Serial.printf("[%s %d dBm] solar %.0f home %.0f grid %.0f batt %.0f soc %.1f%% %s %s | heap %lu\n",
                      n.wifiUp ? n.ssid : "offline", n.rssi, d.solarW, d.loadW, d.siteW, d.batteryW, d.soc,
                      d.gridConnected ? "on-grid" : "OFF-GRID", d.error,
                      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
}
