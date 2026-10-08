#pragma once
// LVGL 9 glue for the 4848S040. Only available when the project depends on lvgl/lvgl.
#if __has_include(<lvgl.h>)
#include <lvgl.h>
#include "Board4848S040.h"

namespace board4848 {

// Sets up LVGL for this board and returns the display:
// - lv_init() + millis() tick
// - partial render mode, 48-line (~46 KB) draw buffer in internal RAM
// - flush via gfx->draw16bitRGBBitmap (native RGB565, no byte swap)
// - pointer input from the polled GT911 (pass nullptr for no touch)
// Call lv_timer_handler() from loop(). Returns nullptr if the draw buffer can't be allocated.
lv_display_t *lvglBegin(Arduino_RGB_Display *gfx, Touch *touch);

} // namespace board4848

#endif
