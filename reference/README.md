# Reference repos (git submodules, read-only)

| Folder | Upstream | Why it's here |
|---|---|---|
| `platformio-espressif32-sunton` | https://github.com/rzeldent/platformio-espressif32-sunton | PlatformIO board JSONs for all Sunton boards (`esp32-4848S040CIY1.json`, `CIY3`), datasheets, board photos |
| `esp32-4848s040-st7701` | https://github.com/aquaElectronics/esp32-4848s040-st7701 | Hardware-verified Arduino_GFX + ST7701 init for this exact panel |

Don't edit these folders. Copy what you need into `common/` or a project.
Update them with `git submodule update --remote`.

## Other useful links
- ESPHome device page: https://devices.esphome.io/devices/guition-esp32-s3-4848s040
- Arduino_GFX: https://github.com/moononournation/Arduino_GFX
- esp32-smartdisplay (LVGL wrapper for Sunton boards): https://github.com/rzeldent/esp32-smartdisplay
- ESPHome ST7701S component: https://esphome.io/components/display/st7701s/
- Espressif `esp_lcd_st7701`: https://components.espressif.com/components/espressif/esp_lcd_st7701
- CH340 driver: https://www.wch-ic.com/downloads/CH341SER_EXE.html
