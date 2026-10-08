# 02 — lvgl-hello

**Goal:** a working LVGL 9 base for real UIs.

**What it does:** shows a title, a button that counts presses, and a slider that dims the backlight. The FPS/CPU monitor is in the bottom-right corner; turn it off in `include/lv_conf.h`.

**How it's wired up**
- LVGL **9.5.0** (pinned). Config is a minimal `include/lv_conf.h`; everything else uses LVGL's defaults.
- **Partial render mode**: a 48-line (46 KB) draw buffer in internal RAM. `flushCb` copies each area into the PSRAM framebuffer with `draw16bitRGBBitmap`. No RGB565 byte swap is needed for an RGB panel.
- Touch: `board4848::Touch` polled from the LVGL read callback. The press is held for 60 ms between GT911 reports.

**Notes for building on this**
- For direct mode (rendering straight into the framebuffer), construct the display with `auto_flush=false` or call `gfx->flush(true)`. Otherwise cache writeback is skipped (Arduino_GFX 1.6.9 behaviour).
- To design screens visually, export from SquareLine Studio or EEZ Studio into `src/ui/` and call `ui_init()` instead of `buildUi()`.
- LVGL 9.6 renamed many things. Re-check `lv_conf.h` before bumping the version.

**Run:** `pio run -t upload` then `pio device monitor` (from PowerShell or VS Code, not Git Bash).

**Status:** builds ✅ (not yet run on hardware)
