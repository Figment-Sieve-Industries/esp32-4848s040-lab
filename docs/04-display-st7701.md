# Display: ST7701S (480×480 RGB)

The ST7701S has **two interfaces**, and both are used:
1. **3-wire SPI** (CS 39, SCK 48, SDA 47, 9-bit with a D/C bit). It's used only once at boot to send the init sequence: power and gamma registers, then sleep-out and display-on.
2. **16-bit RGB565 parallel** (DE/HSYNC/VSYNC/PCLK + 16 data lines). This streams the framebuffer continuously from PSRAM using the ESP32-S3 LCD peripheral (`esp_lcd_panel_rgb`).

The LCD reset pin isn't connected to the ESP32. It's on an RC power-on reset (R16/C22) shared with the GT911, so the init sequence runs on a panel that was reset at power-up. Arduino_GFX also sends a software reset over SPI.

## Timings in use
The porches and polarities come from aquaElectronics and Arduino_GFX's `ESP32_4848S040_86BOX_GUITION` config, which agree. The PCLK arguments come from the Arduino_GFX config. **This exact combination hasn't been run on hardware here yet.**

| Param | Value |
|---|---|
| PCLK | 12 MHz |
| HSYNC front / pulse / back porch | 10 / 8 / 50 |
| VSYNC front / pulse / back porch | 10 / 8 / 20 |
| Polarity | hsync 1, vsync 1, `pclk_active_neg` 0 |
| Colour | RGB565 on a 16-bit bus. The panel is set to `0x3A = 0x60` (RGB666); it's an 18-bit panel with DB0/DB12 unconnected, so the 16 bits map onto its top bits. |

### About the aquaElectronics `12000` argument
aquaElectronics passes `12000` in the `pclk_active_neg` slot (the argument after the 8 sync values), apparently meaning it as the clock. That field is a **1-bit bitfield** in `esp_lcd_rgb_timing_t`, and 12000 is even, so it **truncates to 0**. The speed falls back to Arduino_GFX's default of 12 MHz. Their effective configuration is therefore **identical** to ours (`0, 12000000`).

### Timing variants seen in the wild
If you get drift, shimmer or a rolling image, these are the known alternatives:

| Source | PCLK | H porches (front/pulse/back) | V porches | Other |
|---|---|---|---|---|
| **Our lib** / Arduino_GFX 86BOX / aquaElectronics | 12 MHz | 10/8/50 | 10/8/20 | `pclk_active_neg` 0 |
| openHASP (`reference/snippets/openhasp-esp32-s3-4848S040.ini`) | 12 MHz | 10/8/50 | 10/8/20 | `pclk_active_neg` 1 |
| joevial (LovyanGFX) | 8 MHz | 10/8/50 | 10/8/20 | sync polarity 0, `de_idle_high` 1 |
| rzeldent board JSON | 12.5 MHz | 10/8/50 | 10/8/20 | `pclk_active_neg` 0 |
| ESPHome reference YAML (alaltitov) | 12 MHz | (component defaults) | (component defaults) | `pclk_inverted: false` |
| ESP-IDF First-Start (`board_config.h`, uses `esp_lcd_st7701`) | 15.25 MHz (from a 32 µs line time) | **0**/8/**0** | **0**/8/**0** | Author's note: "any non-zero [front/back] porch causes drift after ~100 cycles; HBP=HFP=VBP=VFP=0 is the only stable configuration". The likely reason is that the ST7701 sets its own VBP/VFP via register C1. Tested with a different init sequence from ours. |

Higher PCLK gives a better frame rate but puts more pressure on PSRAM bandwidth. 8–16 MHz is the safe zone.

## Init sequence
The full table is in [`Board4848S040.cpp`](../common/lib/Board4848S040/src/Board4848S040.cpp). It's taken from [aquaElectronics/esp32-4848s040-st7701](https://github.com/aquaElectronics/esp32-4848s040-st7701). It is **byte-identical** to Arduino_GFX's `st7701_type9_init_operations` (used for `ESP32_4848S040_86BOX_GUITION`), except that ours adds a 10 ms delay before Sleep Out. Two independent sources agree on every register value, which gives good confidence in the table.

Structure:
- `FF 77 01 00 00 10`: command bank 2, page 0. Display line setting (C0/C1/C2); `CD 00` disables MDT (also the key line in ESPHome's init); positive/negative gamma (B0/B1).
- `FF 77 01 00 00 11`: page 1. Voltages (Vop, VCOM, VGH, VGL, AVDD) and GIP timing (E0–ED).
- `FF 77 01 00 00 13`: VAP/VAN tweak.
- `FF 77 01 00 00 00`: back to the normal command set. `3A` pixel format, `11` sleep out, 120 ms wait, `29` display on.

## Rotation
Hardware rotation isn't available on an RGB panel. Rotation happens in software:
- Arduino_GFX: the `rotation` argument to `board4848::initDisplay()` (0–3). Call `touch.setRotation()` with the same value so touch coordinates match.
- LVGL 9: `lv_display_set_rotation()`. In partial render mode the flush callback must rotate each area itself (e.g. with `lv_draw_sw_rotate`), or let Arduino_GFX's own rotation handle it.
- ESPHome: `rotation: 270` or a touchscreen `transform:` (useful when the USB socket points down).

## Performance tips
- The framebuffer is 480×480×2 = 460 KB, in PSRAM.
- With LVGL, `board4848::lvglBegin()` uses a 48-line partial buffer in internal RAM. Direct mode (rendering into the PSRAM framebuffer) needs `auto_flush=false` or `gfx->flush(true)` for cache writeback.
- **Bounce buffers are on by default** (`B4848_LCD_BOUNCE_PX` = 480×10 px, about 19 KB of internal RAM for two buffers).
  - **Verified on hardware (2026-10-10):** without them, the whole screen flickered while drawing with a finger. The CPU writing the PSRAM framebuffer starved the LCD DMA. With them, the image is stable.
  - Pass `initDisplay(rotation, 0)` to disable them, or a larger value to go further. The value must divide 480×480.
- If the image still glitches (e.g. under heavy Wi-Fi load), that's more PSRAM bandwidth contention:
  1. Raise the bounce buffer (e.g. 480×20).
  2. Lower PCLK.
  3. Try the zero front/back porch variant.
  4. See the sdkconfig options in [06](06-frameworks-and-libraries.md).
