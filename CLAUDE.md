# mini-screen: agent guide

Personal hobby workspace for the **Guition ESP32-4848S040C_I**: ESP32-S3 N16R8, 4" 480×480 ST7701S RGB panel, GT911 touch, CH340C USB-UART, microSD, NS4168 amp, Li-ion charger, optional relays. Licences of reference code are not a concern.

## Layout
- `README.md`: master index
- `docs/`: hardware docs 01–08, `datasheets/`, `vendor/` (schematic, IO sheet), `images/`
- `common/lib/Board4848S040/`: shared board library (pins, display, touch, LVGL glue)
- `common/platformio-base.ini`: shared PlatformIO env `[base_4848s040]`
- `common/boards/`: rzeldent board JSONs (CIY1/CIY3)
- `projects/NN-name/`: one PlatformIO project each. The index is `projects/README.md`.
- `reference/`: prior-art git submodules (read-only)
- `tools/`: PowerShell helpers. `firmware/`: flash backups and vendor images.

## Hard rules
- **Run pio and esptool from PowerShell, never Git Bash.** MSYS breaks ESP's `idf_tools.py` install. Use `tools/pio.ps1 <project> [build|upload|monitor|all] [-Port COMx]`.
- **Build-check before committing:** `tools/build-all.ps1`. If scripts are blocked by execution policy: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
- **Never edit `reference/`** (submodules). Copy what you need into `common/` or a project.
- **Pins live only in `common/lib/Board4848S040/src/Board4848S040_pins.h`.** The vendor schematic `docs/vendor/schematic-*.png` is the authority.
- **Projects extend the shared config:** `extra_configs = ../../common/platformio-base.ini`, `extends = base_4848s040`. Depend on the lib via `symlink://../../common/lib/Board4848S040`.
- **Keep `memory_type = qio_opi`** (octal PSRAM) **and `ARDUINO_USB_CDC_ON_BOOT=0`**. Native USB pins 19/20 are reused for touch SDA and LCD G1; the console is on the CH340C (UART0).
- **IO1, IO2 and IO40 are shared** by relays, I2S audio (0 Ω links R25–27 vs R21–23) and a possible 2nd I2C bus.
- **GPIO0, 3, 45 and 46 are strapping pins** used by the LCD and touch. Don't add loads to the touch bus (GPIO45).
- **Back up the factory flash before the first upload:** `tools/backup-flash.ps1`.
- **Credentials live in `projects/*/src/secrets.h` only** (git-ignored, `**/src/secrets.h`). Commit a `secrets.example.h` with placeholders. Never put real SSIDs, passwords or tokens in docs, commits or logs.
- **New projects:** numbered `NN-name`. Copy `02-lvgl-hello` for UI work or `01-hello-display-touch` for plain graphics, without `.pio/`. Add a row to `projects/README.md`.
- **Status vocabulary:** `idea` → `wip` → `builds` → `works on hw` → `done`. "builds" is not "works on hw"; only claim hardware results the user has reported.
- **Arduino vs ESP-IDF** is documented as reasoning (docs/06), not a decision. Don't present it as decided.
- **Performance defaults are measured** (docs/hardware-log.md): `-O2` with `build_unflags = -Os`, `LV_ATTRIBUTE_FAST_MEM` in lv_conf, a 120-line LVGL buffer. Benchmark changes with `03-lvgl-demos`'s `bench` env before changing them. Double framebuffers and 2 draw threads were measured slower.

## Library API (`#include <Board4848S040.h>`, namespace `board4848`)
- `Arduino_RGB_Display *initDisplay(uint8_t rotation = 0, size_t bouncePx = B4848_LCD_BOUNCE_PX)`: ST7701 init over 3-wire SPI, 12 MHz RGB, framebuffer in PSRAM, bounce buffers on (they stop the flicker; verified on hardware)
- `void setBacklight(uint8_t level)`: LEDC at 100 kHz, 8-bit. The driver cuts out below ~100 (`B4848_LCD_BL_MIN`).
- `Touch`:
  - `begin()` probes GT911 at 0x5D/0x14 and checks the product ID "911"
  - `read(points, max)` returns `-1` for no new data, `0` for released, or `n` points
  - `setRotation(r)` matches the display rotation
- `lv_display_t *lvglBegin(gfx, touch)` (`Board4848S040_lvgl.h`): LVGL 9 display + pointer indev, partial buffer in internal RAM

## Toolchain
- PlatformIO Core 6.1.x with pioarduino `54.03.21-2` (Arduino core 3.2.1, IDF 5.4). Newer pioarduino releases need PIO Core ≥ 6.2.
- Arduino_GFX is pinned in `library.json`. LVGL is pinned to 9.5.0 per project.
