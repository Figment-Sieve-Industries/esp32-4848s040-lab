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
- **Build-check before committing:** `tools/build-all.ps1`.
- **Lint every change:** `tools/lint.ps1 <project>` (PlatformIO's cppcheck + clang-tidy; config in `common/platformio-base.ini` and `.clang-tidy`). Run it after editing C/C++ and before committing; fix medium/high findings or justify them with `// NOLINT(<check>)` / `// cppcheck-suppress <id>`. Sub-agents that write C/C++ lint their own files too. If scripts are blocked by execution policy: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
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

## SD card (`common/lib/SdStore` + patched `common/lib/SD`)
- Add both to `lib_deps` (`symlink://../../common/lib/SD`, `symlink://../../common/lib/SdStore`). The patched `SD` replaces the core's: upstream 3.2.1 fails multi-block writes intermittently (see `common/lib/SD/README-mini-screen.md`).
- `sdcard::begin()` after `initDisplay()`, then `sdcard::poll()` every 1-2 s from one task (hot-swap, no card-detect pin). The clock self-tunes per card (link test, remembered in `/.sdstore.cfg` on the card).
- File helpers open/write/close per call and retry/step down on errors; `writeAtomic`/`readAtomic` for state that must survive power cuts. Never formats. `projects/06-sd-card-test` is the test bench (serial-driven).

## Toolchain
- PlatformIO Core 6.1.x with pioarduino `54.03.21-2` (Arduino core 3.2.1, IDF 5.4). Newer pioarduino releases need PIO Core ≥ 6.2.
- Arduino_GFX is pinned in `library.json`. LVGL is pinned to 9.5.0 per project.

## Project 05 (Powerwall display) - current state
- Main screen = design **N** with design **O**'s battery-bar Powerwall pill (colour by charge, % badge inside the battery) from the design canvas https://<private design canvas>. Boards A-O there are the option history (P = moon, Q = aurora). Change the canvas first for layout work, then build the chosen board.
- 8 screens (`ScreenIndex` in `ui_common.h`): house, today, health, weather, moon, aurora, settings, about. All credits live on the About screen only (personal device).
- Architecture: `powerwall.cpp` is the network task on core 0 (Powerwall LAN API with a Bearer token + Open-Meteo every 15 min, one TLS session at a time). LVGL runs on core 1 only. Screens are `screen_*.cpp` behind `screens.h`, sharing `ui_common.*` (palette, top bar, page dots, animations). `energy.cpp` keeps today's totals in RTC memory, backed up to SD via `history.cpp` (SD task on core 0: minute CSV log, baseline, settings). `sky.cpp` fetches AuroraWatch UK + NOAA Kp every 30 min and MET Norway moon times once a day; `moon_calc.cpp` does the phase maths.
- **The user does not want internal-flash writes at runtime** (wear). Use RTC memory now; an SD card later.
- Memory: LVGL heap = 1 MB PSRAM pool (`LV_MEM_POOL_ALLOC`). No `LV_ATTRIBUTE_FAST_MEM` (mbedTLS needs ~50 KB internal RAM). Keep ~100 KB+ internal free.
- Fonts: built-in Montserrat only has ASCII + the degree sign; there is no middle dot, so use "/".
- Editing tip: Python heredocs in Git Bash turned `
` inside C strings into real newlines more than once. For C/C++ string edits use the Edit tool.
- Sub-agents: the user wants parallel sub-agents where possible, with model/effort matched to the task (e.g. Sonnet medium for well-specified screen files). Write the shared headers/contracts first; agents build with a separate `PLATFORMIO_BUILD_DIR` to avoid collisions.
