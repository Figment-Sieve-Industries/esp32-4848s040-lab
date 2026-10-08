# Display: ST7701S (480×480 RGB)

The ST7701S has **two interfaces**, and both are used:
1. **3-wire SPI** (CS 39, SCK 48, SDA 47, 9-bit with a D/C bit). It is used only once at boot to send the init sequence: power and gamma registers, then sleep-out and display-on.
2. **16-bit RGB565 parallel** (DE/HSYNC/VSYNC/PCLK + 16 data lines). This streams the framebuffer continuously from PSRAM using the ESP32-S3 LCD peripheral (`esp_lcd_panel_rgb`).

No reset pin is wired, so the init sequence must start with a software state that the panel accepts after power-on.

## Timings in use (verified by the aquaElectronics project)
| Param | Value |
|---|---|
| PCLK | 12 MHz (12.5 MHz in rzeldent; ESPHome uses 12 MHz) |
| HSYNC pulse / front / back porch | 8 / 10 / 50 |
| VSYNC pulse / front / back porch | 8 / 10 / 20 |
| Polarity | hsync 1, vsync 1, PCLK active on the rising edge |
| Colour | RGB565 bus. The panel is set to `0x3A = 0x60` (RGB666) and the top bits are used. |

ESPHome uses a back porch of 20/10, which also works. If you see **drift, shimmer or a rolling image**, adjust the porches or PCLK first. Higher PCLK gives a better frame rate but more PSRAM bandwidth pressure. 12–16 MHz is the safe zone.

## Init sequence
The full table is in [`Board4848S040.cpp`](../common/lib/Board4848S040/src/Board4848S040.cpp). It is taken from [aquaElectronics/esp32-4848s040-st7701](https://github.com/aquaElectronics/esp32-4848s040-st7701), whose author notes that "generic ST7701 examples often fail on this panel". Arduino_GFX also ships `st7701_type9_init_operations` for the `ESP32_4848S040_86BOX_GUITION` dev device, which is a good second option if colours or gamma look off.

Structure:
- `FF 77 01 00 00 10`: command bank 2, page 0. Display line setting (C0/C1/C2), CD, positive/negative gamma (B0/B1)
- `FF 77 01 00 00 11`: page 1. Voltages (Vop, VCOM, VGH, VGL, AVDD) and GIP timing (E0–ED)
- `FF 77 01 00 00 13`: VAP/VAN tweak
- `FF 77 01 00 00 00`: back to the normal command set. `3A` pixel format, `11` sleep out, 120 ms wait, `29` display on

## Rotation
Hardware rotation isn't available on an RGB panel. Rotation happens in software:
- Arduino_GFX: the `rotation` argument to `initDisplay()` (0–3)
- LVGL: `lv_display_set_rotation()` (needs a rotation buffer or `sw_rotate`)
- ESPHome: `rotation: 270` or a touchscreen `transform:` (useful when the USB socket points down)

Remember to transform touch coordinates to match the rotation.

## Performance tips
- The framebuffer is 480×480×2 = 460 KB, in PSRAM.
- With LVGL, use two partial draw buffers in internal RAM, or a full-frame buffer in PSRAM with `direct_mode`. Bounce buffers (`bounce_buffer_size_px`) in `esp_lcd` reduce tearing and PSRAM contention under Wi-Fi load.
- If the image glitches when Wi-Fi is active, that's PSRAM bandwidth. Lower PCLK or enable bounce buffers.
