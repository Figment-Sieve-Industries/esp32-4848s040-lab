# Reference: prior art (git submodules, read-only)

Shallow submodules. Don't edit them; copy what you need into `common/` or a project.
`git submodule update --init` fetches them; `git submodule update --remote` updates them.
Check each licence before copying code. GPL-3.0 code can't go into closed or non-GPL projects.

## Pulled in

| Folder | Upstream | Licence | Stack | Why it's useful |
|---|---|---|---|---|
| `platformio-espressif32-sunton` | [rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton) | – | PlatformIO board JSONs | Board definitions `esp32-4848S040CIY1/CIY3`, datasheets, photos |
| `esp32-smartdisplay` | [rzeldent/esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay) | GPL-3.0 | Arduino + LVGL 9 + esp_lcd | ~700★, actively maintained. One LVGL abstraction for all Sunton boards: display, touch, backlight, RGB LED, light sensor. Good **architecture reference** for a board HAL, and the quickest LVGL route if GPL is fine. |
| `esp32-4848s040-st7701` | [aquaElectronics/esp32-4848s040-st7701](https://github.com/aquaElectronics/esp32-4848s040-st7701) | – | Arduino_GFX | ST7701 init table verified on hardware (used in our `common/` lib) |
| `ESP32-4848S040-LVGL-LGFX` | [joevial/ESP32-4848S040-LVGL-LGFX](https://github.com/joevial/ESP32-4848S040-LVGL-LGFX) | none stated | pioarduino + **LovyanGFX** + LVGL + **EEZ Studio** | Closest to our stack. Reference for an EEZ Studio UI-designer workflow, LovyanGFX (faster than Arduino_GFX, reported 200+ fps), a backlight coil-whine fix, a backlight-dimming hardware mod, and a 2nd I2C bus on GPIO1/40. No licence, so read only; don't copy. |
| `Guition-ESP32-S3-4848S040` | [alaltitov/Guition-ESP32-S3-4848S040](https://github.com/alaltitov/Guition-ESP32-S3-4848S040) | MIT | **ESPHome** + LVGL 8.4 | ~440★, the most popular firmware for this board. A full Home Assistant wall panel: climate, lights, media, weather, multi-language. Prior art for a HA panel UI and YAML layout. Docs: [alaltitov/…-DOCS](https://github.com/alaltitov/Guition-ESP32-S3-4848S040-DOCS) |
| `ESP32-4848S040-First-Start` | [abcbbck-png/ESP32-4848S040-First-Start](https://github.com/abcbbck-png/ESP32-4848S040-First-Start) | MIT | **ESP-IDF** (CMake + sdkconfig) + LVGL | Starter for the native ESP-IDF route: `sdkconfig.defaults`, partitions and `esp_lcd` setup |

## Snippets (`snippets/`)
| File | From |
|---|---|
| `openhasp-esp32-s3-4848S040.ini` | [openHASP](https://github.com/HASwitchPlate/openHASP) build config for this board. openHASP is a ready-made MQTT/HA touch-panel firmware; flash it if you only need a configurable panel and no custom code. |

## Vendor data pack
In `docs/vendor/`, from [NorthernMan54/ESP32-4848S040](https://github.com/NorthernMan54/ESP32-4848S040). That repo is 149 MB, so it isn't a submodule. Browse it online for the vendor Arduino demo and factory firmware images.

## Not pulled in (links only)
| Link | Notes |
|---|---|
| [moononournation/Arduino_GFX](https://github.com/moononournation/Arduino_GFX) | Our graphics lib (PlatformIO fetches it into `.pio/libdeps`). See `examples/LVGL/LVGL_Arduino_v9` and the `ESP32_4848S040_86BOX_GUITION` dev device. |
| [lovyan03/LovyanGFX](https://github.com/lovyan03/LovyanGFX) | Alternative graphics lib with DMA and its own touch drivers |
| [espressif/esp-bsp](https://github.com/espressif/esp-bsp) | `esp_lcd_st7701`, `esp_lcd_touch_gt911`, `esp_lvgl_port` components (ESP-IDF) |
| [PrayerfulDrop/ESPHome-Guition-ESP32-S3-4848S040](https://github.com/PrayerfulDrop/ESPHome-Guition-ESP32-S3-4848S040) | Another ESPHome HA panel (GPL-3.0), actively maintained |
| [kroon040/HA-Panel-GUITION](https://github.com/kroon040/HA-Panel-GUITION) | ESPHome HA panel |
| [Witaliy76/Yoradio_lvgl](https://github.com/Witaliy76/Yoradio_lvgl) | Internet radio with an LVGL UI. Prior art for **audio over the NS4168** on this board. |
| [APIUM/guition-4848-micropython-lvgl](https://github.com/APIUM/guition-4848-micropython-lvgl) | MicroPython + LVGL route |
| [ESPHome device page](https://devices.esphome.io/devices/guition-esp32-s3-4848s040) | Canonical ESPHome YAML |
| [ESPHome ST7701S](https://esphome.io/components/display/st7701s/) · [esp_lcd_st7701](https://components.espressif.com/components/espressif/esp_lcd_st7701) | Driver docs |
| [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html) | USB-serial driver |
