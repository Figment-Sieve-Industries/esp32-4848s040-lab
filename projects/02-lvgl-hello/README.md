# 02 — lvgl-hello

**Goal:** a working LVGL 9 base for real UIs.

**What it does:** shows a title, a button that counts presses, and a slider that dims the backlight (range `B4848_LCD_BL_MIN`..255). The FPS/CPU monitor is in the bottom-right corner; turn it off in `include/lv_conf.h`.

**How it's wired up**
- LVGL **9.5.0** (pinned in `platformio.ini`). Config is a minimal `include/lv_conf.h` (`LV_MEM_SIZE` 48 KB); everything else uses LVGL's defaults.
- `board4848::lvglBegin(gfx, &touch)` from the shared lib (`Board4848S040_lvgl.h`) does all the LVGL glue:
  - tick
  - a 48-line (46 KB) partial draw buffer in internal RAM
  - a flush that copies each area into the PSRAM framebuffer with `draw16bitRGBBitmap` (no RGB565 byte swap needed for an RGB panel)
  - a GT911 input device, with a 100 ms release fallback
- `main.cpp` only builds the UI.

**Notes for building on this**
- For direct mode (rendering straight into the framebuffer), construct the display with `auto_flush=false` or call `gfx->flush(true)`. Otherwise cache writeback is skipped (Arduino_GFX 1.6.9 behaviour).
- To design screens visually, export from SquareLine Studio or EEZ Studio into `src/ui/` and call `ui_init()` instead of `buildUi()`.
- Before bumping to LVGL 9.6+, re-check `lv_conf.h` against that version's `lv_conf_template.h` (9.6 deprecated `LV_COLOR_DEPTH` and moved headers). See the [LVGL changelog](https://github.com/lvgl/lvgl/blob/master/docs/src/changelog/CHANGELOG.mdx).

**Run** (PowerShell at the repo root, not Git Bash)
```powershell
.\tools\pio.ps1 -Project 02-lvgl-hello -Action all
```

**Status:** `builds` (not yet run on hardware)
