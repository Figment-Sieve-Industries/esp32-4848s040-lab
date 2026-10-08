# Projects index

This is the single list of projects; the root README links here. Each project is a PlatformIO project in its own numbered folder. All projects:
- **extend the shared settings** in [`../common/platformio-base.ini`](../common/platformio-base.ini)
- **use the board library** [`../common/lib/Board4848S040`](../common/lib/Board4848S040) (pulled in via `symlink://`)

| # | Folder | Status | Description |
|---|---|---|---|
| 01 | [01-hello-display-touch](01-hello-display-touch/) | builds | Colour test, finger-paint, touch coordinates on screen and serial. Plain Arduino_GFX. |
| 02 | [02-lvgl-hello](02-lvgl-hello/) | builds | LVGL 9.5 base: button, backlight slider, FPS monitor. **Copy this for UI projects.** |

Status values: `idea` → `wip` → `builds` → `works on hw` → `done`. "builds" means it compiles; it has **not** been run on a board yet.

## Planned
| Idea | Notes |
|---|---|
| 03-sd-audio | microSD (SPI, after display init) + I2S to the NS4168. Needs the 0 Ω link move R25–27 → R21–23. Prior art: the vendor music demo, Yoradio. |
| 04-wifi-ota | Wi-Fi provisioning + ArduinoOTA + mDNS; second env with `upload_protocol = espota` |
| 05-esphome-minimal | Minimal ESPHome YAML (display, touch, backlight, OTA) for Home Assistant |
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
