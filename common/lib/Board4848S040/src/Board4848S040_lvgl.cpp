#include "Board4848S040_lvgl.h"
#if __has_include(<lvgl.h>)

namespace board4848 {

static Arduino_RGB_Display *s_gfx;
static Touch *s_touch;

static uint32_t tickCb() { return millis(); }

// Rotation 3 (USB-C at the bottom): logical pixel (x, y) lives at framebuffer row W-1-x, column y.
// Arduino_GFX's rotated draw writes the strip column-wise (each pixel on a different 960-byte row, so
// the PSRAM cache thrashes) and then writes back every full framebuffer row it touched: ~460 KB for a
// 480x48 strip that only changed 46 KB. Here each framebuffer row gets one contiguous segment, and only
// that segment is written back. Measured on project 05: see docs/hardware-log.md.
static void flushRot3(const lv_area_t *area, const uint16_t *px) {
    const int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    uint16_t *fb = s_gfx->getFramebuffer();
    for (int32_t i = 0; i < w; i++) {
        uint16_t *dst = fb + (size_t)(B4848_LCD_WIDTH - 1 - (area->x1 + i)) * B4848_LCD_WIDTH + area->y1;
        const uint16_t *src = px + i;
        for (int32_t j = 0; j < h; j++, src += w) dst[j] = *src;
        Cache_WriteBack_Addr((uint32_t)dst, (uint32_t)h * 2);
    }
}

static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    if (s_gfx->getRotation() == 3 && area->x1 >= 0 && area->y1 >= 0 && area->x2 < B4848_LCD_WIDTH &&
        area->y2 < B4848_LCD_HEIGHT) {
        flushRot3(area, (const uint16_t *)px);
    } else {
        s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, lv_area_get_width(area), lv_area_get_height(area));
    }
    lv_display_flush_ready(disp);
}

static void touchReadCb(lv_indev_t *, lv_indev_data_t *data) {
    // The GT911 only reports when it has new data, so keep the last state between reports.
    // A report with zero points means the finger lifted; the timeout is a fallback.
    static TouchPoint last;
    static bool pressed = false;
    static uint32_t lastSeen = 0;

    TouchPoint p;
    int8_t n = s_touch->read(&p, 1);
    if (n > 0) {
        last = p;
        pressed = true;
        lastSeen = millis();
    } else if (n == 0 || millis() - lastSeen > 100) {
        pressed = false;
    }
    data->point.x = last.x;
    data->point.y = last.y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_display_t *lvglBegin(Arduino_RGB_Display *gfx, Touch *touch, uint16_t bufLines) {
    s_gfx = gfx;
    s_touch = touch;

    lv_init();
    lv_tick_set_cb(tickCb);

    const size_t bufBytes = (size_t)gfx->width() * bufLines * 2;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_8BIT); // PSRAM fallback: slower
    if (!buf) return nullptr;

    lv_display_t *disp = lv_display_create(gfx->width(), gfx->height());
    lv_display_set_flush_cb(disp, flushCb);
    lv_display_set_buffers(disp, buf, nullptr, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    if (touch) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touchReadCb);
    }
    return disp;
}

} // namespace board4848

#endif
