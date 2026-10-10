// 04-lvgl-fb2-bench
// Boots straight into LVGL's Benchmark on the double-framebuffer path:
// board4848::lvglBeginDoubleBuffered() (2 PSRAM framebuffers, vsync swap, DIRECT mode;
// FULL mode with -DFB2_FULL_REFRESH=1). Results go to serial like 03-lvgl-demos [env:bench],
// plus a line of swap statistics every 5 s.
#include <Arduino.h>
#include <Board4848S040.h>
#include <Board4848S040_lvgl_fb2.h>
#include <demos/lv_demos.h>

#ifndef FB2_FULL_REFRESH
#define FB2_FULL_REFRESH 0
#endif

static board4848::Touch touch;

static void halt(const char *msg) {
    Serial.println(msg);
    while (true) delay(1000);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    bool hasTouch = touch.begin();
    if (!hasTouch) Serial.println("GT911 not found");

    if (!board4848::lvglBeginDoubleBuffered(hasTouch ? &touch : nullptr, FB2_FULL_REFRESH))
        halt("Double-buffered display init failed (PSRAM missing? check memory_type = qio_opi)");
    board4848::setBacklight(255);

    Serial.printf("fb2 mode: %s\n", FB2_FULL_REFRESH ? "FULL" : "DIRECT");
    Serial.printf("Free internal heap: %lu, PSRAM: %lu\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)ESP.getFreePsram());

    lv_demo_benchmark();
}

void loop() {
    lv_timer_handler();
    delay(2);

    static uint32_t lastReport = 0;
    if (millis() - lastReport >= 5000) {
        lastReport = millis();
        board4848::Fb2Stats s = board4848::fb2Stats(true);
        Serial.printf("fb2: %lu presents/5s (%.1f/s), avg wait %.1f ms, max wait %.1f ms, timeouts %lu\n",
                      (unsigned long)s.presents, s.presents / 5.0f,
                      s.presents ? s.waitUs / 1000.0f / s.presents : 0.0f, s.maxWaitUs / 1000.0f,
                      (unsigned long)s.vsyncTimeouts);
    }
}
