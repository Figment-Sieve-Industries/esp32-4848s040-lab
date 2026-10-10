#pragma once
// Opt-in LVGL 9 path with two full PSRAM framebuffers and a vsync-synchronised swap.
// Only available when the project depends on lvgl/lvgl.
//
// Unlike initDisplay() + lvglBegin(), this does NOT use Arduino_GFX for the RGB panel:
// Arduino_ESP32RGBPanel hard-codes num_fbs = 1. Here the ESP-IDF RGB driver owns two
// framebuffers, LVGL renders straight into the hidden one (DIRECT or FULL mode), and the
// flush callback presents it with esp_lcd_panel_draw_bitmap() (a pointer swap, no copy),
// then blocks until the bounce-buffer ISR has switched over to it at a frame boundary.
// Don't mix with initDisplay(): only one of them may own the panel.
#if __has_include(<lvgl.h>)
#include <lvgl.h>
#include "Board4848S040.h"

namespace board4848 {

// Sets up the ST7701 (3-wire SPI init), the RGB panel with 2 PSRAM framebuffers and
// B4848_LCD_BOUNCE_PX bounce buffers, and LVGL (lv_init, millis() tick, display, touch indev).
// - fullRefresh = false: LV_DISPLAY_RENDER_MODE_DIRECT. LVGL redraws only dirty areas and
//   copies the previous frame's dirty areas into the other buffer itself (lv_refr.c,
//   refr_sync_areas) so both buffers stay identical outside the newly drawn areas.
// - fullRefresh = true: LV_DISPLAY_RENDER_MODE_FULL. Every frame redraws the whole screen.
// Rotation 0 only (the panel is scanned out as-is). pass touch = nullptr for no touch.
// Call lv_timer_handler() from loop(). Returns nullptr on failure (message on Serial).
lv_display_t *lvglBeginDoubleBuffered(Touch *touch, bool fullRefresh = false);

struct Fb2Stats {
    uint32_t presents;      // frames handed to the panel (draw_bitmap calls)
    uint32_t vsyncTimeouts; // waits that hit the timeout (should stay 0)
    uint64_t waitUs;        // total time the flush callback spent waiting for the swap
    uint32_t maxWaitUs;     // longest single wait
};
// Snapshot of the counters since start (or since the last reset).
Fb2Stats fb2Stats(bool reset = false);

} // namespace board4848

#endif
