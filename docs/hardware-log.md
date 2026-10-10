# Hardware log

What has been verified on real units. Add an entry each time something is tested.

## Unit 1: MAC <mac>

| Item | Result |
|---|---|
| Chip | ESP32-S3 (QFN56) rev v0.2, 40 MHz crystal |
| Flash | **16 MB** detected (mfr 0x68, dev 0x4018). The factory bootloader header says 8 MB; that's just a conservative vendor setting. |
| PSRAM | 8 MB embedded (AP_3v3), octal. 8,384,052 bytes free at boot with `qio_opi` |
| USB | CH340C on COM9 (1A86:7523). Auto-reset upload works with no buttons |
| Factory backup | `firmware/factory/<mac>-20261010-0934.bin`, SHA256 `DA9F18AE…1A33`. Factory partitions: nvs, otadata, one 7.9 MB app0, coredump |
| Display | ST7701 init + RGB timings OK. Red/green/blue/white colour test correct |
| Flicker | Whole-screen flicker while drawing **without** bounce buffers. **Stable with** `B4848_LCD_BOUNCE_PX` = 480×10 |
| Touch | GT911 at **0x5D**, product ID "911". Finger drawing tracks correctly |
| Backlight | 100 kHz LEDC: smooth dimming 255 → 100, **still lit at 100** (`B4848_LCD_BL_MIN`), **no audible whine**. The true cut-out point below 100 isn't measured yet |
| LVGL (02-lvgl-hello) | LVGL 9.5 via `lvglBegin()`: renders correctly, button taps count once each, slider drags smoothly, no flicker |
| Restart quirk | Restarting via `ESP.restart()` from the 03 launcher once came back as `RTCWDT_RTC_RST` (RTC watchdog) instead of a software reset. It recovered by itself. Suspected: restarting while the RGB panel DMA is running. Not fixed yet |
| Not yet tested | Corner accuracy, rotation, backlight cut-out below 100, SD card, audio, relays, Wi-Fi, battery |

### LVGL benchmark, unit 1 (2026-10-10)
Setup: `03-lvgl-demos`, LVGL 9.5.0, Arduino core 3.2.1 (`-Os`), partial render with a 48-line internal-RAM buffer, flush via `draw16bitRGBBitmap` into a single PSRAM framebuffer, 12 MHz PCLK, bounce buffers 480×10.

| Scene | CPU | FPS | Avg ms | Render ms | Flush ms |
|---|---|---|---|---|---|
| Empty screen | 85% | 21 | 38 | 18 | 20 |
| Moving wallpaper | 96% | 16 | 55 | 34 | 21 |
| Single rectangle | 61% | 60 | 3 | 1 | 2 |
| Multiple rectangles | 90% | 35 | 21 | 10 | 11 |
| Multiple RGB images | 80% | 55 | 9 | 7 | 2 |
| Multiple ARGB images | 87% | 44 | 16 | 13 | 3 |
| Rotated ARGB images | 86% | 44 | 17 | 15 | 2 |
| Multiple labels | 93% | 26 | 31 | 25 | 6 |
| Screen sized text | 98% | 7 | 129 | 108 | 21 |
| Multiple arcs | 67% | 58 | 5 | 5 | 0 |
| Containers | 77% | 46 | 19 | 16 | 3 |
| Containers with overlay | 95% | 13 | 63 | 49 | 14 |
| Containers with opa | 85% | 39 | 23 | 20 | 3 |
| Containers with opa_layer | 97% | 10 | 89 | 82 | 7 |
| Containers with scrolling | 97% | 10 | 85 | 64 | 21 |
| Widgets demo | 98% | 6 | 114 | 96 | 18 |
| **All scenes avg.** | **87%** | **30** | 44 | 35 | 9 |

Reading: a full-screen flush (CPU copy of 460 KB into PSRAM) costs about 20 ms. Text and opa layers are render-bound. Small-area updates run at 44–60 FPS. Optimisation candidates, in order of effort: `-O2` + `LV_ATTRIBUTE_FAST_MEM`; a bigger draw buffer; double framebuffers with vsync swap (needs our own `esp_lcd` setup, no copy, no tearing); esp_lvgl_port SIMD blend routines.
