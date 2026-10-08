// 02-lvgl-hello
// LVGL 9 on the 4848S040 using the shared glue (board4848::lvglBegin).
// UI: a button counter and a slider driving the backlight.
#include <Arduino.h>
#include <Board4848S040.h>
#include <Board4848S040_lvgl.h>

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

static void sliderCb(lv_event_t *e) {
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    board4848::setBacklight((uint8_t)lv_slider_get_value(slider));
}

static void buttonCb(lv_event_t *e) {
    static uint32_t count = 0;
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_label_set_text_fmt(label, "Pressed %lu times", (unsigned long)++count);
}

static void buildUi() {
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_label_set_text(title, "ESP32-4848S040");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *countLabel = lv_label_create(scr);
    lv_label_set_text(countLabel, "Pressed 0 times");
    lv_obj_align(countLabel, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 70);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(btn, buttonCb, LV_EVENT_CLICKED, countLabel);
    lv_obj_t *btnLabel = lv_label_create(btn);
    lv_label_set_text(btnLabel, "Press me");
    lv_obj_center(btnLabel);

    lv_obj_t *blLabel = lv_label_create(scr);
    lv_label_set_text(blLabel, LV_SYMBOL_IMAGE "  Backlight");
    lv_obj_align(blLabel, LV_ALIGN_BOTTOM_MID, 0, -110);

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 360);
    lv_slider_set_range(slider, B4848_LCD_BL_MIN, 255);
    lv_slider_set_value(slider, 255, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_add_event_cb(slider, sliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
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

    buildUi();
    Serial.println("LVGL ready");
}

void loop() {
    lv_timer_handler();
    delay(5);
}
