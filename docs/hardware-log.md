# Hardware log

What has been verified on real units. Add an entry each time something is tested.

## Unit 1

| Item | Result |
|---|---|
| Chip | ESP32-S3 (QFN56) rev v0.2, 40 MHz crystal |
| Flash | **16 MB** detected (mfr 0x68, dev 0x4018). The factory bootloader header says 8 MB; that's just a conservative vendor setting. |
| PSRAM | 8 MB embedded (AP_3v3), octal. 8,384,052 bytes free at boot with `qio_opi` |
| USB | CH340C on COM9 (1A86:7523). Auto-reset upload works with no buttons |
| Factory backup | `firmware/factory/<mac>-20261010-0934.bin` (Unit 1; named by the chip MAC), SHA256 `DA9F18AE…1A33`. Factory partitions: nvs, otadata, one 7.9 MB app0, coredump |
| Display | ST7701 init + RGB timings OK. Red/green/blue/white colour test correct |
| Flicker | Whole-screen flicker while drawing **without** bounce buffers. **Stable with** `B4848_LCD_BOUNCE_PX` = 480×10 |
| Touch | GT911 at **0x5D**, product ID "911". Finger drawing tracks correctly |
| Backlight | 100 kHz LEDC: smooth dimming 255 → 100, **still lit at 100** (`B4848_LCD_BL_MIN`), **no audible whine**. The true cut-out point below 100 isn't measured yet |
| LVGL (02-lvgl-hello) | LVGL 9.5 via `lvglBegin()`: renders correctly, button taps count once each, slider drags smoothly, no flicker |
| Restart quirk | Restarting via `ESP.restart()` from the 03 launcher once came back as `RTCWDT_RTC_RST` (RTC watchdog) instead of a software reset. It recovered by itself. Suspected: restarting while the RGB panel DMA is running. Not fixed yet |
| Wi-Fi + HTTPS | Joins the home 2.4 GHz network (2.4 GHz, ch 13, ≈ −60 dBm). HTTPS to the Powerwall gateway works with TLS and the LVGL UI running (~140 KB internal heap free while polling) |
| 05 Powerwall display | 4 LVGL screens + Wi-Fi + 2 TLS endpoints: ~188 KB internal free at boot, ~140 KB polling, 20-50 fps. Powerwall 3 fw 26.26.4 basic LAN API: only meters/aggregates, soe and grid_status answer (all else 404); aggregates include voltage, frequency, reactive/apparent power and lifetime Wh counters |
| SD card (06-sd-card-test, 2026-10-11) | Works in SPI mode on its own bus after display init. **Core 3.2.1 SD driver bug:** no busy-wait after the multi-block Stop Tran token, so large writes failed intermittently at every clock (1-25 MHz); fixed in `common/lib/SD`. 16 GB SDHC (CID maker 0x82 "JT" "NCard", 2014, class 10/U1): 19/19 tests at 20-25 MHz, 1 MB write ~670 KB/s, read ~1.47 MB/s, log append ~17 ms. 2 GB SanDisk SU02G SDSC (2011, no class): corrupt responses above 4 MHz, flaky at 4 MHz, retired. Backlight off made no difference (not a supply problem). Hot-swap: pull/re-insert recovers without reset; inserting a card can briefly drop the PC's CH340 USB link |
| Rotated flush (05, 2026-10-11) | With `SCREEN_ROTATION` 3, Arduino_GFX's `draw16bitRGBBitmap` wrote each LVGL strip column-wise and wrote back every full framebuffer row it touched (~460 KB of cache write-back for a 480x48 strip). Full-screen redraw **254 ms -> 142 ms** and the ~300 ms many-area redraws (visible stutter) **-> ~145-185 ms** after `lvglBegin`'s own rotation-3 path (contiguous row segments + per-segment write-back). Crackle under PSRAM load: also helped by 2x bounce buffers (480x20) and keeping mbedTLS's small blocks in internal RAM |
| Rotated flush, 03 bench (2026-10-11) | A/B in `03-lvgl-demos` (envs `bench`, `bench-rot3`, `bench-rot3-gfx`): average 35 / 33 / 29 FPS, flush 9 / 13 / 49 ms. Full-screen flush 20 ms (rotation 0), 27 ms (rotation 3, own path), 110-152 ms (rotation 3, Arduino_GFX). Render time identical. Table: [guide 03](guide/03-investigations.md#inv-stutter-rotated-flush) |
| Backlight control v2 (2026-10-11) | Gamma slider + `bf`/`bd`/`br`/`bs` commands on unit 1 (100 kHz, 8 bits by default). Owner: "the tiniest bit of whine but that's fine" (frequency and duty at the time not recorded; earlier 8-bit 100 kHz test heard none). Full frequency sweep not done |
| History screen + day store (2026-10-11) | Day / week / month views correct on unit 1 (screenshots). Idle on the screen: 0 fps, no redraw. Tab / period change renders 100-117 ms (~85-116k px). Month-file save 14 KB: 210-260 ms (one 632 ms in a soak), loads 76-78 ms. Internal free unchanged at ~132 KB polling. Sunrise / sunset row on weather shows |
| Screen on/off switches (2026-10-11) | `e` command and Settings switches: navigation skips off screens, `s` to an off screen errors, mask survives a reboot (after the SET3 load fix) |
| Panel drift (2026-10-11) | Image shifted sideways once during a soak, cleared by a reboot; framebuffer was correct. 60 forced SD saves did not reproduce it. Open: guide 03 `inv-panel-drift` |
| Not yet tested | Corner accuracy, backlight cut-out below 100, audio, relays, battery |

### LVGL benchmark, unit 1 (2026-10-10)
Setup: `03-lvgl-demos`, LVGL 9.5.0, Arduino core 3.2.1 (`-Os`), partial render with a 48-line internal-RAM buffer, flush via `draw16bitRGBBitmap` into a single PSRAM framebuffer, 12 MHz PCLK, bounce buffers 480×10.

| Scene | CPU | FPS | Avg ms | Render ms | Flush ms |
|---|---|---|---|---|---|
| Empty screen | 85% | 21 | 38 | 18 | 20 |
| Moving wallpaper | 96% | 16 | 55 | 34 | 21 |
| Single rectangle | 61% | 60 | 3 | 1 | 2 |
| Multiple rectangles | 90% | 35 | 21 | 10 | 11 |
| Multiple RGB images | 80% | 55 | 9 | 7 | 2 |
| Multiple ARGB images | 87% | 44 | 16 | 13 | 3 |
| Rotated ARGB images | 86% | 44 | 17 | 15 | 2 |
| Multiple labels | 93% | 26 | 31 | 25 | 6 |
| Screen sized text | 98% | 7 | 129 | 108 | 21 |
| Multiple arcs | 67% | 58 | 5 | 5 | 0 |
| Containers | 77% | 46 | 19 | 16 | 3 |
| Containers with overlay | 95% | 13 | 63 | 49 | 14 |
| Containers with opa | 85% | 39 | 23 | 20 | 3 |
| Containers with opa_layer | 97% | 10 | 89 | 82 | 7 |
| Containers with scrolling | 97% | 10 | 85 | 64 | 21 |
| Widgets demo | 98% | 6 | 114 | 96 | 18 |
| **All scenes avg.** | **87%** | **30** | 44 | 35 | 9 |

Reading: a full-screen flush (CPU copy of 460 KB into PSRAM) costs about 20 ms. Text and opa layers are render-bound. Small-area updates run at 44–60 FPS. Optimisation candidates, in order of effort: `-O2` + `LV_ATTRIBUTE_FAST_MEM`; a bigger draw buffer; double framebuffers with vsync swap (needs our own `esp_lcd` setup, no copy, no tearing); esp_lvgl_port SIMD blend routines.

### LVGL performance experiments, unit 1 (2026-10-10)
These were automated: the `bench` env of `03-lvgl-demos` boots straight into the Benchmark, and serial is captured and parsed. Each variant adds to the previous one unless noted. v1 (`-O2` via build_flags alone) is omitted: it produced a **byte-identical** firmware, because the core appends `-Os` after build_flags.

Cells: FPS (CPU%) [render ms / flush ms]

| Scene | v0 baseline | v2 +fast mem | v3 +120 lines | v4 +2 draw threads | **v5 +real -O2** | v6 2 FB direct | v7 2 FB full |
|---|---|---|---|---|---|---|---|
| Empty screen | 21 (83%) [18/20] | 21 (83%) [17/20] | 24 (83%) [12/20] | 22 (100%) [16/19] | 25 (84%) [9/21] | 19 (84%) [30/12] | 19 (85%) [29/12] |
| Moving wallpaper | 16 (95%) [34/21] | 17 (94%) [32/20] | 16 (94%) [32/20] | 17 (100%) [30/21] | 17 (96%) [30/20] | 10 (96%) [85/3] | 10 (96%) [81/8] |
| Single rectangle | 59 (47%) [1/2] | 60 (56%) [1/2] | 61 (55%) [1/2] | 61 (100%) [1/2] | 60 (40%) [1/2] | 42 (89%) [4/16] | 21 (95%) [32/11] |
| Multiple rectangles | 35 (88%) [10/12] | 37 (87%) [10/11] | 37 (87%) [9/12] | 33 (100%) [11/12] | 40 (87%) [9/10] | 35 (90%) [17/5] | 19 (93%) [42/4] |
| Multiple RGB images | 54 (74%) [6/3] | 56 (74%) [6/2] | 56 (74%) [6/2] | 56 (100%) [7/2] | 56 (72%) [5/2] | 20 (93%) [21/19] | 19 (95%) [42/3] |
| Multiple ARGB images | 41 (85%) [14/4] | 45 (85%) [12/3] | 44 (84%) [12/3] | 44 (100%) [12/3] | 46 (89%) [12/3] | 20 (94%) [26/15] | 14 (96%) [49/16] |
| Rotated ARGB images | 45 (83%) [15/1] | 45 (83%) [15/2] | 45 (83%) [15/2] | 45 (100%) [15/2] | 47 (82%) [15/2] | 35 (90%) [18/3] | 14 (97%) [46/19] |
| Multiple labels | 27 (91%) [24/6] | 29 (91%) [21/7] | 29 (91%) [21/7] | 27 (100%) [24/7] | 32 (93%) [22/2] | 19 (93%) [27/15] | 17 (91%) [43/16] |
| Screen sized text | 7 (97%) [107/21] | 7 (97%) [105/21] | 10 (96%) [64/21] | 8 (100%) [92/20] | 11 (97%) [60/20] | 10 (96%) [69/20] | 10 (96%) [69/20] |
| Multiple arcs | 58 (57%) [4/0] | 59 (61%) [4/0] | 59 (61%) [4/0] | 59 (100%) [4/0] | 59 (47%) [2/1] | 38 (90%) [6/13] | 21 (95%) [36/5] |
| Containers | 47 (75%) [14/3] | 49 (71%) [14/3] | 55 (65%) [10/3] | 51 (100%) [13/3] | 57 (51%) [8/3] | 21 (92%) [27/13] | 14 (96%) [56/8] |
| Containers with overlay | 13 (94%) [51/15] | 14 (93%) [47/15] | 15 (93%) [41/16] | 14 (100%) [49/15] | 16 (92%) [37/17] | 12 (95%) [59/9] | 10 (97%) [82/7] |
| Containers with opa | 39 (83%) [20/3] | 43 (80%) [17/3] | 50 (76%) [14/3] | 43 (100%) [17/3] | 54 (63%) [12/3] | 18 (95%) [30/12] | 14 (97%) [61/3] |
| Containers with opa_layer | 10 (94%) [83/7] | 10 (96%) [77/7] | 15 (92%) [52/5] | 12 (100%) [67/6] | 21 (91%) [40/4] | 9 (96%) [76/11] | 8 (97%) [96/16] |
| Containers with scrolling | 10 (97%) [65/21] | 11 (96%) [58/21] | 14 (96%) [44/21] | 11 (100%) [59/20] | 15 (96%) [40/20] | 10 (97%) [73/16] | 10 (97%) [73/16] |
| Widgets demo | 6 (97%) [96/18] | 7 (97%) [87/17] | 8 (97%) [62/17] | 6 (100%) [90/17] | 9 (97%) [56/17] | 7 (97%) [82/12] | 6 (98%) [114/11] |
| All scenes avg. | 30 (83%) [35/9] | 31 (84%) [32/9] | 33 (82%) [24/9] | 31 (100%) [31/9] | 35 (79%) [22/9] | 20 (92%) [40/12] | 14 (95%) [59/10] |

| Variant | Free internal heap at boot |
|---|---|
| v0 baseline | 267 KB |
| v2 + `LV_ATTRIBUTE_FAST_MEM` (IRAM) | 212 KB |
| v3 + 120-line draw buffer | 143 KB |
| v5 + real `-O2` (`build_unflags = -Os`) | 87 KB |
| v6/v7 double framebuffer (no draw buffer) | 314 KB (but +460 KB PSRAM) |

**Conclusions**
- **Confirmed after making them the shared defaults:** `03` bench env = 35 FPS at 76% CPU, render 21 ms, 86 KB internal heap free.
- **v5 is the new default** (`common/platformio-base.ini` + `lv_conf.h` + `B4848_LVGL_BUF_LINES` 120):
  - average 30 → 35 FPS, render 35 → 22 ms
  - heavy scenes up 50–110%: opa_layer 10 → 21 FPS, text 7 → 11, containers with opa 39 → 54
- **Bigger buffer was the single biggest win** (render −31%), then `-O2`, then IRAM.
- **Two LVGL draw threads (v4) are slower:** threading overhead outweighs the second core for this workload, and the CPU reading is pinned at 100%.
- **Double framebuffers (v6/v7, project 04) are slower:**
  - rendering straight into PSRAM is much slower than into internal RAM
  - each frame also waits up to a refresh (~24 ms at 42 Hz)
  - the swap itself works: 0 timeouts, presents synchronised
  - kept as an opt-in path only in case tearing matters for a specific UI
- **Remaining limit:** full-screen flush ≈ 20 ms (CPU copy into the PSRAM framebuffer). Untried next steps: async DMA copy (`esp_async_memcpy`) overlapping render and flush with 2 draw buffers; esp_lvgl_port SIMD blend.
