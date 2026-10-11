# New project checklist

[Guide index](README.md) | [01 Quirks](01-quirks-and-gotchas.md) | [02 Debugging](02-debugging-playbook.md) | [03 Investigations](03-investigations.md) | [04 Patterns](04-patterns-and-decisions.md) | [05 New project](05-new-project-checklist.md) | [06 Hardware](06-hardware-and-sourcing.md) | [Credits](99-sources-and-credits.md)

A practical, ordered list for starting a project on the Guition ESP32-4848S040 (or a similar ESP32-S3 board with an RGB panel). It assumes Windows, PlatformIO with pioarduino, and this repo's layout. The reasons behind each step are in [04-patterns-and-decisions.md](04-patterns-and-decisions.md) (cited as ADR-nn).

Contents: [1 Toolchain](#1-toolchain) | [2 Back up the flash](#2-back-up-the-factory-flash) | [3 Copy a template](#3-create-the-project) | [4 platformio.ini](#4-platformioini) | [5 lv_conf.h](#5-lv_confh) | [6 Secrets](#6-secrets) | [7 Pins](#7-pins) | [8 Init order](#8-init-order) | [9 Defaults to keep](#9-memory-and-performance-defaults-to-keep) | [10 Build and lint](#10-build-and-lint) | [11 Test and soak](#11-test-soak-and-screenshots) | [12 Commit](#12-commit-rules) | [13 Porting](#13-porting-to-a-different-board) | [14 Pre-flight table](#14-pre-flight-checklist)

---

## 1. Toolchain

| Step | Detail |
|---|---|
| Install VS Code and the PlatformIO IDE extension | This installs PlatformIO Core under `%USERPROFILE%\.platformio\`. The repo's PowerShell scripts expect `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` |
| Core version | Core 6.1.x is enough for the pinned platform. Newer pioarduino releases need Core 6.2 or later (`pio upgrade`) |
| Platform | Pinned in `common/platformio-base.ini`: pioarduino `54.03.21-2` (Arduino core 3.2.1, ESP-IDF 5.4). Do not use the official `espressif32` platform: it is on core 2.x and Arduino_GFX 1.6 needs 3.x |
| Shell | **PowerShell, never Git Bash.** ESP's `idf_tools.py` installer rejects MSYS shells and the first build fails with `xtensa-esp32s3-elf-g++ is not recognized` |
| Execution policy | If scripts are blocked: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` |
| USB driver | The board shows as `USB-SERIAL CH340 (COMx)`, VID:PID `1A86:7523`. If Windows does not install it, use WCH's CH341SER driver. Use a **data** USB-C cable |
| Git | Clone the repo. `reference/` holds prior-art submodules (read-only); `git submodule update --init` is only needed if you want to read them |
| First build | Downloads the toolchain and libraries; several minutes. Later builds take seconds |
| Optional: host tests | Visual Studio with the C++ workload and a Windows SDK (for `hosttest/run.ps1`) |
| Optional: Python | PlatformIO's bundled Python has `pyserial`, which the screenshot and SD scripts need |

Check: `.\tools\pio.ps1 -Project 01-hello-display-touch -Action build` completes.

## 2. Back up the factory flash

Do this before the first upload on a new board. It is the only way back to the shipped firmware.

```powershell
.\tools\backup-flash.ps1            # reads all 16 MB, a few minutes; or -Port COMx
.\tools\restore-flash.ps1 -File firmware\factory\<file>.bin
```

- The image is named after the board's MAC address. **In a public repository, do not commit it** (the name identifies the unit, and the image may hold data). Keep backups outside git or add the path to `.gitignore`.
- If auto-reset fails: hold BOOT, tap RST, release BOOT, then start the upload.
- Vendor factory images are described in `firmware/README.md`.

## 3. Create the project

1. Pick the next free number: `projects/NN-short-name/`.
2. Copy a template **without `.pio/`** (it is large build output):

   | Template | Use for |
   |---|---|
   | `02-lvgl-hello` | LVGL UI projects (default choice) |
   | `01-hello-display-touch` | Plain Arduino_GFX graphics |
   | `05-powerwall-display` | A networked, multi-screen app: shows the task layout, `net::Session`, screen registry. Copy files from it rather than the whole project |
   | `06-sd-card-test` | SD work; also the serial-driven test bench pattern |

3. Edit the header comments in `platformio.ini` and `src/main.cpp`.
4. Write a short `README.md` in the project: goal, how to run, status.
5. Add a row to `projects/README.md` (the only index). Start at status `wip`.
6. Status words: `idea`, `wip`, `builds`, `works on hw`, `done`. "Builds" is not "works on hw"; claim hardware results only when you have seen them or the owner reported them.

## 4. platformio.ini

Extend the shared environment and add only what the project needs:

```ini
[platformio]
extra_configs = ../../common/platformio-base.ini
default_envs = esp32-4848s040

[env:esp32-4848s040]
extends = base_4848s040
build_flags =
    ${base_4848s040.build_flags}
    -DLV_CONF_INCLUDE_SIMPLE
    -Iinclude                       ; lv_conf.h lives in include/
    -DB4848_LVGL_BUF_LINES=48       ; network projects; omit for the 120-line default
lib_deps =
    ${base_4848s040.lib_deps}       ; the board library, via symlink://
    lvgl/lvgl @ 9.5.0               ; exact pin
    bblanchon/ArduinoJson @ 7.4.3   ; only if needed
    symlink://../../common/lib/SD       ; only with an SD card: the PATCHED SD, replaces the core's
    symlink://../../common/lib/SdStore
```

Keep from the base (do not override): `memory_type = qio_opi` (octal PSRAM), `ARDUINO_USB_CDC_ON_BOOT=0` (the console is the CH340C on UART0, because the native USB pins 19/20 are used by touch SDA and an LCD data bit), `-O2` with `build_unflags = -Os`, the pinned platform.

Pin every library version. Arduino_GFX is pinned only in `common/lib/Board4848S040/library.json`. Re-check `lv_conf.h` against the new template before raising LVGL (9.6 deprecated `LV_COLOR_DEPTH` and moved headers).

## 5. lv_conf.h

Start from the template's file and choose by project type:

| Setting | UI only, no Wi-Fi/TLS | Wi-Fi/TLS or many screens |
|---|---|---|
| `LV_COLOR_DEPTH` | 16 | 16 |
| Heap | 48 KB static builtin pool is enough for a handful of widgets (02), or the C heap (03) | `LV_STDLIB_BUILTIN`, `LV_MEM_SIZE (1024*1024U)`, `LV_MEM_POOL_ALLOC` = `heap_caps_malloc(size, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT)` (ADR-08) |
| `LV_ATTRIBUTE_FAST_MEM` | `IRAM_ATTR` (about 55 KB internal RAM, faster) | Leave undefined, re-test when RAM allows (ADR-16) |
| Draw buffer lines (`B4848_LVGL_BUF_LINES`) | 120 (default) | 48 (ADR-15) |
| `LV_USE_OS` | `LV_OS_NONE`, LVGL only from `loop()` | `LV_OS_NONE`; no other task calls LVGL (ADR-04) |
| Fonts | Enable only the sizes you use | Built-in Montserrat 12 to 48; ASCII and degree sign only, no middle dot |
| Logging | `LV_LOG_LEVEL_WARN`, `LV_LOG_PRINTF 1` | Same |

## 6. Secrets

- Real Wi-Fi names and passwords, IPs, API keys, tokens and coordinates live **only** in `projects/NN-name/src/secrets.h`. It is git-ignored by the pattern `**/src/secrets.h`.
- Commit `src/secrets.example.h` with obviously fake values:

```cpp
// Copy to secrets.h (git-ignored) and fill in.
#pragma once
struct WifiCred { const char *ssid; const char *password; };
static const WifiCred WIFI_NETWORKS[] = {
    {"YourNetwork", "your-password"},
};
#define DEVICE_HOST "192.0.2.10"     // documentation address; replace with your device
#define TZ_INFO "UTC0"               // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0"
#define LOCATION_LAT 0.0
#define LOCATION_LON (0.0)           // parenthesised: a bare negative macro breaks in expressions
```

- Confirm the ignore rule works before the first commit: `git check-ignore -v projects/NN-name/src/secrets.h`.
- Never put real values in docs, commit messages, screenshots or logs. Serial logs from the Wi-Fi scan print visible network names: keep captured logs out of git (`.gitignore` already covers `sd_serial_*.log`; add others).
- If a secret was committed, a later commit does not remove it from history. Rotate the secret, then rewrite history before the repo is shared.
- Set `WiFi.persistent(false)` so the Wi-Fi stack does not write credentials to internal flash (ADR-28).

## 7. Pins

- Every pin is defined once, in `common/lib/Board4848S040/src/Board4848S040_pins.h`. Use the macros; never type a GPIO number in application code (ADR-02).
- The vendor schematic (`docs/vendor/schematic-*.png`) is the authority over community configs and the vendor spreadsheet.
- Shared pins: **IO1, IO2 and IO40** serve the relays and I2S audio (0 ohm links R25 to R27 versus R21 to R23), and could carry a second I2C bus. SD SCK/MOSI (48/47) are the LCD's init bus.
- **Strapping pins GPIO0, 3, 45, 46** are used by the LCD and touch. Do not add loads to the touch bus (GPIO45).
- Do not use the `LED_GPIO` define from the rzeldent board JSONs: it is HSYNC.

## 8. Init order

The order in `setup()` is a rule, not a preference:

```cpp
Serial.begin(115200);
gfx = board4848::initDisplay(rotation, bouncePx);   // 1. display
if (!gfx) halt("Display init failed (PSRAM? qio_opi?)");
gfx->fillScreen(RGB565_BLACK);
board4848::setBacklight(level);                      //    backlight at or above B4848_LCD_BL_MIN
sdcard::begin(20000000);  // or start the SD task   // 2. SD card (optional)
bool hasTouch = touch.begin();                       // 3. touch
touch.setRotation(rotation);                         //    same value as the display
lv_display_t *d = board4848::lvglBegin(gfx, hasTouch ? &touch : nullptr);   // 4. LVGL
// build screens, lv_screen_load(...)
// start network/background tasks                    // 5. after LVGL has its memory
lv_timer_create(refreshCb, 500, nullptr);
```

| Step | Why this position |
|---|---|
| Display first | SD SCK/MOSI are shared with the ST7701's 3-wire init bus. After init the panel's CS (GPIO39) stays high and ignores SD traffic. `SD.begin()` with default pins would grab FSPI pins 10 to 13, which are LCD red bits, so SdStore pins its own SPI bus first |
| SD before LVGL | A card mounting in the background should not wait for the UI. `sdcard::begin()` without a card takes a few seconds, so 05 mounts in a background task. A missing card must not block boot (it is polled, ADR-32) |
| Touch before `lvglBegin` | `lvglBegin` takes the `Touch*` and creates the pointer input device. `setRotation` must match the display rotation or the axes are wrong |
| LVGL before network tasks | The draw buffer and the LVGL pool are the big allocations; allocate them while internal RAM is whole, then give what is left to TLS. The mbedTLS hook must be installed before the first TLS session (ADR-11) |
| Log `[mem]` after each step | Shows who used the internal RAM, so a growing budget is caught early |

Then, in `loop()`: `lv_timer_handler()` and nothing slow. Raise the loop task stack (`SET_LOOP_TASK_STACK_SIZE(16 * 1024)`) if LVGL decoders or large screens are used. A GT911 `read()` returns `-1` for "no new data", `0` for "released" and `n` for points; test with `> 0`.

## 9. Memory and performance defaults to keep

| Default | Value | Why | Change only after |
|---|---|---|---|
| PSRAM mode | `qio_opi` | Octal PSRAM; a wrong mode is a boot loop or `PSRAM init failed` | Never, on this board |
| USB CDC on boot | 0 | Console is UART0 via the CH340C | Never, on this board |
| Optimisation | `-O2` plus `build_unflags = -Os` | About 10 percent faster rendering; `-O2` alone has no effect | Benchmarking |
| Bounce buffers | On: 480x10 default, 480x20 with Wi-Fi/TLS/JPEG work | Stops flicker and crackle when the CPU hits PSRAM (ADR-13) | Finger-paint test (01) and a soak |
| LVGL render mode | Partial, buffer in internal RAM | Rendering into PSRAM is much slower | `bench` env |
| Draw buffer | 120 lines without network, 48 with | RAM budget (ADR-15) | `bench` env, RAM measurement |
| Framebuffers and draw threads | One framebuffer, one draw thread | Double framebuffers and two draw threads measured slower (ADR-17) | Benchmark of the specific UI |
| Rotation | `initDisplay(3)` with `lvglBegin` uses the library's contiguous flush | 254 ms to 142 ms full-screen (ADR-14) | `bench` env |
| LVGL heap | 1 MB PSRAM pool for multi-screen or network apps | Widgets must not eat internal RAM (ADR-08) | Pool usage in the heartbeat |
| Internal RAM | About 100 KB or more free at the low-water mark | One TLS session needs 40 to 50 KB (ADR-09) | `[mem]` and `m` output |
| TLS allocator | `mbedtls_platform_set_calloc_free`, 4 KB threshold | ADR-11 | A soak |
| Tasks | Network and SD at `tskIDLE_PRIORITY`, core 0; LVGL on core 1 | Watchdog and thread-safety (ADR-04, ADR-07) | Never raise the priority |
| One TLS session at a time | Close the previous one first | RAM (ADR-06) | RAM numbers |
| Flash writes at runtime | None. RTC memory or SD only | Wear; PSRAM stalls during flash writes (ADR-28) | Owner decision |
| Change-only UI setters | Always | Avoids needless redraw (ADR-18) | Never |
| Layer-forcing styles | Avoid `clip_corner`, `opa_layered`, transforms on things that redraw | PSRAM layers, big slowdown (ADR-21) | The `bench` scenes |

Benchmark rule: any change that can affect render or flush (optimisation level, buffer lines, cache settings, malloc thresholds, rotation path) goes through `projects/03-lvgl-demos`, `env:bench`, and is compared with the v5 row in [hardware-log.md](../hardware-log.md). Record the result there.

## 10. Build and lint

```powershell
.\tools\pio.ps1 -Project NN-name -Action build         # also: upload, monitor, all (-Port COMx)
.\tools\lint.ps1 NN-name                               # cppcheck + clang-tidy, own code only
.\tools\build-all.ps1                                  # every project, pass/fail table
```

- Lint after every C or C++ edit and before committing. Exit code 1 means a medium or high finding remains. Fix it, or justify it with `// NOLINT(<check>)` or `// cppcheck-suppress <id>` and a reason. `-All` shows low-severity style findings; `-Refresh` rebuilds the cached compilation database.
- A new shared library of your own must be added to the `HeaderFilterRegex` in `.clang-tidy` and to `check_src_filters` in the base ini.
- Several builds at once need separate output: set `PLATFORMIO_BUILD_DIR` per build. `lint.ps1` uses a fixed folder under `%TEMP%`, so parallel lint runs need a private `$env:TEMP`.
- Parallel work: write the shared headers (contracts) first, give each worker files it alone owns, and integrate, flash and run serial checks in one place.

## 11. Test, soak and screenshots

**First boot.** Read the `[mem]` lines (internal RAM after each step), `Free internal heap`, and the first heartbeat. Compare them with the previous project's numbers.

**What a healthy heartbeat shows** (05 prints one every 10 s):

| Field | Look for |
|---|---|
| fps and max gap | Steady frame rate; a max gap above about 120 ms is a visible stall and is logged as `[stall]` |
| internal free, largest block, min ever | Min ever about 100 KB or more; the largest block comfortably above one TLS session |
| stack free (net, lvgl) | Headroom of a couple of KB or more; raise a stack that runs low |
| LVGL pool used | Well under 100 percent |
| redraw px/s and flushes/s | The number to compare when changing idle animations |
| TLS connects, requests, failures | Failures stay 0 |
| SD state, clock, I/O errors | Mounted at the expected clock, 0 errors |

**Soak.** Capture serial to a log file and leave the board running:

1. 10 minutes as a smoke test after any change.
2. At least 1 hour after a change to networking, memory or the display path.
3. A day before calling a networking or memory change settled.
4. Search the log for `[slow]`, `[stall]`, `task_wdt`, `Guru Meditation`, `rst:` (unexpected resets), `-32512`, `Memory allocation failed`, and growth in used heap.
5. Watch the screen and listen: PSRAM contention shows as crackle, sparkle or a shifting image, which logs do not capture.

**Serial control** makes tests repeatable: add a command channel (one line in, one reply line ending `#ok` or `#err <reason>`) early. 05 has `s <n>` (go to screen), `n`/`p`, `l`, `b <0-255>`, `m` (memory), `x` (screenshot), `r`, `h`. `06-sd-card-test/sd_serial.py` drives a sequence of commands, waits for each `#done`, and saves a log.

**Screenshots.** The `x` command sends the native framebuffer as base64 lines at 921600 baud; the host script rotates it and writes a PNG:

```powershell
& $env:USERPROFILE\.platformio\penv\Scripts\python.exe projects\05-powerwall-display\tools-dev\screendump.py COMx out.png --screen 3
```

Use it to check every screen after UI changes (layout, clipping, colours, stale states, empty states). It shows the framebuffer, so it cannot show tearing or crackle. Do not commit screenshots that contain real names, addresses or readings from your home.

**Host tests.** Logic with no Arduino types (framing, parsers, cache headers) belongs in a `hosttest/` folder and runs with `.\projects\05-powerwall-display\hosttest\run.ps1` (prints `N passed, 0 failed`).

**Hardware claims.** Mark a project `works on hw` only after you or the owner has seen it run. Record measurements in `docs/hardware-log.md` with the date and setup.

## 12. Commit rules

1. `tools/build-all.ps1` passes.
2. `tools/lint.ps1 NN-name` has no medium or high findings.
3. The project README and `projects/README.md` row match the real status.
4. Docs are updated where behaviour, pins or defaults changed.
5. **Before every commit in a public repo**, read the staged diff for: Wi-Fi names and passwords, tokens and keys, IP addresses, MAC addresses, coordinates and place names, personal names and email addresses, serial logs, flash images. A quick scan: `git diff --cached | Select-String -Pattern 'password|token|ssid|secret|api[_-]?key|\b\d{1,3}(\.\d{1,3}){3}\b|([0-9a-f]{2}:){5}[0-9a-f]{2}'` then read the hits.
6. Do not commit `.pio/`, `secrets.h`, `*.bin` outside the allowed folders, logs, or a copy of the factory flash.
7. One topic per commit; message says what and why. Never edit `reference/`.

## 13. Porting to a different board

What carries over unchanged: `net::Session`, `shared_state.h`, `mem_util.h`, `time_util`, the UI helpers, `BarChart`, `SdStore` (except pins), the job-table pattern, serial commands. What belongs to the board is isolated in the board library (ADR-01). Copy `common/lib/Board4848S040` to `common/lib/Board<NAME>` and change the following.

| Area | What to change | How to check |
|---|---|---|
| Pins | Replace `..._pins.h`: panel (DE, VSYNC, HSYNC, PCLK, data pins), 3-wire or 4-wire SPI for init, backlight, touch SDA/SCL/INT/RST, SD, UART. Take them from the schematic. Mark strapping pins and shared pins | Re-read the schematic against every `#define`; ESP32-S3 strapping pins are 0, 3, 45, 46 |
| Panel controller init | Replace the init sequence table (ST7701 here). Source: the panel vendor, Arduino_GFX's device configs, or `esp_lcd` drivers. Include the pixel-format register (`0x3A`) | Colour test red/green/blue/white (01) |
| RGB timings | PCLK, hsync/vsync polarity, front porch, pulse width, back porch, `pclk_active_neg`, `de_idle_high`. Start from the vendor or Arduino_GFX config. Keep PCLK modest (this board runs 12 MHz); the quoted limit for octal PSRAM at 80 MHz is about 22 MHz | Stable image under load; see the timing-variants table in doc 04 |
| Colour and bus width | 16-bit RGB565 here (the 18-bit panel's low bits unconnected). A different bus width or `useBigEndian` changes the data pin list and the byte order | Colours correct; no swapped red and blue |
| Rotation flush path | `flushRot3` maps logical (x, y) to framebuffer row `W-1-x`, column `y` and **assumes a square panel** (width is used for both dimensions). For a non-square panel, use separate width and height and recompute strides. Re-derive the mapping for each rotation you support and the cache write-back ranges | Compare against the generic `draw16bitRGBBitmap` path; time a full-screen redraw |
| Bounce buffers | Pixel count must divide the frame size; costs 2 x px x 2 bytes of internal RAM. Start with 10 lines, use 20 if PSRAM is also used by Wi-Fi/TLS/JPEG | Finger-paint test (01) with and without; a soak |
| PSRAM | Octal (`qio_opi`) or quad (`qio_qspi`) in `board_build.arduino.memory_type`; N16R8 here. Quad PSRAM has less bandwidth: expect to need a lower PCLK or more bounce buffer, and check Espressif's FAQ for limits. Confirm the framebuffer (W x H x 2 bytes) fits | `ESP.getFreePsram()` at boot; the `initDisplay` guard |
| Flash and partitions | `board_upload.flash_size`, `board_build.flash_mode`, partition CSV (this board: 16 MB, `default_16MB.csv` with two OTA slots) | `flash-id` with esptool; size in the boot log |
| Touch controller | Driver, I2C address(es), whether INT/RST are wired (not on this board, so the address cannot be forced), coordinate transform per rotation, release detection | Touch coordinates on screen and serial (01) at every rotation |
| Backlight | Pin, PWM frequency, minimum duty where the driver cuts out. Here: 100 kHz LEDC, floor `B4848_LCD_BL_MIN` 100 | Dim from 255 down; listen for whine |
| Console and USB | Which UART or USB the console uses; `ARDUINO_USB_CDC_ON_BOOT`; whether native USB pins are free or used by the panel | Serial output at boot |
| SD | Pins and whether the bus is shared with the panel's init bus; keep "display first, SD second" if shared | 06 test bench |
| Shared pins and relays/audio | Document which links or jumpers select each function | Schematic |
| PlatformIO | New `board` or a board JSON; keep the pinned platform unless the chip needs a newer one; check Arduino_GFX supports the panel and chip | A clean build |
| LVGL buffer and memory defaults | Re-measure the internal RAM budget; set `B4848_LVGL_BUF_LINES` from what is left | `[mem]` boot trace |
| Performance defaults | `-O2`, FAST_MEM, buffer lines were measured on this board only. Re-run the `bench` env and log the result | Benchmark table |
| Docs | New pinout, timing and gotchas pages; update `projects/README.md` | |

Bring-up order on a new board: (1) serial and flash only; (2) the display with raw colours; (3) backlight; (4) touch; (5) a minimal LVGL page; (6) the benchmark; (7) SD; (8) Wi-Fi with the display running, watching for flicker; (9) soak. A chip with a different architecture (an ESP32-P4 board, see [doc 10](../10-esp32-p4-displays.md)) needs its own toolchain notes: Arduino support there lags, and this guide's memory numbers do not apply.

## 14. Pre-flight checklist

Run through this before the first flash of a new project and again before calling it done.

| # | Check | How | Pass |
|---|---|---|---|
| 1 | Shell is PowerShell | `$PSVersionTable` | Not Git Bash |
| 2 | Factory flash backed up | file in the backup folder, not committed in a public repo | Exists, hash noted |
| 3 | Project numbered, listed, README written | `projects/README.md` row | Yes |
| 4 | `platformio.ini` extends the base | read it | `extends = base_4848s040`, pinned libs |
| 5 | `qio_opi` and `ARDUINO_USB_CDC_ON_BOOT=0` untouched | read the merged config | Yes |
| 6 | `lv_conf.h` matches the project type | section 5 | Heap, FAST_MEM, buffer lines chosen on purpose |
| 7 | Pins only via `B4848_*` macros | `Select-String -Path projects\NN-name\src\* -Pattern 'GPIO|\bpinMode\('` | No raw GPIO numbers |
| 8 | Secrets ignored, example committed | `git check-ignore -v ...secrets.h` | Ignored; example has fake values |
| 9 | Init order | section 8 | Display, SD, touch, LVGL, network |
| 10 | Touch rotation equals display rotation | read `setRotation` and `initDisplay` | Same value |
| 11 | No runtime flash writes | search for `Preferences`, `SPIFFS`, `LittleFS`, `nvs_`; `WiFi.persistent(false)` | None |
| 12 | LVGL only on one core | search for `lv_` calls in tasks | Only in `loop()` and its callbacks |
| 13 | Background tasks | `tskIDLE_PRIORITY`, core 0, yielding waits, hard deadlines | Yes |
| 14 | One TLS session at a time; budget under the staleness limit | read the job table | Yes |
| 15 | Pinned CA per host; identifying User-Agent; polite intervals | `ca_certs.h`, `net_http.h`, job table | Yes |
| 16 | Heavy data in PSRAM | bodies, JSON, images | `psramAlloc`/`HeapBuf`/`psramJson` |
| 17 | Change-only setters; no `clip_corner` on redrawn objects | search `clip_corner`, `lv_label_set_text` in refresh code | Clean |
| 18 | `tools/build-all.ps1` | run | All PASS |
| 19 | `tools/lint.ps1 NN-name` | run | Exit 0 |
| 20 | Host tests for pure logic | `hosttest/run.ps1` | 0 failed |
| 21 | Heartbeat healthy | section 11 | Min internal free about 100 KB or more, 0 failures, no stalls |
| 22 | Soak done | 10 min, then 1 h, then a day for network/memory changes | No resets, no growth |
| 23 | Every screen checked by screenshot | `x` command | No clipping or empty states |
| 24 | Screen watched and listened to under load | visual | No flicker, shift or crackle |
| 25 | Benchmark if render/flush changed | `03-lvgl-demos` `env:bench` | Not worse than the recorded row |
| 26 | Docs and status | READMEs, `hardware-log.md` | Status honest (`builds` until seen on hardware) |
| 27 | Public-repo scan | section 12 item 5 | No secrets, IPs, MACs, names, coordinates, logs |

---

## Sources

- PlatformIO documentation: https://docs.platformio.org/
- pioarduino platform (pinned release): https://github.com/pioarduino/platform-espressif32
- Arduino-ESP32 core: https://github.com/espressif/arduino-esp32
- Arduino_GFX: https://github.com/moononournation/Arduino_GFX
- LVGL documentation: https://docs.lvgl.io/ and https://github.com/lvgl/lvgl (changelog for version bumps)
- Espressif LCD FAQ (bounce buffers, PCLK limits): https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html
- ESP-IDF 5.4 RGB LCD driver: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- ESP-IDF 5.4 external RAM: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/external-ram.html
- ESP32-S3 strapping pins (datasheet): https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- WCH CH340/CH341 driver: https://www.wch-ic.com/downloads/CH341SER_EXE.html
- esptool: https://docs.espressif.com/projects/esptool/
- IETF RFC 5737, documentation IP address ranges (192.0.2.0/24): https://www.rfc-editor.org/rfc/rfc5737
