// 03-lvgl-demos
// A touch launcher for LVGL's official demos. The chosen demo is stored in NVS and the board
// restarts into it (LVGL demos aren't designed to be torn down). The choice is one-shot: it's
// cleared as soon as it's read, so any restart (home button, reset, crash) lands on the launcher.
#include <Arduino.h>
#include <Preferences.h>
#include <Board4848S040.h>
#include <Board4848S040_lvgl.h>
#include <demos/lv_demos.h>

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

struct Demo {
    const char *name;
    const char *blurb;
    void (*run)();
};

static const Demo DEMOS[] = {
    {"Widgets", "Dashboard-style UI: charts, forms, lists", lv_demo_widgets},
    {"Music", "Animated music player (square layout)", lv_demo_music},
    {"Benchmark", "Measures FPS across scenes, prints results to serial", lv_demo_benchmark},
    {"Stress", "Heavy animation torture test", lv_demo_stress},
};
static constexpr int DEMO_COUNT = sizeof(DEMOS) / sizeof(DEMOS[0]);
static constexpr int LAUNCHER = -1;

static void restartInto(int choice) {
    Preferences prefs;
    prefs.begin("demos", false);
    prefs.putInt("choice", choice);
    prefs.end();
    delay(50);
    ESP.restart();
}

static void homeCb(lv_event_t *) { restartInto(LAUNCHER); }

static void demoButtonCb(lv_event_t *e) {
    restartInto((int)(intptr_t)lv_event_get_user_data(e));
}

static void addHomeButton() {
    lv_obj_t *btn = lv_button_create(lv_layer_top());
    lv_obj_set_size(btn, 56, 56);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(btn, homeCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, LV_SYMBOL_HOME);
    lv_obj_center(icon);
}

static void buildLauncher() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 20, 0);
    lv_obj_set_style_pad_row(scr, 14, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_label_set_text(title, "LVGL demos");

    for (int i = 0; i < DEMO_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(scr);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 78);
        lv_obj_add_event_cb(btn, demoButtonCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(btn);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
        lv_label_set_text(name, DEMOS[i].name);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *blurb = lv_label_create(btn);
        lv_obj_set_style_text_font(blurb, &lv_font_montserrat_16, 0);
        lv_label_set_text(blurb, DEMOS[i].blurb);
        lv_obj_align(blurb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

static void halt(const char *msg) {
    Serial.println(msg);
    while (true) delay(1000);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    gfx = board4848::initDisplay();
    if (!gfx) halt("Display init failed (PSRAM missing? check memory_type = qio_opi)");
    gfx->fillScreen(RGB565_BLACK);
    board4848::setBacklight(255);

    bool hasTouch = touch.begin();
    if (!hasTouch) Serial.println("GT911 not found");
    if (!board4848::lvglBegin(gfx, hasTouch ? &touch : nullptr)) halt("LVGL draw buffer alloc failed");

    Preferences prefs;
    prefs.begin("demos", false);
    int choice = prefs.getInt("choice", LAUNCHER);
    prefs.putInt("choice", LAUNCHER); // one-shot
    prefs.end();

    if (choice >= 0 && choice < DEMO_COUNT) {
        Serial.printf("Running demo: %s\n", DEMOS[choice].name);
        DEMOS[choice].run();
        addHomeButton();
    } else {
        Serial.println("Launcher");
        buildLauncher();
    }
}

void loop() {
    lv_timer_handler();
    delay(2);
}
