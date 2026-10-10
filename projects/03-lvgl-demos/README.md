# 03 — lvgl-demos

**Goal:** show off the display with LVGL's official demos, and measure performance.

**What it does:** a touch launcher with four demos:
| Demo | |
|---|---|
| Widgets | Dashboard-style UI: charts, forms, lists |
| Music | Animated music player, `LV_DEMO_MUSIC_SQUARE` layout, auto-play |
| Benchmark | Scenes with an FPS/CPU summary; results also go to serial |
| Stress | Heavy animation torture test |

- Tap a demo: the choice is saved to NVS and the board restarts into it.
- The round **home** button (bottom-left, on `lv_layer_top`) returns to the launcher.
- The choice is one-shot, so any reset or crash also lands back on the launcher.

**How it's built**
- `build_src_filter` adds `.pio/libdeps/<env>/lvgl/demos/`, because PlatformIO only compiles `lvgl/src`.
- `lv_conf.h` uses `LV_STDLIB_CLIB` malloc, so LVGL memory comes from the heap (big blocks go to PSRAM). It also enables Montserrat 12–32 for the demos.

**Run:** `.\tools\pio.ps1 -Project 03-lvgl-demos -Action all` (PowerShell, not Git Bash)

**Results:** the benchmark table for unit 1 is in [docs/hardware-log.md](../../docs/hardware-log.md): average 30 FPS at 87% CPU, with full-screen flush ≈20 ms.

**Known issue:** a restart from the launcher once came back via an RTC watchdog reset (it recovered by itself).

**Status:** `works on hw` (2026-10-10): launcher and Benchmark confirmed.
