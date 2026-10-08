// 02-lvgl-hello
// LVGL 9 on the 4848S040: partial rendering into an internal-RAM buffer, flushed via Arduino_GFX.
// UI: a slider driving the backlight, a button counter and a label.
#include <Arduino.h>
#include <lvgl.h>
#include <Board4848S040.h>

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

static constexpr uint32_t BUF_LINES = 48; // 1/10 of the screen
static uint8_t *drawBuf;

static uint32_t tickCb() { return millis(); }

static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    // RGB panel takes native little-endian RGB565: no byte swap needed.
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px,
                            lv_area_get_width(area), lv_area_get_height(area));
    lv_display_flush_ready(disp);
}

static void touchReadCb(lv_indev_t *, lv_indev_data_t *data) {
    // Poll the GT911 for one point. LVGL calls this every LV_DEF_REFR_PERIOD ms.
    // The GT911 only reports when its buffer has new data, so remember the last state.
    static board4848::TouchPoint last;
    static bool pressed = false;
    static uint32_t lastSeen = 0;

    board4848::TouchPoint p;
    if (touch.read(&p, 1)) {
        last = p;
        pressed = true;
        lastSeen = millis();
    } else if (millis() - lastSeen > 60) {
        pressed = false;
    }
    data->point.x = last.x;
    data->point.y = last.y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

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
    lv_slider_set_range(slider, 10, 255);
    lv_slider_set_value(slider, 255, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_add_event_cb(slider, sliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    gfx = board4848::initDisplay();
    if (!gfx) {
        Serial.println("Display init failed");
        while (true) delay(1000);
    }
    gfx->fillScreen(RGB565_BLACK);
    board4848::setBacklight(255);

    if (!touch.begin()) Serial.println("GT911 not found");

    lv_init();
    lv_tick_set_cb(tickCb);

    lv_display_t *disp = lv_display_create(B4848_LCD_WIDTH, B4848_LCD_HEIGHT);
    lv_display_set_flush_cb(disp, flushCb);
    const size_t bufBytes = B4848_LCD_WIDTH * BUF_LINES * 2;
    drawBuf = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!drawBuf) drawBuf = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_8BIT);
    lv_display_set_buffers(disp, drawBuf, nullptr, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchReadCb);

    buildUi();
    Serial.println("LVGL ready");
}

void loop() {
    lv_timer_handler();
    delay(5);
}
