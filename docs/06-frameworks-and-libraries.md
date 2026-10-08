# Frameworks and libraries

## Arduino vs ESP-IDF: trade-offs

This section lays out the trade-offs. No decision has been made; `projects/` currently use Arduino via PlatformIO because that was the fastest way to bring the board up.

**It's not either/or.** Arduino-ESP32 core 3.x is a layer on top of ESP-IDF 5.x. An Arduino sketch can call ESP-IDF APIs directly (`esp_lcd_*`, `heap_caps_malloc`, FreeRTOS, `esp_pm`), and pioarduino can also run Arduino *as a component inside* an ESP-IDF project.

### Arduino (PlatformIO + pioarduino) strengths
- **Fast iteration:** Wi-Fi, HTTP, MQTT, JSON, OTA, LovyanGFX and Arduino_GFX are each one `lib_deps` line.
- **Prior art:** nearly all code for this board (vendor demo, smartdisplay, joevial, aquaElectronics) is Arduino, so it can be reused directly.
- **Quick rebuilds:** the core is precompiled, so incremental builds take seconds.
- **Gentle learning curve:** `setup()`/`loop()`, with FreeRTOS available when needed.

### Arduino weaknesses (on this board specifically)
- **Fixed sdkconfig.** The precompiled core bakes in options that matter for a PSRAM-framebuffer RGB panel:
  - `CONFIG_LCD_RGB_RESTART_IN_VSYNC` (auto-recover from drift)
  - `CONFIG_LCD_RGB_ISR_IRAM_SAFE`
  - `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` / `CONFIG_SPIRAM_RODATA` (reduce PSRAM-bus contention while flash is busy)
  - PSRAM and cache tuning
- **Less control** over the memory layout, power management and component versions.
- **Library quality varies.** Some Arduino libraries hide blocking calls or allocate memory carelessly.

### ESP-IDF strengths
- **Full `menuconfig` control** over the panel, PSRAM, cache, Wi-Fi buffers, power management and logging.
- **First-party components:** `esp_lcd_st7701`, `esp_lcd_touch_gt911` and `esp_lvgl_port`. These include tear-free double framebuffers synced to vsync, bounce buffers, and an LVGL task with locking.
- **Better tooling for serious products:** secure boot, flash encryption, OTA with rollback, core dumps, app tracing, unit tests.
- **Predictable behaviour:** you own the whole configuration.

### ESP-IDF weaknesses
- More boilerplate: component manager, CMake, event loops and explicit init for Wi-Fi, NVS and netif.
- Slower full builds (everything compiles from source).
- Less copy-paste prior art for this exact board; most examples need porting.
- Steeper learning curve.

### Middle-ground options (in order of effort)
1. **Stay Arduino and use ESP-IDF APIs directly** for the parts that need them. Example: `bounce_buffer_size_px` in `Arduino_ESP32RGBPanel`, the first fix for display drift under Wi-Fi load.
2. **pioarduino `custom_sdkconfig`**: rebuilds the Arduino core libraries with your own sdkconfig lines. You keep the Arduino APIs and get the tuning. The cost is a long first build.
3. **Hybrid** `framework = arduino, espidf`: a full ESP-IDF project with Arduino as a component.
4. **Pure ESP-IDF**: see `reference/ESP32-4848S040-First-Start`.

### Signals that point towards ESP-IDF
- Display drift or tearing that bounce buffers and a lower PCLK don't fix
- Battery operation needing deep or light sleep and fine power management
- A product-grade update or security story (rollback, secure boot)
- Wanting `esp_lvgl_port`'s vsync-synced double buffering

### Signals that favour staying on Arduino
- Rapid prototyping and many integrations (HA, MQTT, web)
- Reusing existing Arduino code for this board

## 1. PlatformIO + Arduino (used in `projects/`)
- **Platform:** **pioarduino** (`platform-espressif32` fork with Arduino core 3.x), pinned to release `54.03.21-2` (Arduino core 3.2.1, ESP-IDF 5.4). The official PlatformIO espressif32 platform is stuck on core 2.x, and Arduino_GFX 1.6.x needs 3.x. Newer pioarduino releases need PlatformIO Core ≥ 6.2 (`pio upgrade`).
- **Shared settings:** all of these live in `common/platformio-base.ini`, section `[base_4848s040]`.
  - board `esp32-s3-devkitc-1`, `qio_opi`, 16 MB, `default_16MB.csv`
  - build flags
  - monitor and upload speed
  - `monitor_filters = esp32_exception_decoder`
  - `lib_deps = symlink://../../common/lib/Board4848S040`

  Each project's `platformio.ini` is just:
  ```ini
  [platformio]
  extra_configs = ../../common/platformio-base.ini

  [env:esp32-4848s040]
  extends = base_4848s040
  ; add project-specific lib_deps / build_flags here, e.g.
  ; lib_deps = ${base_4848s040.lib_deps}
  ;     lvgl/lvgl @ 9.5.0
  ```
  Projects keep their own `lib/` folder for private libraries.
- **Pinned versions:** Arduino_GFX is pinned to exactly **1.6.9**, in one place only (`common/lib/Board4848S040/library.json`). LVGL is pinned per project (02 uses **9.5.0**).
- **Board JSONs (optional):** rzeldent's `esp32-4848S040CIY1.json` / `CIY3.json` are in `common/boards/`. To use them, set `boards_dir = ../../common/boards` and `board = esp32-4848S040CIY1`. They define `LED_GPIO=16`, which is actually HSYNC, so **don't use that define**.
- **Graphics:** **[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)** (`moononournation/GFX Library for Arduino`), via `Arduino_ESP32RGBPanel` + `Arduino_RGB_Display`. **LovyanGFX** is a faster alternative with a built-in `Panel_ST7701_guition_esp32_4848S040` and `Touch_GT911`; see `reference/ESP32-4848S040-LVGL-LGFX/src/monolith.cpp`.
- **Shared library:** `common/lib/Board4848S040`.
  - `initDisplay()`, `setBacklight()`
  - `Touch` (GT911, with `setRotation`)
  - `lvglBegin()` in `Board4848S040_lvgl.h`, compiled only when LVGL is present

## 2. LVGL (rich UIs)
- LVGL 9.x on top of Arduino_GFX (flush with `gfx->draw16bitRGBBitmap`), or on top of `esp_lcd` in ESP-IDF.
- **[esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay)** (rzeldent) wraps LVGL, display, touch and backlight for all Sunton boards, including the 4848S040. It is the fastest way to get LVGL running. Use it together with the board JSONs in `common/boards/`. Our own `board4848::lvglBegin()` is the lighter in-repo equivalent.
- **SquareLine Studio** or **EEZ Studio** for drag-and-drop UI design. Export LVGL code into a project. joevial's repo shows the EEZ layout: generated `ui.c` / `screens.c`, with `ui_init()` and `ui_tick()` called from the loop.

## 3. ESP-IDF (native)
- Components: `espressif/esp_lcd_st7701` (3-wire SPI + RGB), `espressif/esp_lcd_touch_gt911`, `espressif/esp_lvgl_port`.
- Use `esp_lcd_new_panel_io_3wire_spi()` for init, then `esp_lcd_new_rgb_panel()` with the timings from [04-display-st7701.md](04-display-st7701.md).
- Enable `CONFIG_SPIRAM_MODE_OCT` and `CONFIG_SPIRAM_SPEED_80M`. Consider `CONFIG_LCD_RGB_RESTART_IN_VSYNC` to avoid drift.

## 4. ESPHome / Home Assistant (wall panel)
A ready config is on the ESPHome devices site: https://devices.esphome.io/devices/guition-esp32-s3-4848s040
It covers the display (`st7701s`), GT911 touch, backlight `light`, the 3 relays as `switch`, and LVGL. Newer ESPHome also offers a shortcut, `display: platform: mipi_rgb` with `model: GUITION-4848S040`. `reference/Guition-ESP32-S3-4848S040/src/main.yaml` has it commented out as an alternative and actually uses `platform: st7701s`. Its touch `on_release` resumes a paused LVGL and turns the backlight back on (the screen-off pattern in [08](08-power-battery-sleep.md)). This is the easiest path if the screen is meant to be a Home Assistant wall controller. Useful extras: `psram` octal at 80 MHz, the sdkconfig options `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` / `CONFIG_SPIRAM_RODATA`, and `ignore_strapping_warning: true` on GPIO45.

## 5. Others
- **Tasmota** has universal display support for this board (via a DisplayConfig descriptor).
- **NuttX** supports similar Sunton S3 boards.
- **MicroPython**: `lvgl_micropython` supports ST7701 RGB with GT911.
