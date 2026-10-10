#pragma once
// LVGL 9 glue for the 4848S040. Only available when the project depends on lvgl/lvgl.
#if __has_include(<lvgl.h>)
#include <lvgl.h>
#include "Board4848S040.h"

namespace board4848 {

// Sets up LVGL for this board and returns the display:
// - lv_init() + millis() tick
// - partial render mode, bufLines-line draw buffer in internal RAM (120 lines = 115 KB; measured
//   ~30% faster rendering than 48 lines. Lower it if the project also needs internal RAM for Wi-Fi/BLE)
// - flush via gfx->draw16bitRGBBitmap (native RGB565, no byte swap)
// - pointer input from the polled GT911 (pass nullptr for no touch)
// Call lv_timer_handler() from loop(). Returns nullptr if the draw buffer can't be allocated.
#ifndef B4848_LVGL_BUF_LINES
#define B4848_LVGL_BUF_LINES 120
#endif
lv_display_t *lvglBegin(Arduino_RGB_Display *gfx, Touch *touch, uint16_t bufLines = B4848_LVGL_BUF_LINES);

} // namespace board4848

#endif
