# Projects index

This is the single list of projects; the root README links here. Each project is a PlatformIO project in its own numbered folder. All projects:
- **extend the shared settings** in [`../common/platformio-base.ini`](../common/platformio-base.ini)
- **use the board library** [`../common/lib/Board4848S040`](../common/lib/Board4848S040) (pulled in via `symlink://`)

| # | Folder | Status | Description |
|---|---|---|---|
| 01 | [01-hello-display-touch](01-hello-display-touch/) | works on hw | Colour test, finger-paint, touch coordinates on screen and serial. Plain Arduino_GFX. |
| 02 | [02-lvgl-hello](02-lvgl-hello/) | works on hw | LVGL 9.5 base: button, backlight slider, FPS monitor. **Copy this for UI projects.** |
| 03 | [03-lvgl-demos](03-lvgl-demos/) | works on hw | Touch launcher for LVGL's official demos: Widgets, Music (square), Benchmark, Stress. Benchmark results are in the [hardware log](../docs/hardware-log.md). |
| 04 | [04-lvgl-fb2-bench](04-lvgl-fb2-bench/) | works on hw | Experiment: double framebuffers + vsync swap. Works, but measured slower (20 vs 35 FPS). See the hardware log. |
| 05 | [05-powerwall-display](05-powerwall-display/) | works on hw | **Tesla Powerwall 3 display** from the gateway's local LAN API (keep-alive HTTPS). Ten swipeable screens: power flow, today, battery & grid, weather, moon, aurora (UK), sun & space weather, El Nino, settings (brightness, SD status), about. SD minute log, power-cut-safe today totals, SD cache of fetched data (screens fill ~2 s after boot). Serial test commands and screenshots. No internal-flash writes at runtime. Needs `src/secrets.h`. |
| 06 | [06-sd-card-test](06-sd-card-test/) | works on hw | microSD bring-up and the shared SD code (`common/lib/SdStore`, patched `common/lib/SD`): mount, card registers, 19 functional tests, clock sweep, soak, hot-swap. Fully drivable over serial (`sd_serial.py`). |

Status values: `idea` → `wip` → `builds` → `works on hw` → `done`. "builds" means it compiles; it has **not** been run on a board yet.

## Planned
Numbers are assigned when a project starts.

| Idea | Notes |
|---|---|
| sd-audio | microSD (use `common/lib/SdStore`, see 06) + I2S to the NS4168. Needs the 0 Ω link move R25–27 → R21–23. Prior art: the vendor music demo, Yoradio. |
| wifi-ota | Wi-Fi provisioning + ArduinoOTA + mDNS; second env with `upload_protocol = espota` |
| esphome-minimal | Minimal ESPHome YAML (display, touch, backlight, OTA) for Home Assistant |
| LVGL performance, round 2 | Done so far: `-O2`, IRAM, 120-line buffer (35 FPS); double FB and 2 draw threads were slower. Next: async DMA flush (`esp_async_memcpy`) with 2 draw buffers to hide the 20 ms copy, then esp_lvgl_port SIMD blend. |
| 05 follow-ups | OTA updates (it will be wall-mounted); day/week/month charts from the SD log; a VPP test toggle; optional TEDAPI access for backup reserve / operation mode; fewer simultaneous label redraws on the power-flow screen (the remaining ~150 ms frames). |
| Backlight range | The 05 brightness slider barely changes the backlight. Plan: (A) lower the PWM from 100 kHz to ~20-25 kHz after a serial-driven sweep with someone watching/listening, then re-measure the minimum; (B) gamma curve on the slider. Details: [docs/02-pinout.md](../docs/02-pinout.md#backlight-dimming-range-planned-not-yet-done). |
| LovyanGFX alternative | Port the shared lib to LovyanGFX (`Panel_ST7701_guition_esp32_4848S040`, see `reference/ESP32-4848S040-LVGL-LGFX`) and compare speed |

## Building
From PowerShell at the repo root (not Git Bash):
```powershell
.\tools\pio.ps1 -Project 02-lvgl-hello -Action all     # build + upload + monitor
.\tools\build-all.ps1                                   # build every project
```

## Adding a new project
1. Copy `02-lvgl-hello` (UI projects) or `01-hello-display-touch` (plain Arduino_GFX) to `NN-short-name` (next free number). **Skip the `.pio/` folder**, which is large build output.
2. Keep the `platformio.ini` pattern:
   ```ini
   [platformio]
   extra_configs = ../../common/platformio-base.ini

   [env:esp32-4848s040]
   extends = base_4848s040
   ; project-specific additions, e.g.
   ; lib_deps = ${base_4848s040.lib_deps}
   ;     lvgl/lvgl @ 9.5.0
   ```
3. Edit the header comment in `platformio.ini` and `src/main.cpp`.
4. Write a short `README.md` in the project folder: goal, how to run, status.
5. Add a row to the table above. This is the only index of projects.
6. Commit.

Non-PlatformIO projects (ESPHome YAML, ESP-IDF) go in their own numbered folders too. Note the toolchain in the README.
