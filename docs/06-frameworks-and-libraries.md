# Frameworks and libraries

## 1. PlatformIO + Arduino (used in `projects/`)
- Platform: **pioarduino** (`platform-espressif32` fork with Arduino core 3.x). The official PlatformIO espressif32 platform is stuck on core 2.x, and Arduino_GFX 1.6.x needs 3.x.
- Board: `esp32-s3-devkitc-1` with `qio_opi`, 16 MB flash. Alternatively, use rzeldent's ready-made board JSONs (`reference/platformio-espressif32-sunton`, `esp32-4848S040CIY1`/`CIY3`) by putting that repo in the project's `boards/` folder.
- Graphics: **[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)** (`moononournation/GFX Library for Arduino`). It has `Arduino_ESP32RGBPanel` + `Arduino_RGB_Display`.
- Shared code for this repo: `common/lib/Board4848S040`.

## 2. LVGL (rich UIs)
- LVGL 9.x on top of Arduino_GFX (flush with `gfx->draw16bitRGBBitmap`), or on top of `esp_lcd` in ESP-IDF.
- **[esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay)** (rzeldent) wraps LVGL, display, touch and backlight for all Sunton boards, including the 4848S040. It is the fastest way to get LVGL running. Use it together with the board JSONs above.
- **SquareLine Studio** or **EEZ Studio** for drag-and-drop UI design. Export LVGL code into a project.

## 3. ESP-IDF (native)
- Components: `espressif/esp_lcd_st7701` (3-wire SPI + RGB), `espressif/esp_lcd_touch_gt911`, `espressif/esp_lvgl_port`.
- Use `esp_lcd_new_panel_io_3wire_spi()` for init, then `esp_lcd_new_rgb_panel()` with the timings from [04-display-st7701.md](04-display-st7701.md).
- Enable `CONFIG_SPIRAM_MODE_OCT` and `CONFIG_SPIRAM_SPEED_80M`. Consider `CONFIG_LCD_RGB_RESTART_IN_VSYNC` to avoid drift.

## 4. ESPHome / Home Assistant (wall panel)
A ready config is on the ESPHome devices site: https://devices.esphome.io/devices/guition-esp32-s3-4848s040
It covers the display (`st7701s`), GT911 touch, backlight `light`, the 3 relays as `switch`, and LVGL. This is the easiest path if the screen is meant to be a Home Assistant wall controller.

## 5. Others
- **Tasmota** has universal display support for this board (via a DisplayConfig descriptor).
- **NuttX** supports similar Sunton S3 boards.
- **MicroPython**: `lvgl_micropython` supports ST7701 RGB with GT911.
