# mini-screen: agent guide

Personal hobby workspace for the **Guition ESP32-4848S040C_I**: ESP32-S3 N16R8, 4" 480×480 ST7701S RGB panel, GT911 touch, CH340C USB-UART, microSD, NS4168 amp, Li-ion charger, optional relays. The repo is public (MIT, see LICENSE): code copied from reference/ or elsewhere keeps its own licence and must be credited in docs/guide/99-sources-and-credits.md. Never commit secrets, personal data or flash backups.

## Development guide: read it first, keep it current
`docs/guide/` is the first-party reference for developing and debugging on this board. It records measured, hard-won findings; base new work on it rather than re-deriving or guessing.

| Before you... | Read |
|---|---|
| start a new project or port to another board | [05 new-project checklist](docs/guide/05-new-project-checklist.md) |
| debug anything (reboot, stutter, crackle, RAM, network, SD, tooling) | the symptom table in [02 debugging playbook](docs/guide/02-debugging-playbook.md), then [01 quirks](docs/guide/01-quirks-and-gotchas.md) for a known cause |
| change architecture, memory, display, network or storage code | [04 patterns and decisions](docs/guide/04-patterns-and-decisions.md) (ADRs with the reasoning) |
| revisit something already tried | [03 investigations](docs/guide/03-investigations.md) (what was tried, before/after numbers, rejected ideas) |
| buy hardware or credit a source | [06 hardware and sourcing](docs/guide/06-hardware-and-sourcing.md), [99 sources and credits](docs/guide/99-sources-and-credits.md) |

Updating the guide is part of finishing a task, not optional:
- A new quirk or gotcha gets a Q-ID entry in 01 (symptom, root cause, fix, evidence, applies-to).
- A debugging session gets an `inv-` section in 03 with hypotheses, every attempt in order, a before/after table of real measurements (from logs, not memory) and the fixing commit. Mark anything inferred as "(inferred)".
- A new or improved debugging method gets a D-ID in 02. A design decision or reversal gets an ADR in 04.
- Keep cross-links valid (anchors `inv-<slug>`, Q-/D-IDs) and the index tables at the top of each chapter in step.
- Hardware results also go in `docs/hardware-log.md`; new projects in `projects/README.md` and the README projects table.

Testing on hardware (project 05 pattern, reuse it in new projects): serial commands (`s n p l b bf bd br bs y m x v c r h`, see the top of `projects/05-powerwall-display/src/main.cpp`), screenshots with `projects/05-powerwall-display/tools-dev/screendump.py COM9 out --all --privacy` (look at every screen after UI changes), timestamped soak logs, `addr2line` for backtraces, host tests in `projects/05-powerwall-display/hosttest/run.ps1`. Details and pitfalls (CH340 reset on port open, concurrent pio runs): guide chapter 02.

Public repo (github.com/Figment-Sieve-Industries/esp32-4848s040-lab): commits only as `Paul <paul@wibjibit.com>`. Never commit secrets, flash backups, serial/soak logs (they contain the home SSID), local paths, private claude.ai links, the home town or coordinates; screenshots only in privacy mode (`v 1`). CI builds every project and runs the host tests on each push.

## Layout
- `README.md`: master index (projects table with the Powerwall 3 display featured, screenshots, where to buy)
- `docs/guide/`: the development guide (above). `docs/`: hardware and research docs 01–12, `hardware-log.md`, `datasheets/`, `vendor/` (schematic, IO sheet), `images/` (incl. `images/screens/`)
- `common/lib/Board4848S040/`: shared board library (pins, display, touch, LVGL glue)
- `common/platformio-base.ini`: shared PlatformIO env `[base_4848s040]`
- `common/boards/`: rzeldent board JSONs (CIY1/CIY3)
- `projects/NN-name/`: one PlatformIO project each. The index is `projects/README.md`.
- `reference/`: prior-art git submodules (read-only)
- `tools/`: PowerShell helpers. `firmware/`: vendor image notes; local flash backups (git-ignored).
- `.github/workflows/ci.yml`: CI (build all projects, host tests).

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
- `void setBacklight(uint8_t level)`: LEDC, 100 kHz by default (`B4848_LCD_BL_FREQ_HZ`), highest duty resolution for that frequency (8 bits at 100 kHz). The driver cuts out below ~100/255 (`B4848_LCD_BL_MIN`). Also `setBacklightLevel(float)`, `setBacklightDuty(raw)`, `bool setBacklightFrequency(hz)` and getters `backlightFrequency/Resolution/Duty/DutyMax/MinDuty()`; the frequency default changes only after the hardware sweep (docs/02-pinout.md).
- `Touch`:
  - `begin()` probes GT911 at 0x5D/0x14 and checks the product ID "911"
  - `read(points, max)` returns `-1` for no new data, `0` for released, or `n` points
  - `setRotation(r)` matches the display rotation
- `lv_display_t *lvglBegin(gfx, touch)` (`Board4848S040_lvgl.h`): LVGL 9 display + pointer indev, partial buffer in internal RAM. With rotation 3 its flush writes the framebuffer itself (row segments + exact cache write-back); Arduino_GFX's rotated draw was ~2x slower (docs/hardware-log.md)

## SD card (`common/lib/SdStore` + patched `common/lib/SD`)
- Add both to `lib_deps` (`symlink://../../common/lib/SD`, `symlink://../../common/lib/SdStore`). The patched `SD` replaces the core's: upstream 3.2.1 fails multi-block writes intermittently (see `common/lib/SD/README-mini-screen.md`).
- `sdcard::begin()` after `initDisplay()`, then `sdcard::poll()` every 1-2 s from one task (hot-swap, no card-detect pin). The clock self-tunes per card (link test, remembered in `/.sdstore.cfg` on the card).
- File helpers open/write/close per call and retry/step down on errors; `writeAtomic`/`readAtomic` for state that must survive power cuts. Never formats. `projects/06-sd-card-test` is the test bench (serial-driven).

## Toolchain
- PlatformIO Core 6.1.x with pioarduino `54.03.21-2` (Arduino core 3.2.1, IDF 5.4). Newer pioarduino releases need PIO Core ≥ 6.2.
- Arduino_GFX is pinned in `library.json`. LVGL is pinned to 9.5.0 per project.

## Project 05 (Powerwall display) - current state
- Main screen = design **N** with design **O**'s battery-bar Powerwall pill (colour by charge, % badge inside the battery) from the maintainer's private design canvas (a claude.ai artifact; link not published). Boards A-O there are the option history (P = moon, Q = aurora). Change the canvas first for layout work, then build the chosen board.
- 11 screens (`ScreenIndex` in `ui_common.h`, `s <0..10>`): house, today, history, health (battery & grid), weather (with sunrise/sunset), moon, aurora, sun, El Nino, settings, about. All credits live on the About screen only (personal device).
- Architecture: `powerwall.cpp` runs the single network task on core 0: each pass polls the Powerwall LAN API (Bearer token) then runs at most one due job (Open-Meteo 15 min, sky/space 30 min, sun image 1 h, ENSO 12 h), so only one TLS session ever exists. LVGL runs on core 1 only. Screens are `screen_*.cpp` behind `screens.h`, sharing `ui_common.*` (palette, top bar, page dots, animations). `energy.cpp` keeps today's totals in RTC memory, backed up to SD via `history.cpp` (SD task on core 0: minute CSV log, baseline, settings). `day_history.cpp` (history screen) builds today's hourly bins from the lifetime counters in RTC memory and saves one 448-byte record per day into `/powerwall/days/YYYY-MM.bin` from the SD task, with a 3-month PSRAM cache the UI reads (it never touches the card; ADR-35 in guide 04). `sky.cpp` fetches AuroraWatch UK + NOAA Kp every 30 min and MET Norway moon times once a day; `moon_calc.cpp` does the phase maths. `space.cpp` (NOAA SWPC + SOHO sun JPEG via LVGL's TJpgDec) and `enso.cpp` (NOAA CPC, HTTP Range tails) feed the sun and El Nino screens.
- **The user does not want internal-flash writes at runtime** (wear). Use RTC memory now; an SD card later.
- Memory: LVGL heap = 1 MB PSRAM pool (`LV_MEM_POOL_ALLOC`). No `LV_ATTRIBUTE_FAST_MEM`. mbedTLS allocations of 1 KB+ go to PSRAM (`mbedtls_platform_set_calloc_free` in `powerwall.cpp`); every `JsonDocument` uses `psramJson()` (`psram_json.h`). `WiFi.persistent(false)`: no NVS writes. Keep ~100 KB+ internal free; the serial heartbeat and `[tls]` lines show the low-water mark.
- Fonts: built-in Montserrat only has ASCII + the degree sign; there is no middle dot, so use "/".
- Shared modules: `net_http` (every background fetch: `net::Session`, own de-chunker, deadlines, per-job budget), `shared_state.h` (`Mutex`/`Shared<T>`/`isStale`), `mem_util.h` (PSRAM buffers, `psramJson()`), `time_util` (dates, `tu::clockValid`), `ui_common` helpers + `ui_chart` (`BarChart`: one object per bar strip). Host tests: `hosttest/run.ps1`.
- Testing on hardware: serial commands `s <n>` / `n` / `p` / `l` / `b` / `bf` / `bd` / `br` / `bs` / `y [d|w|m|-|+]` / `m` / `x` (see main.cpp). `tools-dev/screendump.py COM9 out --all` screenshots every screen to PNG (opens the port without resetting the board). Use it to check layouts after UI changes.
- Editing tip: shell heredocs and sed in the Bash tool turn `\n` inside C strings into real newlines (doubling it to `\\n` doesn't help: the backslashes collapse too). Edit C/C++ and any file containing `\n` with the Edit/Write tools, or write a script file with Write and run it. See guide Q-TOOL-02.
- Sub-agents: the user wants parallel sub-agents where possible, with model/effort matched to the task (e.g. Sonnet medium for well-specified screen files). Write the shared headers/contracts first; agents build with a separate `PLATFORMIO_BUILD_DIR` to avoid collisions.
