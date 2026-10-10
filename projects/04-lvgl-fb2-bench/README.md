# 04 — lvgl-fb2-bench

**Goal:** measure LVGL with two full PSRAM framebuffers and a vsync-synchronised swap, against 03's single framebuffer + 20 ms flush copy.

**What it does:** boots straight into `lv_demo_benchmark` on `board4848::lvglBeginDoubleBuffered()` (`Board4848S040_lvgl_fb2.{h,cpp}`):
- the ESP-IDF RGB driver owns 2 framebuffers in PSRAM (`num_fbs = 2`, bounce buffer 480×10 px)
- LVGL renders straight into the hidden one (`LV_DISPLAY_RENDER_MODE_DIRECT`; LVGL copies the previous frame's dirty areas across itself)
- the flush presents it with `esp_lcd_panel_draw_bitmap()` (pointer swap, no copy) and waits for the driver's frame-complete callback
- every 5 s, serial prints `fb2: presents/s, avg/max wait, timeouts`

Envs: `esp32-4848s040` (DIRECT), `full` (`LV_DISPLAY_RENDER_MODE_FULL`). The panel refreshes at ~42 Hz (12 MHz PCLK), which caps the FPS.

**Run:** `.\tools\pio.ps1 -Project 04-lvgl-fb2-bench -Action all` (PowerShell, not Git Bash)

**Result on hardware (2026-10-10):** it works (0 timeouts, presents synchronised), but it's **slower** than the single-framebuffer path:
| Path | Average |
|---|---|
| DIRECT | 20 FPS |
| FULL | 14 FPS |
| 03 with the v5 defaults | 35 FPS |

Rendering into PSRAM is much slower than into internal RAM, and each frame waits up to one refresh. Kept as an opt-in path for UIs where tearing matters. Full table in [docs/hardware-log.md](../../docs/hardware-log.md).

**Status:** `works on hw` (experiment; not recommended by default)
