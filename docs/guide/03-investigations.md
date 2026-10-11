# Investigations log

[Guide index](README.md) | [01 Quirks](01-quirks-and-gotchas.md) | [02 Debugging](02-debugging-playbook.md) | [03 Investigations](03-investigations.md) | [04 Patterns](04-patterns-and-decisions.md) | [05 New project](05-new-project-checklist.md) | [06 Hardware](06-hardware-and-sourcing.md) | [Credits](99-sources-and-credits.md)

Every problem on this workspace that needed real debugging, written up so the next project starts from what was measured rather than from guesses. One section per investigation: what was seen, what was suspected, what was tried in order, the numbers before and after, and what is still open.

Hardware: one Guition ESP32-4848S040C_I ("unit 1"; ESP32-S3 N16R8, 4" 480x480 ST7701 RGB panel, GT911, microSD, CH340C). Toolchain: PlatformIO with pioarduino `54.03.21-2` (Arduino core 3.2.1, IDF 5.4), LVGL 9.5.0, Arduino_GFX 1.6.9. Nothing here has been repeated on a second unit.

## How to read this

- **Status words.** `fixed` = a change is committed and the number moved. `works on hw` = the user saw the fix on the panel (only used where the user said so). `mitigated` = better, not gone. `open` = not solved. `rejected` = tried or analysed and dropped. `diagnosed` = cause understood, nothing changed yet.
- **Sources.** Each number names where it came from:
  - repo docs (relative links);
  - commit hashes with their subject;
  - serial logs captured during the session (`soak15.log`, `soak-flush.log`, `soak-flush2.log`, `soak-refactor.log`, `soak-keepalive.log`). These were not committed; the figures quoted here were extracted from them with a script;
  - session transcripts, cited as "transcript, <date>". These are the working notes, serial captures and tool output from the sessions that did the work.
- **(inferred)** marks a conclusion that no measurement isolated.
- **Commit hashes** are those of the history on 2026-10-11. The history was rewritten once (2026-10-10, to remove a network name and an address) and may be rewritten again before publication. If a hash does not resolve, search for the commit subject.
- **Privacy.** Quoted serial lines are trimmed. Network names, addresses and locations are removed or shown as `<home-ssid>`.
- **Units.** KB = 1024 bytes where a log gave bytes; heap figures are internal RAM (`MALLOC_CAP_INTERNAL`) unless marked PSRAM.

## Index

| ID / anchor | Title | Area | Status | Key before -> after | Date |
|---|---|---|---|---|---|
| [inv-flicker-bounce](#inv-flicker-bounce) | Whole-screen flicker while drawing | Display | works on hw | flicker -> stable with 480x10 bounce buffers | 2026-10-10 |
| [inv-backlight-pwm](#inv-backlight-pwm) | Backlight PWM silently stuck at 1 kHz | Display | works on hw | 1 kHz (source analysis) -> 100 kHz, no whine | 2026-10-08 / 10 |
| [inv-perf-defaults](#inv-perf-defaults) | LVGL speed defaults and the `-O2` trap | Performance | fixed | 30 -> 35 FPS average; render 35 -> 22 ms | 2026-10-10 |
| [inv-double-framebuffer](#inv-double-framebuffer) | Double framebuffers and 2 draw threads | Performance | rejected | 35 FPS -> 20 (DIRECT) / 14 (FULL) | 2026-10-10 |
| [inv-restart-rtcwdt](#inv-restart-rtcwdt) | `ESP.restart()` came back as an RTC watchdog reset | System | open | seen once | 2026-10-10 |
| [inv-tls-oom](#inv-tls-oom) | `SSL - Memory allocation failed` (-32512) | Memory / network | fixed | boot free 60,768 B -> 170,236 B | 2026-10-10 |
| [inv-chunked-json](#inv-chunked-json) | Gateway JSON parsed as a number | Network | fixed | all fields 0 -> parsed | 2026-10-10 |
| [inv-jerky-flow](#inv-jerky-flow) | Jerky power-flow animation | UI | works on hw | ~28 FPS, "much smoother" | 2026-10-10 |
| [inv-lvgl-heap-psram](#inv-lvgl-heap-psram) | Internal RAM drained by LVGL widgets | Memory | fixed | polling free 37,764 B -> 140,060 B | 2026-10-10 |
| [inv-chunked-stall](#inv-chunked-stall) | Task watchdog in `HTTPClient` on kept-alive TLS | Network | fixed | wdt within 30 s -> 4 min clean | 2026-10-10 / 11 |
| [inv-vpp-flag](#inv-vpp-flag) | VPP event not shown | Data | open | `grid_services_active` false during an event | 2026-10-10 |
| [inv-sd-multiblock](#inv-sd-multiblock) | SD large writes fail at every clock | Storage | works on hw | 7 of 10 runs failing -> 4 of 4 pass | 2026-10-11 |
| [inv-sd-card-clock](#inv-sd-card-clock) | Choosing the SD clock per card | Storage | fixed | 2 GB card: rated 25 MHz, flaky at 4 MHz, failing at 10 MHz+ | 2026-10-11 |
| [inv-heredoc-newlines](#inv-heredoc-newlines) | Shell heredocs corrupt C string escapes | Tooling | mitigated | 6 incidents over 2 days | 2026-10-10 / 11 |
| [inv-lint-setup](#inv-lint-setup) | Making static analysis actually analyse | Tooling | fixed | 357 -> 3 raw findings on 05 | 2026-10-11 |
| [inv-brightness-range](#inv-brightness-range) | Brightness slider makes little difference | Display | diagnosed | duty 100-255 at 100 kHz | 2026-10-11 |
| [inv-internal-ram](#inv-internal-ram) | Internal RAM low point below 100 KB | Memory | fixed | min ever 92,136 B -> 146,116 B -> 118,096 B | 2026-10-11 |
| [inv-loop-stack-overflow](#inv-loop-stack-overflow) | Core 1 panic: LVGL task stack nearly full | Memory | fixed | stack free 824 B -> 9,624 B | 2026-10-11 |
| [inv-boot-race-lock](#inv-boot-race-lock) | SD task used a mutex before it existed | Concurrency | fixed | assert in `xQueueSemaphoreTake` -> none | 2026-10-11 |
| [inv-crackle](#inv-crackle) | Panel "crackle" after the mbedTLS-to-PSRAM change | Display / memory | works on hw (owner, 2026-10-11) | 1 KB -> 4 KB threshold, 480x10 -> 480x20 bounce | 2026-10-11 |
| [inv-stutter-rotated-flush](#inv-stutter-rotated-flush) | ~300 ms stutters on the main screen | Display / performance | works on hw (much less noticeable; ~135-190 ms frames remain) | full-screen render 254 ms -> 142 ms; many-area 293-309 ms -> 134-196 ms | 2026-10-11 |
| [inv-redraw-load](#inv-redraw-load) | Constant redraw load from animations | UI / performance | mitigated | not isolated | 2026-10-11 |
| [inv-job-budget](#inv-job-budget) | Long background jobs made live data stale | Network | fixed | jobs up to 25 s+ -> 12 s budget | 2026-10-11 |
| [inv-shared-modules](#inv-shared-modules) | Consolidation round: duplicated HTTP code | Code quality | done | flash 2,263,562 -> 2,250,190 B; 217 host tests | 2026-10-11 |
| [inv-tls-gateway-refusals](#inv-tls-gateway-refusals) | Gateway refuses TLS handshakes | Network | open (keep-alive under trial) | 27.9 -> 2.2 TLS connects/min; 19 -> 1 refusals | 2026-10-11 |
| [inv-barchart-empty](#inv-barchart-empty) | Aurora and sun bar strips empty | UI | fixed | empty -> drawn (seen in screenshots) | 2026-10-11 |
| [inv-tooling-pitfalls](#inv-tooling-pitfalls) | Measurements spoiled by tooling | Method | documented | 2 measurements invalidated | 2026-10-11 |
| [inv-minor](#inv-minor) | Smaller findings | Various | mostly fixed | see table | 2026-10-08 to 11 |

---

## Performance and memory baseline history

Project 05 (Powerwall display) unless the stage names another project. "Boot" is the `Free internal heap` line printed at the end of `setup()`, or the 03 benchmark's boot figure. "Polling" is the 10 s heartbeat while the gateway is polled every 2 s. "Min ever" is `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`. "n/r" = not recorded.

| Stage (commit) | Date | Boot internal free | Polling internal free | Min ever | Largest block | FPS | Flash used | Source |
|---|---|---|---|---|---|---|---|---|
| 02-lvgl-hello first build (`27bf530`), `LV_MEM_SIZE` 128 KB static | 2026-10-08 | n/r (static RAM 154,444 B, 47.1%) | - | - | - | n/r | 794,614 B | transcript 2026-10-08 (code review build) |
| 03 bench v0: `-Os`, 48 lines, no fast mem (`e6338b2`) | 2026-10-10 | 267 KB | - | - | - | 30 avg (87% CPU) | ~1.9 MB | [hardware-log](../hardware-log.md); transcript 2026-10-10 |
| 03 bench v5 = new shared defaults (`d17673a`) | 2026-10-10 | 87 KB (86 KB on re-check) | - | - | - | 35 avg (76% CPU) | n/r | [hardware-log](../hardware-log.md) |
| 05 first build, `LV_ATTRIBUTE_FAST_MEM` on, 48 lines | 2026-10-10 | 60,768 B | ~58.5 KB (Wi-Fi not joined) | n/r | n/r | n/r | 1,675,666 B (25.6%) | transcript 2026-10-10 |
| 05 fast mem removed (`ac1ad2b`) | 2026-10-10 | 170,236 B | ~125 KB | n/r | n/r | n/r | ~1.68 MB | transcript; `ac1ad2b` hardware-log row |
| 05 per-frame flow dots (`b78e7bf`) | 2026-10-10 | n/r | 126,576-128,404 B | n/r | n/r | 27.9 | n/r | transcript 2026-10-10 |
| 05 design B (`976f358`) | 2026-10-10 | n/r | "~110 KB to spare" | n/r | n/r | ~35 | n/r | transcript 2026-10-10 |
| 05 four screens, LVGL on the C heap | 2026-10-10 | 83,160 B | 37,764 B | n/r | n/r | 48.8 | n/r | transcript 2026-10-10 |
| 05 four screens, 1 MB LVGL pool in PSRAM (`2dde03f`) | 2026-10-10 | 187,792 B | 140,060-143,468 B | n/r | n/r | 46.1 (23.7 on Today) | n/r | transcript 2026-10-10 |
| 05 design M (`5b601ac`) | 2026-10-10 | n/r | ~143 KB | n/r | n/r | ~50 | n/r | transcript 2026-10-10 |
| 05 cold-review fixes (`ca8bc70`) | 2026-10-10 | 174,112 B | ~133.6 KB | ~112.0 KB | 54,260-69,620 B | n/r | n/r | transcript 2026-10-10 |
| 05 eight screens + SD (`14a049f`, trace `174ae0b`) | 2026-10-11 | 163,436 B | ~160 KB idle, ~119-125 KB with gateway TLS open | 101,592 B, then 92,136 B | 60,404 B, then 44,020 B | 34-47 | 2,200,974 B (33.6%) | transcript 2026-10-11 |
| same, SD task disabled (A/B build, not committed) | 2026-10-11 | 171,856 B | ~168 KB idle, ~117 KB with TLS | 110,068 B | 51,188-67,572 B | n/r | n/r | transcript 2026-10-11 |
| 10 screens, RAM fixes, mbedTLS >= 1 KB to PSRAM, 8 KB loop stack | 2026-10-11 | 172,700 B (after net task step) | ~163-170 KB | 146,116 B | 96,244 B | 42.7-51.7 | 2,262,370 B (34.5%) | transcript 2026-10-11 |
| same + 16 KB loop stack (`cca53c5`) | 2026-10-11 | n/r | ~156 KB | 137,516 B | 88,052 B | n/r | ~2.26 MB | transcript 2026-10-11 |
| crackle fixes: 4 KB threshold, 480x20 bounce (`300e7b5`) | 2026-10-11 | 144,916 B | 130,576-142,336 B (median 136,440) | 118,052 B | 59,380-75,764 B | 39.4-58.2 (median 47.6) | n/r | `soak15.log` (14.9 min) |
| rotated flush (`bd3f28d`) | 2026-10-11 | 144,916 B | 129,552-143,484 B (median 136,468) | 117,576 B | 61,428-75,764 B | 40.2-50.0 (median 41.6) | n/r | `soak-flush2.log` (9.9 min) |
| consolidation refactor (`496462e`) | 2026-10-11 | 142,512 B | 127,276-140,432 B (median 135,108) | 115,104 B | 59,380-71,668 B | 35.6-49.3 (median 37.8) | ~2.25 MB (2,250,190 B on the network branch) | `soak-refactor.log` (9.9 min); `ba7b097` |
| refactor + `-DPW_GW_KEEPALIVE=1` (trial build, not committed) | 2026-10-11 | 142,256 B | 122,536-140,296 B (median 136,444) | 105,400 B | 55,284-60,404 B | 43.5-57.0 (median 55.0) | n/r | `soak-keepalive.log` (9.9 min) |

Reading the table:
- The FPS column is the heartbeat's frame count on the main screen. It depends on how many chevrons are moving, which follows live power flows, so rows are not directly comparable (inferred).
- The project 05 README and the hardware log quote "~188 KB at boot, ~140 KB polling, 20-50 fps". That is the four-screen state (`2dde03f`) and is now out of date.
- From `cca53c5` onwards the "Polling" figure already includes the 2x bounce buffers (`300e7b5`, +19,504 B, see [inv-crackle](#inv-crackle)).

---

## Ideas tried and rejected

| Idea | Evidence | Verdict |
|---|---|---|
| Double framebuffers with a vsync swap (project 04, `LV_DISPLAY_RENDER_MODE_DIRECT` / `FULL`) | 20 / 14 FPS average against 35 for the single-framebuffer v5 path. It works (0 timeouts) but rendering into PSRAM is slower, and each frame waits up to one refresh (~24 ms at 42 Hz). Source: [hardware-log](../hardware-log.md), [04 README](../../projects/04-lvgl-fb2-bench/README.md) | rejected; kept opt-in. [inv-double-framebuffer](#inv-double-framebuffer) |
| Two LVGL draw threads (v4) | 31 FPS at 100% CPU against 33 FPS at 82% for v3. Source: [hardware-log](../hardware-log.md) | rejected |
| `-O2` in `build_flags` alone (v1) | Firmware byte-identical to the baseline: the core appends `-Os` after `build_flags`. Fixed with `build_unflags = -Os`. Source: [hardware-log](../hardware-log.md), [07-gotchas](../07-gotchas.md) | rejected as a method |
| `LV_ATTRIBUTE_FAST_MEM` (IRAM) in a network project | 05 boot free 60,768 B and TLS failing with -32512; 170,236 B and TLS working without it. Source: transcript 2026-10-10 | rejected for 05. [inv-tls-oom](#inv-tls-oom) |
| LVGL on the C heap (`LV_STDLIB_CLIB`) with many screens | Polling free 37,764 B against 140,060 B with a PSRAM pool. Source: transcript 2026-10-10 | rejected. [inv-lvgl-heap-psram](#inv-lvgl-heap-psram) |
| HTTPClient keep-alive with HTTPClient's own chunk reader | Task watchdog (IDLE0) inside `HTTPClient::getString()` within ~20-28 s of boot. Source: transcript 2026-10-10, [07-gotchas](../07-gotchas.md) | rejected; replaced by an own de-chunker. [inv-chunked-stall](#inv-chunked-stall) |
| mbedTLS allocations of 1 KB and up in PSRAM | Low point 92,136 -> 146,116 B, but the panel started to crackle. Moved to a 4 KB threshold. Source: `cca53c5`, `300e7b5`, transcript 2026-10-11 | rejected (threshold too low; crackle gone at 4 KB, confirmed by the owner 2026-10-11). [inv-crackle](#inv-crackle) |
| `custom_sdkconfig` (pioarduino hybrid compile) | Not tried. Reading pioarduino's builder showed it rewrites the shared global framework package and reinstalls it whenever another project builds. Source: [12-esp32-performance-and-framework](../12-esp32-performance-and-framework.md) section 4c | rejected on analysis; `framework = arduino, espidf` is the preferred route if ever needed |
| 120 MHz PSRAM | Not tried: experimental in IDF, risky above 65 °C, and PCLK (12 MHz) is far below the 80 MHz PSRAM limit (~22 MHz). Source: [12](../12-esp32-performance-and-framework.md) section 3 | rejected on analysis |
| Run `onShow` fades on `LV_EVENT_SCREEN_LOADED` (after the slide) | The UI-core agent reasoned that the cards would be fully visible during the slide, then flash to transparent and fade in. Not measured or seen on hardware (inferred). It used a 300 ms show delay behind `-DUI_FADE_AFTER_SLIDE` instead. Source: transcript 2026-10-11 (agent report), `1f816a8` | rejected on reasoning |
| LVGL's own TJpgDec image decoder registered | It sniffed every image LVGL drew for a JPEG header (`is_jpg`). That was in the backtrace of a core 1 panic. Unregistered with `lv_tjpgd_deinit()`. Source: transcript 2026-10-11, `cca53c5` | rejected. [inv-loop-stack-overflow](#inv-loop-stack-overflow) |
| Trust the SD card's rated clock (CSD) | The 2 GB card reports 25 MHz but failed at 10 MHz and above, and once at 4 MHz. Replaced by a mount-time link test. Source: transcript 2026-10-11, [hardware-log](../hardware-log.md) | rejected. [inv-sd-card-clock](#inv-sd-card-clock) |
| SD failures caused by supply droop (backlight load) | 3 of 4 test runs still failed with the backlight off. Source: transcript 2026-10-11 | rejected. [inv-sd-multiblock](#inv-sd-multiblock) |
| `cppcheck -i.pio` to skip library code | No effect: cppcheck still followed includes into the libraries. Replaced by `--suppress=*:*libdeps*`. Source: transcript 2026-10-11 | rejected. [inv-lint-setup](#inv-lint-setup) |
| clang-tidy through `pio check` with framework packages | A full run on project 01 took over 12 minutes and was killed. Replaced by a translated compile database. Source: transcript 2026-10-11 | rejected. [inv-lint-setup](#inv-lint-setup) |
| Inferred-VPP heuristic (battery discharging > 1 kW while exporting) | Started without being asked and reverted at the user's request. Not a technical rejection. Source: transcript 2026-10-10 | reverted. [inv-vpp-flag](#inv-vpp-flag) |

---

## Investigations

### <a id="inv-flicker-bounce"></a><a id="inv-bounce-buffers"></a>Whole-screen flicker while drawing

| | |
|---|---|
| Date | 2026-10-10 |
| Area | Display (RGB panel, PSRAM bandwidth) |
| Status | works on hw (user: "yes thats stable now") |
| Fix | `fa441f3` Enable RGB bounce buffers by default (fixes drawing flicker, verified on hw); hardware log |

**Symptom.** First flash of project 01 on unit 1: "i can draw the trail with my fingers, but the screen flickers a bit as i do" (user, 2026-10-10).

**Hypotheses.**

| Hypothesis | Verdict | Why |
|---|---|---|
| PSRAM bandwidth: CPU writes to the PSRAM framebuffer starve the LCD DMA | kept | Known ESP32-S3 RGB-panel behaviour; the flicker happened only while drawing |
| PCLK too high for PSRAM | held in reserve | It was to be tried next (12 -> 10 or 8 MHz) if bounce buffers only helped partly |
| Backlight or timing (porches) | held in reserve | It was to be checked only if bounce buffers made no difference |

**Attempts.**
1. `initDisplay(rotation, bouncePx = 480*10)`: Arduino_GFX's bounce-buffer argument (two 480x10 px buffers in internal RAM, ~19 KB). Measured by eye on the panel. Result: stable.
2. Project 01 also overwrites the coordinate text in place instead of clear + draw, which removed a small flash in one corner.

**Before / after.**

| | Before | After |
|---|---|---|
| Image while finger-drawing | whole-screen flicker | stable |
| Internal RAM cost | 0 | ~19 KB (2 x 480 x 10 x 2 B, [04-display-st7701](../04-display-st7701.md)) |

**Conclusion.** Bounce buffers are on by default in the shared library. `RESTART_IN_VSYNC` was already enabled in the core's sdkconfig ([12](../12-esp32-performance-and-framework.md) section 1).

**Follow-ups.** Project 05 later doubled the bounce buffers to 480x20 ([inv-crackle](#inv-crackle)).

**Sources.** Transcript 2026-10-10; [hardware-log](../hardware-log.md) "Flicker" row; [07-gotchas](../07-gotchas.md).

---

### <a id="inv-backlight-pwm"></a>Backlight PWM silently stuck at 1 kHz

| | |
|---|---|
| Dates | Found 2026-10-08 (review, no hardware); verified 2026-10-10 |
| Area | Display (backlight LEDC) |
| Status | works on hw |
| Fix | `dca099e` Apply cold-review findings: fix backlight PWM, ...; `82b456d` 02-lvgl-hello works on hw; backlight dims to 100 with no whine (unit 1) |

**Symptom.** None seen; two independent review agents found it in the code before any hardware run. The comment and docs claimed 100 kHz "to avoid coil whine".

**Analysis.** In core 3.2.1, `analogWriteFrequency()` calls `ledcChangeFrequency()`. That finds no LEDC channel on a pin that isn't attached yet, logs at error level (hidden at `CORE_DEBUG_LEVEL=0`) and returns. The first `analogWrite()` then attaches at the default 1 kHz (`esp32-hal-ledc.c`, lines quoted in the transcript).

**Fix.** Call `ledcAttach(B4848_LCD_BL, 100000, 8)` once, then `ledcWrite()`.

**Before / after.**

| | Before | After |
|---|---|---|
| PWM frequency | 1 kHz (from source analysis; not measured with a scope) | 100 kHz (by construction) |
| Audible whine | not tested at 1 kHz | none at any level (user, 2026-10-10) |
| Lowest lit level | n/r | 100/255 still lit (user); cut-out below 100 not measured |

**Follow-ups.** The 100 kHz choice probably compresses the dimming range ([inv-brightness-range](#inv-brightness-range)).

**Sources.** Transcript 2026-10-08 (review reports); transcript 2026-10-10; [07-gotchas](../07-gotchas.md) "Backlight whines".

---

### <a id="inv-perf-defaults"></a><a id="inv-lvgl-perf-defaults"></a>LVGL speed defaults and the `-O2` trap

| | |
|---|---|
| Date | 2026-10-10 |
| Area | Performance |
| Status | fixed (measured; benchmark on hardware) |
| Fix | `d17673a` LVGL performance: -O2 (unflag -Os), IRAM fast mem, 120-line buffer as defaults (30 -> 35 FPS); baseline in `e6338b2` |

**Symptom.** The first LVGL benchmark (project 03) averaged 30 FPS at 87% CPU (the user read it off the screen). Text-heavy and opacity-layer scenes ran at 6-10 FPS, and every full-screen frame paid ~20 ms of flush.

**Hypotheses.**

| Hypothesis | Verdict | Why |
|---|---|---|
| Rendering is CPU-bound because the core builds with `-Os` | kept | Render ms dominated in heavy scenes |
| More, smaller strips cost per-strip overhead (48-line buffer) | kept | Biggest single win (render -31%) |
| Hot LVGL code in flash (cache misses) | kept, small | v2 gave +1 FPS on average |
| A second draw thread would use core 0 | rejected | v4 was slower ([inv-double-framebuffer](#inv-double-framebuffer)) |
| The full-screen 20 ms flush is the cap | kept, not fixed | Needs DMA or no-copy paths; untried |

**Attempts** (automated: a `bench` env boots straight into the benchmark; serial is captured and parsed; each variant adds to the previous).
1. v1 `-O2` in `build_flags`: firmware **byte-identical** to v0, because the core appends `-Os` after `build_flags`.
2. v2 `LV_ATTRIBUTE_FAST_MEM` (IRAM): 31 FPS.
3. v3 120-line draw buffer: 33 FPS.
4. v4 two draw threads: 31 FPS at 100% CPU (rejected).
5. v5 real `-O2` (`build_unflags = -Os`): 35 FPS.
6. v6/v7 double framebuffers: 20 / 14 FPS (rejected).
7. v5 made the shared default and re-benchmarked: 35 FPS at 76% CPU, render 21 ms, 86 KB internal free.

**Before / after** (03 benchmark, unit 1).

| Metric | v0 baseline | v5 defaults |
|---|---|---|
| Average FPS (CPU) | 30 (83-87%) | 35 (76-79%) |
| Average render / flush | 35 ms / 9 ms | 22 ms / 9 ms |
| Containers with opa_layer | 10 FPS | 21 FPS |
| Screen-sized text | 7 FPS | 11 FPS |
| Containers with opa | 39 FPS | 54 FPS |
| Widgets demo | 6 FPS | 9 FPS |
| Free internal heap at boot | 267 KB | 87 KB |

**Conclusion.** The three changes are the shared defaults. They cost ~180 KB of internal RAM, so network projects must trade some back (see [inv-tls-oom](#inv-tls-oom)).

**Follow-ups.** The full-screen flush (~20 ms CPU copy into PSRAM) is the remaining limit. Untried: `esp_async_memcpy` overlap with 2 draw buffers, esp_lvgl_port SIMD blend.

**Sources.** [hardware-log](../hardware-log.md) (full v0-v7 table); [07-gotchas](../07-gotchas.md); transcript 2026-10-10.

---

### <a id="inv-double-framebuffer"></a>Double framebuffers and two draw threads

| | |
|---|---|
| Date | 2026-10-10 |
| Area | Performance |
| Status | rejected (kept as opt-in) |
| Commit | `d17673a` (adds `04-lvgl-fb2-bench` and `Board4848S040_lvgl_fb2`) |

**Idea.** Let LVGL render straight into one of two PSRAM framebuffers and swap at vsync (`esp_lcd_panel_draw_bitmap`), removing the ~20 ms copy and any tearing.

**Result.** It works: 0 timeouts and synchronised presents (serial `fb2:` lines). But:

| Path | Average FPS | Note |
|---|---|---|
| 03 single framebuffer, v5 defaults | 35 | render into internal RAM, then copy |
| 04 DIRECT | 20 | render straight into PSRAM |
| 04 FULL | 14 | |
| v4 two draw threads | 31 (100% CPU) | thread overhead |

**Conclusion.** Rendering into PSRAM costs far more than the copy it saves, and each frame waits up to one ~24 ms refresh at 42 Hz. Two draw threads add overhead. Both rejected as defaults. esp_lvgl_port's "avoid tearing" mode is the same approach ([12](../12-esp32-performance-and-framework.md) section 5).

**Sources.** [hardware-log](../hardware-log.md); [04 README](../../projects/04-lvgl-fb2-bench/README.md).

---

### <a id="inv-restart-rtcwdt"></a>`ESP.restart()` came back as an RTC watchdog reset

| | |
|---|---|
| Date | 2026-10-10 |
| Status | open |
| Logged in | `e6338b2` (hardware log, 03 README) |

**Symptom.** Tapping a demo in the 03 launcher (which saves the choice to NVS and calls `ESP.restart()`) once rebooted with `RTCWDT_RTC_RST` instead of a software reset. The board recovered by itself.

**Hypothesis.** Restarting while the RGB panel DMA is running (suspected, not tested). Proposed fix: stop the display before restarting. Not attempted.

**Sources.** [hardware-log](../hardware-log.md) "Restart quirk"; transcript 2026-10-10.

---

### <a id="inv-tls-oom"></a><a id="inv-tls-memory"></a>`SSL - Memory allocation failed` (-32512)

| | |
|---|---|
| Date | 2026-10-10 |
| Area | Memory / network |
| Status | fixed (superseded by later RAM work) |
| Fix | `ac1ad2b` Add 05-powerwall-display ... (docs: TLS internal-RAM gotcha) |

**Symptom.** The first 05 build joined Wi-Fi, then every HTTPS connection to the gateway failed: `[start_ssl_client():302]: (-32512) SSL - Memory allocation failed`, once every ~2 s.

**Investigation.** The installed core's `sdkconfig.h` (`qio_opi`) has `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC 1` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096`. mbedTLS takes ~40-50 KB per session from internal RAM only. The build used the v5 shared defaults (IRAM fast mem) with a 48-line buffer.

**Attempt.** Remove `LV_ATTRIBUTE_FAST_MEM` from 05's `lv_conf.h` (keep 48 lines). Result: logged in.

**Before / after.**

| | Before | After |
|---|---|---|
| Free internal heap at boot | 60,768 B | 170,236 B |
| TLS handshake | -32512 every attempt | `Powerwall: logged in` |
| Polling internal free | ~58.5 KB (before Wi-Fi joined) | ~125 KB |

**Discrepancy.** The boot figure rose by ~109 KB. The 03 benchmark measured IRAM fast mem at ~55 KB (267 -> 212 KB). The transcript records no other change between the two boots. Not explained (gap).

**Follow-ups.** Later work moved most of the TLS cost to PSRAM ([inv-internal-ram](#inv-internal-ram)).

**Sources.** Transcript 2026-10-10 (serial capture and sdkconfig grep); [07-gotchas](../07-gotchas.md).

---

### <a id="inv-chunked-json"></a>Gateway JSON parsed as a number

| | |
|---|---|
| Date | 2026-10-10 |
| Status | fixed |
| Fix | `ac1ad2b` (chunked-response fix) |

**Symptom.** `/api/meters/aggregates` parsed, but every field was 0.

**Cause.** The gateway sends larger responses with `Transfer-Encoding: chunked`. `deserializeJson(http.getStream())` read the hex chunk-size line as a JSON number and stopped.

**Fix.** Read with `http.getString()`, which de-chunks (later replaced by an own de-chunker, [inv-chunked-stall](#inv-chunked-stall)). Alternative: `useHTTP10(true)`.

**Sources.** [07-gotchas](../07-gotchas.md); transcript 2026-10-10.

---

### <a id="inv-jerky-flow"></a>Jerky power-flow animation

| | |
|---|---|
| Date | 2026-10-10 |
| Status | works on hw (user: "new build is much smoother") |
| Fix | `b78e7bf` 05: Grid at bottom, Powerwall left; smoother flow (per-frame dot updates, no redundant label redraws); fps in heartbeat |

**Symptom.** "the animation feels a little jerky" (user).

**Hypotheses** (no frame counter existed yet, so none was measured first):
1. Dots moved by a 33 ms timer, so 1-2 px uneven steps at low speeds. Kept: moved to per-frame updates.
2. Labels re-set every 500 ms even when unchanged, forcing redraws. Kept: change-only setters.
3. "I can't see the frame rate". Added an FPS count to the heartbeat.

**After.** Heartbeat 27.9 FPS with live flows. No before figure (the counter arrived with the fix).

**Sources.** Transcript 2026-10-10.

---

### <a id="inv-lvgl-heap-psram"></a>Internal RAM drained by LVGL widgets

| | |
|---|---|
| Date | 2026-10-10 |
| Area | Memory |
| Status | fixed |
| Fix | `2dde03f` 05: four swipeable screens ... (LVGL heap moved to a 1 MB PSRAM pool (internal RAM 38 KB -> 140 KB free)) |

**Symptom.** With four screens built at boot, polling internal free fell to 37,764 B: too little for a TLS session.

**Cause.** `LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB`: every allocation under 4 KB (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`), which means every widget, lands in internal RAM.

**Fix.** LVGL's built-in heap with `LV_MEM_SIZE` 1 MB and `LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.

**Before / after.**

| | C heap | 1 MB PSRAM pool |
|---|---|---|
| Boot internal free | 83,160 B | 187,792 B |
| Polling internal free | 37,764 B | 140,060 B |
| Main-screen FPS | 48.8 | 46.1 |

Later screens' per-screen cost was measured at 0 B internal ([inv-internal-ram](#inv-internal-ram)). The pool sat at 13-17% used with 8-10 screens (heartbeat `lvgl pool` line).

**Sources.** Transcript 2026-10-10; [07-gotchas](../07-gotchas.md); `projects/05-powerwall-display/include/lv_conf.h`.

---

### <a id="inv-chunked-stall"></a><a id="inv-httpclient-chunked-stall"></a>Task watchdog in `HTTPClient` on a kept-alive TLS connection

| | |
|---|---|
| Dates | 2026-10-10 (watchdog); 2026-10-11 (own de-chunker) |
| Area | Network |
| Status | fixed |
| Fix | `ca8bc70` 05: apply cold-review fixes (network robustness, ...); `d3a860f` docs: gotcha for HTTPClient keep-alive chunked stall -> task watchdog; `ba7b097` 05 refactor: network side (net_http, ...) |

**Trigger.** A review found that each request built a new `HTTPClient`, so there were 3 full TLS handshakes per 2 s poll (~130,000 a day). The fix reused one kept-alive connection. After flashing it: "I've seen it reset multiple times now" (user).

**Evidence.** A capture of 14 watchdog lines:

```
E (27851) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (27851) task_wdt:  - IDLE0 (CPU 0)
E (27851) task_wdt: CPU 0: pw_gw
```

The decoded backtrace ends in `HTTPClient::getString()` -> `writeToStream()` -> `writeToStreamDataBlock()` -> `Stream::readBytes`. Resets came 19.6 s and 27.9 s after boot.

**Hypotheses.**

| Hypothesis | Verdict | Why |
|---|---|---|
| A response without a declared length, read until timeout | rejected | Every response declares its length: aggregates is chunked, the rest have `Content-Length` |
| HTTPClient's chunk reader on a reused TLS connection busy-waits in `readBytes` without yielding, starving IDLE0 | kept | Backtrace; `readBytes` loops until the read timeout |
| Task priority lets a busy-wait starve the idle task | kept | Network task above idle priority on core 0 |

**Attempts.**
1. Fetch the chunked `/aggregates` last, as HTTP/1.0 (`useHTTP10(true)`), so the body is plain and the connection closes. The small fixed-length endpoints stay on the kept-alive connection.
2. Network tasks at `tskIDLE_PRIORITY`, so any library busy-wait time-slices with IDLE0.
3. Connect 3 s / read 3 s / handshake 8 s timeouts (the default handshake timeout was 120 s).
4. 2026-10-11 refactor: `net_body.h`, an own de-chunker and read loop with a deadline and yields, host-tested by splitting real payloads at every byte offset (217 checks). Gateway keep-alive for `/aggregates` is behind `-DPW_GW_KEEPALIVE` (default 0). See [inv-tls-gateway-refusals](#inv-tls-gateway-refusals).

**Before / after.**

| | Before | After |
|---|---|---|
| Task watchdog resets | several, within ~20-30 s of boot | none in a 4-minute run; none in later soaks (`soak15`, `soak-refactor`, `soak-keepalive`: 0 `Guru`, 0 `wdt`) |
| TLS handshakes per poll | 3 (before the review fix) | 1 |
| Internal free after the fix | | polling ~133.6 KB, min ever ~112 KB |

**Sources.** Transcript 2026-10-10 (capture, addr2line output); [07-gotchas](../07-gotchas.md) last row; `ba7b097` message; `soak-keepalive.log`.

---

### <a id="inv-vpp-flag"></a>VPP event not shown

| | |
|---|---|
| Date | 2026-10-10 |
| Status | open |
| Commits | `9dbfcd3` added the indicator; no fix |

**Symptom.** "currently I am in a VPP event, but the display doesn't show it" (user).

**Diagnosis.** A direct read of the gateway during the event returned `{"grid_status":"SystemGridConnected","grid_services_active":false}`, with the battery discharging 6,099 W and the site exporting 6,693 W. The local API's flag did not reflect the event in this one observation. Other system endpoints return 404 on basic access.

**Attempt.** An inferred heuristic (battery > 1 kW discharging, more than the home load, while exporting, sustained 20 s) was started without the user asking for it, then reverted. This led to the rule to diagnose and offer options before changing code on a report.

**Follow-ups.** Options not yet chosen: an inferred indicator, a VPP test toggle, or fuller TEDAPI access.

**Sources.** Transcript 2026-10-10.

---

### <a id="inv-sd-multiblock"></a><a id="inv-sd-multiblock-write"></a><a id="inv-sd-bring-up"></a>SD large writes fail at every clock

| | |
|---|---|
| Date | 2026-10-11 |
| Area | Storage (SPI-mode microSD, Arduino core SD driver) |
| Status | works on hw |
| Fix | `8892191` sd: patched SD lib (multi-block write busy-wait fix), SdStore library, 06 serial-driven SD test bench |

**Symptom.** The new serial-driven test bench (project 06, 19 functional tests plus a 1-25 MHz clock sweep) showed failures on large writes only, intermittently. Small writes, appends and atomic files always passed.

**Hypotheses.**

| Hypothesis | Verdict | Why |
|---|---|---|
| Old card (2 GB SDSC, 2011) | partly true, not the root cause | The newer 16 GB card failed the same way before the fix |
| Clock too high | rejected | The 512 KB sweep failed at every clock including 1 MHz |
| Shared lines with the LCD init bus (MOSI/SCK on GPIO 47/48) | rejected | SD runs on its own SPI host after `initDisplay()`; failures were independent of clock |
| Supply droop (backlight load) | rejected | With the backlight off, 3 of 4 runs still failed. The first backlight-off attempt was void: the user reported "backlight has stayed on the whole time" because an upload had collided with a tool install ([inv-tooling-pitfalls](#inv-tooling-pitfalls)) |
| Driver: no busy-wait after the multi-block Stop Tran token | kept | The driver log showed an error on the next command after a large write. `sdStop()` deselected straight after the token, so the next CMD13 met a busy card |

**Fix.** In `sd_diskio.cpp` `sdStop()`, after the Stop Tran token, skip one `0xFF` byte and wait out the busy period (`sdWait`). A 3-line change in a patched copy of the core's SD library (`common/lib/SD`), which replaces the built-in one through `lib_deps`.

**Before / after** (16 GB SDHC, class 10 / U1).

| | Before | After |
|---|---|---|
| Full test runs with >= 1 failure | 7 of 10 | 0 of 4 (19/19 each) |
| 1 MB write throughput (when it passed) | 306 KB/s | 652-673 KB/s |
| 1 MB read throughput | 1,488 KB/s | 1,467-1,473 KB/s |
| Clock sweep (512 KB write+verify) | failed at 1, 4, 10 and 20 MHz in one run; 25 MHz passed once | all pass: 1 MHz W 101 / R 113 KB/s; 4 MHz W 302 / R 407; 10 MHz W 505 / R 847; 20 MHz W 650 / R 1,329; 25 MHz W 631 / R 1,326 |
| Log-line append (open, write, close) | ~17.1-17.4 ms average | ~17 ms |

The 2 GB card before the fix: 1 MB write failed in all 3 runs. The 256 KB pattern and a PC-written file read failed in run 1 and passed in runs 2-3.

**Conclusion.** An upstream bug in Arduino-ESP32 3.2.1's SD driver. One harmless `no token received` warning remains on each mount (the driver retries its first command). Hot-swap works (pull, wait, re-insert remounts without a reset). Inserting a card can briefly drop the PC's CH340 USB link.

**Follow-ups.** Re-apply the patch, or drop it, when the core is upgraded ([common/lib/SD/README-mini-screen.md](../../common/lib/SD/README-mini-screen.md)). Reporting upstream was offered and not done.

**Sources.** Transcript 2026-10-11 (sd_serial runs); [hardware-log](../hardware-log.md) "SD card" row; [06 README](../../projects/06-sd-card-test/README.md).

---

### <a id="inv-sd-card-clock"></a>Choosing the SD clock per card

| | |
|---|---|
| Date | 2026-10-11 |
| Status | fixed (self-tuning in `SdStore`) |
| Fix | `8892191` (SdStore: per-card clock tuning, retry/step-down) |

**Question.** Can the card's registers (CID/CSD/SCR/SD Status) tell us the best clock?

**Findings** (registers read with an added `sd_read_register()`):

| | 16 GB (kept) | 2 GB (retired) |
|---|---|---|
| Type, date | SDHC, July 2014 | SDSC, April 2011 |
| Rated max clock (CSD) | 25 MHz | 25 MHz |
| Speed class | Class 10, U1 | none |
| Access time / write factor | 1.0 ms / 4x | 1.5 ms / 16x |
| Result with the fixed driver | 19/19 at 20-25 MHz | 4 MHz: one run 18/19 (atomic mismatch), two runs 19/19; 10 MHz: 16/19 and 17/19 |

**Conclusion.** The rated clock is not a guide: the SD spec has cards report 25 MHz. `SdStore` instead:
- runs a 32 KB link test at mount;
- remembers each card's working clock on the card (`/.sdstore.cfg`, keyed by CID);
- retries, then steps down 25/20/10/4/1 MHz on errors.

20 MHz is the default (also ESP-IDF's SDSPI default). Self-tuning passed 2 of 2 full runs with no step-downs on the 16 GB card.

**Sources.** Transcript 2026-10-11; [hardware-log](../hardware-log.md).

---

### <a id="inv-heredoc-newlines"></a>Shell heredocs corrupt C string escapes

| | |
|---|---|
| Dates | 2026-10-10 and 2026-10-11 |
| Area | Tooling (agent editing method) |
| Status | mitigated |
| Fix | `5c61e5d` docs: wrap-up ... session lessons in CLAUDE.md (editing tip); `2c4025e` tools: lint.ps1 (detection) |

**Symptom.** Edits made with Python or shell heredocs from Git Bash turned `\n` inside C string literals into real newlines. That gave unterminated strings in `printf` format strings, a `#done` marker and a CA-certificate block. One edit script also silently matched nothing.

**Occurrences recorded.** 2026-10-10: three incidents (weather `printf`, an edit script that matched nothing, the energy fix). 2026-10-11: three incidents (06 `main.cpp` `#done`, the 05 heartbeat `printf`, and the CA cert block, where a second attempt via a quoted heredoc failed the same way: "Bash in this tool mangles `\n` even inside a quoted heredoc").

**Options considered.** A hook that blocks heredoc edits to C/C++ files (prevent), and a linter that catches unterminated literals (detect). The user preferred a real linter ([inv-lint-setup](#inv-lint-setup)).

**Conclusion.** Use the Edit/Write tools for C/C++ string edits (rule in `CLAUDE.md`). The linter and the build catch what slips through. No hook was installed.

**Sources.** Transcripts 2026-10-10 and 2026-10-11; `CLAUDE.md` (project 05 section).

---

### <a id="inv-lint-setup"></a>Making static analysis actually analyse

| | |
|---|---|
| Date | 2026-10-11 |
| Status | fixed |
| Fix | `2c4025e` tools: lint.ps1 (cppcheck via pio check + clang-tidy on a translated compile database) |

**Symptom.** The first `pio check` setup reported mostly library noise. clang-tidy could not find `Arduino.h`, so "clean" really meant "cppcheck only".

**Attempts.**
1. Narrow clang-tidy's `HeaderFilterRegex` to `projects/<name>/(src|include)` and `common/lib`: library lines remained (285 from cppcheck).
2. `cppcheck -i.pio`: no effect.
3. `--suppress=*:*libdeps*` plus style suppressions: raw findings on 05 fell from 357 to 3 (all from `secrets.h`, which `lint.ps1` never prints).
4. clang-tidy with framework packages (`check_skip_packages = no`): over 12 minutes on project 01; killed.
5. Final approach: `pio run -t compiledb` into an isolated build dir. The database is cut down to our files and the flags translated for clang (`--target=i686-none-elf`, Xtensa defines, toolchain headers as `-isystem`). clang-tidy 18.1.8 then runs on our files only.

**Before / after.**

| | Before | After |
|---|---|---|
| Raw `pio check` lines on 05 | 357 (336 from `.pio/libdeps`) | 3 |
| clang-tidy coverage | stopped at `Arduino.h` | real analysis of our sources |
| Run time | `pio check` ~25 s; full packages > 12 min | `lint.ps1` ~36 s on 05, ~16 s on 01 (tightened `pio check` version) |

**Real findings it produced.** A struct compared with `memcmp`, `bugprone-branch-clone` in 01 and in the aurora screen, `bugprone-signed-char-misuse` on `int8_t` percentages, and an uninitialised `Stream::_startMillis` in the core (NOLINTed).

**Side lesson.** A lint agent killed every `pio` and `python` process while cleaning up a hung run. Agents now stop only processes they started. A `pio check` of 05 hung for about 3 hours with no CPU use and had to be killed ([inv-tooling-pitfalls](#inv-tooling-pitfalls)).

**Sources.** Transcript 2026-10-11 (agent reports); `tools/lint.ps1` header; `.clang-tidy`.

---

### <a id="inv-brightness-range"></a>Brightness slider makes little difference

| | |
|---|---|
| Date | 2026-10-11 |
| Status | diagnosed; A and B implemented (builds), hardware sweep pending |
| Commit | `14a049f` (docs: backlight range options A/B) |

**Symptom.** "the brightness slider almost makes no difference" (user).

**Hypotheses.**
1. Narrow range: duty 100-255 is only 2.5x, and perceived brightness is roughly logarithmic.
2. 100 kHz is too fast for the boost driver's EN-pin dimming. Each period is 10 µs, so low and mid duties are compressed. This fits the cut-out at ~100 at 100 kHz against the community's ~60 at a lower frequency (inferred).

**Plan** ([02-pinout](../02-pinout.md), "Backlight dimming range"):
- A. A serial-driven sweep of frequency (100/50/25/20 kHz) and duty, with the user watching and listening. Pick the lowest silent frequency, then re-measure the minimum duty.
- B. A perceptual (gamma) curve on the slider.
- The hardware mod only if A falls short.

**Implemented (builds; not flashed, nothing measured yet).**
- Board library: `B4848_LCD_BL_FREQ_HZ` (default stays 100 kHz), `setBacklightFrequency()` retunes at run time and picks the highest duty resolution (9 bits at 100 kHz, 11 bits at 20-25 kHz), plus `setBacklightDuty/Level()` and getters. `backlightMinDuty()` scales `B4848_LCD_BL_MIN` as a fraction of the period.
- Project 05: slider 0-100 % through `duty = min + (max - min) * (pct/100)^2.2` (`backlight_curve.h`, host-tested); 0 % is min-but-on. Settings store the percentage (`SET2`); old raw-duty files (`SET1`) are converted on load.
- Serial `bf <hz>`, `bd <duty>`, `br`, `bs [steps ms from to]` for the sweep; procedure in [02-pinout](../02-pinout.md).
- Design note (inferred): the sweep must start below the assumed minimum, because the cut-out at other frequencies may be lower than 100/255; that is why `bs` defaults to `from` = 0.

**Sources.** Transcript 2026-10-11; [02-pinout](../02-pinout.md).

---

### <a id="inv-internal-ram"></a><a id="inv-internal-ram-budget"></a>Internal RAM low point below 100 KB

| | |
|---|---|
| Date | 2026-10-11 |
| Area | Memory |
| Status | fixed (then partly traded back for [inv-crackle](#inv-crackle)) |
| Fix | `174ae0b` 05: boot heap trace per step and TLS low-water per job; ...; `cca53c5` 05: sun and El Nino screens; RAM fixes (single net task, mbedTLS + JSON in PSRAM, no Wi-Fi NVS writes) |

**Symptom.** After adding moon, aurora, settings and about screens plus SD: steady internal free ~121 KB (down from ~161 KB), min ever ~101.5 KB, largest block ~41 KB (transcript 2026-10-11). The project rule is >= ~100 KB.

**Method.** A permanent boot trace (`[mem] <step> internal free N (+delta)`) after each setup step. A per-job TLS low-water line (`[tls] job free before N, min ever A -> B`). One A/B build with the SD task disabled.

**Findings.**

| Step (boot trace) | Delta |
|---|---|
| display (incl. 480x10 bounce buffers) | -20,376 B |
| SD task (6 KB stack) | -6,952 B |
| LVGL (48-line buffer etc.) | -48,268 B |
| moon / aurora / settings / about screens | 0 B each (widgets live in the PSRAM pool) |
| two network tasks (`pw_gw` 12 KB + `pw_wx` 10 KB stacks, Wi-Fi start) | -66,572 B |

| | With SD | Without SD |
|---|---|---|
| Free after setup | 163,436 B | 171,856 B |
| Idle | ~160 KB | ~168 KB |
| Gateway TLS open | ~120 KB | ~117 KB |
| Min ever | 92,136 B | 110,068 B |
| Largest block | 44,020 B | 51,188-67,572 B |

- The "121 KB" was simply the reading while the gateway TLS session was open (~40 KB).
- The low point came from the Open-Meteo handshake (`[tls] job free before 161096, min ever 116252 -> 92136`), about 69 KB at peak, while SD buffers were also in use.
- The per-screen negative deltas during boot came from the SD task mounting in parallel, not from the screens (0 B in the no-SD build).

**Research input** ([12](../12-esp32-performance-and-framework.md)):
- The core forces mbedTLS to internal RAM (`MBEDTLS_INTERNAL_MEM_ALLOC`, 16 KB in + 16 KB out).
- `mbedtls_platform_set_calloc_free()` is exported, so it can be overridden at runtime.
- `WiFi.persistent()` defaults to true, which writes NVS (breaks the no-flash-writes rule; [inv-minor](#inv-wifi-nvs-writes)).

**Fixes** (`cca53c5`):
1. `WiFi.persistent(false)`.
2. mbedTLS calloc hook: blocks >= 1 KB to PSRAM, with fallback either way.
3. All `JsonDocument`s on a PSRAM allocator.
4. The weather task folded into one network task (one TLS session at a time by construction).

**Before / after.**

| | Before (`14a049f` + trace) | After, 1 KB threshold | After `300e7b5` (4 KB threshold, 2x bounce) |
|---|---|---|---|
| Min ever internal | 92,136 B | 146,116 B (137,516 B with the 16 KB loop stack) | 118,096 B (118,052 B over 15 min) |
| Largest internal block | 44,020 B | 96,244 B (88,052 B) | 63,476 B (59,380-75,764 B over 15 min) |
| Network task step in the boot trace | -66,572 B (2 tasks) | -55,584 B (1 task) | -55,528 B |

**Disagreement.** The `cca53c5` message says "min internal free during TLS 92 KB -> ~140-155 KB, largest block 44 -> ~90 KB". The captured min-ever values are 146,116 B and 137,516 B. The "~140-155" range matches neither exactly (probably a mix of min-ever and per-job figures; inferred).

**Follow-ups.** The 1 KB threshold was traded back to 4 KB for [inv-crackle](#inv-crackle). The decision trigger for leaving plain Arduino is in [12](../12-esp32-performance-and-framework.md) section 5: low point < ~80 KB or largest block < ~25 KB. Neither has been reached.

**Sources.** Transcript 2026-10-11 (serial captures); `cca53c5` message; [12-esp32-performance-and-framework](../12-esp32-performance-and-framework.md).

---

### <a id="inv-loop-stack-overflow"></a>Core 1 panic: LVGL task stack nearly full

| | |
|---|---|
| Date | 2026-10-11 |
| Status | fixed |
| Fix | `cca53c5` (loopTask stack 16 KB (8 KB had <1 KB left); LVGL's own JPEG decoder unregistered) |

**Symptom.** About 2 minutes into the first run with the sun and ENSO screens: `Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception)`.

**Evidence.** addr2line: `is_jpg` (`lv_tjpgd.c:305`) <- `decoder_info` <- `lv_image_decoder_open` <- `lv_draw_image_normal_helper` <- ... <- `lv_label_event` <- `lv_obj_redraw`. Enabling `LV_USE_TJPGD` (for `space.cpp`'s own sun-image decode) had also registered LVGL's JPEG decoder, which probes every image it draws. A new heartbeat field showed the loopTask (LVGL) stack high-water mark at 1,432 B, then 856 B and 824 B free out of 8 KB.

**Hypothesis.** A stack overflow in loopTask during a deep redraw call chain. The "Unhandled debug exception" fits the stack-end watchpoint (inferred; no "stack overflow" text was printed).

**Fixes.**
1. `lv_tjpgd_deinit()` after `lv_init()`: LVGL no longer sniffs images for JPEG.
2. `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`.

**Before / after.**

| | Before | After |
|---|---|---|
| loopTask stack free (high-water) | 1,432 -> 856 -> 824 B (8 KB stack) | 9,624-9,656 B (16 KB stack) |
| Panics | 1 in ~2 min | none in the following soaks (0 `Guru` in all five soak logs) |
| Internal RAM cost | | +8 KB stack; min ever 146,116 -> 137,516 B |

**Sources.** Transcript 2026-10-11; `soak*.log` (`stack free ... lvgl` field).

---

### <a id="inv-boot-race-lock"></a>SD task used a mutex before it existed

| | |
|---|---|
| Date | 2026-10-11 |
| Status | fixed |
| Fix | `cca53c5` (fix: powerwallData() lock created at static init (SD task could call it first)); made structural in `9c817a2` (`shared_state.h`: `Mutex` / `Shared<T>` created at static init) |

**Symptom.** A second crash: `__assert_func` in `xQueueSemaphoreTake` (`queue.c:1713`) <- `powerwallData()` (`powerwall.cpp:459`) <- `sampleMinute()` (`history.cpp:156`) <- `sdTask`.

**Cause.** The SD task starts right after `initDisplay()` and can sample `powerwallData()` before `powerwallStart()` had created the mutex, so it took a null handle.

**Fix.** Create the lock at static initialisation (`SemaphoreHandle_t s_lock = xSemaphoreCreateMutex();`). The refactor's `Shared<T>` keeps that guarantee for all shared state.

**Sources.** Transcript 2026-10-11 (decoded backtrace).

---

### <a id="inv-crackle"></a><a id="inv-crackle-psram"></a>Panel "crackle" after the mbedTLS-to-PSRAM change

| | |
|---|---|
| Date | 2026-10-11 |
| Area | Display / memory |
| Status | works on hw: confirmed visually by the owner, 2026-10-11 ("screen looks good now") after the mitigations (2x bounce buffers 480x20, mbedTLS PSRAM threshold 4 KB, rotation-3 flush path, gateway keep-alive) |
| Fix | `300e7b5` 05: crackle fixes (2x bounce buffers, mbedTLS PSRAM threshold 4 KB), slower ENSO ticker, stutter diagnostics |

**Symptom.** "starting to see some strangle crackling on the main screen now it's been running for a bit", then "once it starts it becomes a constant crackle" (user). The report came right after `cca53c5` was deployed.

**Hypotheses.**

| Hypothesis | Verdict | Why |
|---|---|---|
| The RGB DMA underruns while PSRAM is busy; the 1 KB mbedTLS threshold put the per-poll handshake maths in PSRAM every 2 s | main suspect | Timing matched the change; constant once started; 30 handshakes/min from gateway polling |
| The hourly sun-image JPEG decode (~0.7-0.85 s, PSRAM accumulators) | minor | Once an hour only |
| Steady redraw load (60 fps chevrons, icon loops, ENSO ticker) | contributing (inferred) | Raised by the consolidation review ([inv-redraw-load](#inv-redraw-load)) |
| Rotated flush write-back (~460 KB cache write-back per 48-line strip) | contributing (inferred) | Found during the stutter work ([inv-stutter-rotated-flush](#inv-stutter-rotated-flush)) |

**Options offered.**
1. Only blocks >= 4 KB to PSRAM (mainly the two 16 KB record buffers).
2. Larger bounce buffers.
3. Undo the hook completely.

The user chose 1 + 2.

**Before / after.**

| | 1 KB threshold, 480x10 bounce (`cca53c5`) | 4 KB threshold, 480x20 bounce (`300e7b5`) |
|---|---|---|
| `[mem] display` boot step | -20,376 B | -39,880 B (bounce cost +19,504 B) |
| Polling internal free | ~156 KB | ~136 KB |
| Min ever internal | 137,516 B | 118,096 B |
| Largest block | 88,052 B | 63,476 B |
| Crackle | reported, constant | gone; confirmed visually by the owner, 2026-10-11 |

The drop in min ever (19,420 B) is almost exactly the bounce-buffer cost (19,504 B). This suggests the threshold change itself cost little at the low point (inferred).

**Follow-ups.** Confirmed gone by the owner on 2026-10-11 with all mitigations in place; which single change did the most is not isolated. A long soak (several hours) for leaks, fragmentation and reconnects is still planned ([projects/README.md](../../projects/README.md#planned)). Untried options from [12](../12-esp32-performance-and-framework.md): 64-byte data-cache line, `SPIRAM_XIP_FROM_PSRAM` (both need a custom sdkconfig), and lowering PCLK around heavy PSRAM work.

**Sources.** Transcript 2026-10-11; [hardware-log](../hardware-log.md) "Rotated flush" row (last sentence); `300e7b5` message.

---

### <a id="inv-stutter-rotated-flush"></a><a id="inv-rotated-flush"></a>~300 ms stutters on the main screen

| | |
|---|---|
| Date | 2026-10-11 |
| Area | Display / performance |
| Status | works on hw: owner, 2026-10-11: stutter "a lot less noticeable". Not fully gone: ~135-190 ms frames remain when all power-flow labels and the top-bar Wi-Fi label redraw together; the Wi-Fi label is now throttled (see below), the rest is power-flow labels + chevrons |
| Fix | `bd3f28d` Board4848S040: rotation-3 LVGL flush writes framebuffer rows contiguously and writes back only what changed; diagnostics in `300e7b5`; docs in `d6f1b22` |

**Symptom.** "now it seems to stutter every once in a while on the main screen" (user).

**Method** (instrumentation in `300e7b5`, all serial):
- `[stall] N ms`: a gap > 120 ms between rendered frames;
- `[slow] lv_timer_handler N ms (net P)`: with the network task's phase (0 idle, 1 gateway poll, 2 background job);
- `[slow] refresh` (screen refresh callback > 50 ms);
- `[slow] render N ms: A areas, P px, box ...`: the frame's invalidated-area summary;
- later `[slow] first areas: ...`: the first 16 areas of a slow frame.

**What the logs showed** (`soak15.log`, 14.9 min, before the fix):
- `[slow] render 296 ms: 81 areas, 133159 px, box 0,0-474,427` and `render 293 ms: 103 areas, 146787 px`, each with `[stall] 307/312 ms`. The handler was slow (`net 1`, gateway poll in progress), but the refresh callback was not slow: the time was in rendering and flushing.
- The first full-screen frame: `render 255 ms: 52 areas, 1307956 px`.
- A redraw of ~130-150k px taking ~300 ms is far slower than the 03 benchmark's numbers. That pointed at the flush, not at drawing.

**Root cause** (code reading, Arduino_GFX 1.6.9 `Arduino_RGB_Display::draw16bitRGBBitmap`). With rotation 3 (USB-C at the bottom):
- `gfx_draw_bitmap_to_framebuffer_rotate_3` writes each LVGL strip column-wise, so every pixel lands on a different 960-byte framebuffer row (cache thrash);
- `_auto_flush` then calls `Cache_WriteBack_Addr` over `_fb_width * w * 2` bytes: every full framebuffer row the strip touches. For a 480x48 strip that changed 46 KB, ~460 KB is written back. A 14 px chevron gives 13 KB.

**Fix** (in the shared board library, so all rotation-3 projects benefit): `flushRot3()` writes each framebuffer row's segment contiguously and calls `Cache_WriteBack_Addr` on exactly that segment (`h * 2` bytes). Other rotations still use Arduino_GFX.

**Attempts in order.**
1. Flash the fix and soak 10 min (`soak-flush.log`). **Void**: the upload failed with `FileExistsError` (a concurrent tool install), so the board ran the old firmware. The log matches the baseline: renders 292-309 ms, stalls 307-343 ms.
2. Re-flash and soak 10 min (`soak-flush2.log`): renders 142-144 ms.
3. The consolidation refactor on top (`soak-refactor.log`) and the keep-alive trial (`soak-keepalive.log`).

**Before / after** (all from soak logs).

| | Before (`soak15.log`; void `soak-flush.log`) | After flush fix (`soak-flush2.log`) | After refactor (`soak-refactor.log`) | Keep-alive trial (`soak-keepalive.log`) |
|---|---|---|---|---|
| First full-screen render | 255 ms (52 areas, 1,307,956 px); 254 ms | 142 ms (same areas, same px) | 178 ms (132 areas, 1,607,474 px: different frame, not comparable) | 174 ms (132 areas, 1,607,474 px) |
| Many-area redraws (~100-150k px) | 293-296 ms; 292-309 ms | 144 ms (112 areas, 101,559 px) | 164-196 ms (80-106 areas, 114,781-137,759 px) | 134-189 ms (81-121 areas, 111,317-141,386 px) |
| Frame gaps `[stall]` after boot | 307, 312 ms (14.9 min); 307-343 ms x5 (9.85 min) | 162 ms x1 (9.9 min) | 213-226 ms x5 (9.9 min) | 136-251 ms x4 (9.85 min) |
| Main-screen FPS (median) | 47.6 | 41.6 | 37.8 | 55.0 |

**Disagreement.** [hardware-log](../hardware-log.md) records "~300 ms many-area redraws -> ~145-185 ms". The logs give 144 ms right after the fix, then 134-196 ms (renders) and 136-251 ms (stall gaps) in later builds. The "-> ~145-215 ms" summary used elsewhere is consistent with the stall gaps of the refactor build.

**Conclusion.** Arduino_GFX's rotated draw cost about half the time of the slow frames. The remaining 130-200 ms frames are many small areas: chevrons, the top bar and the weather icon in the same frame. The slow frames coincide with gateway polls (`net 1`) more often than not. Whether PSRAM contention from TLS lengthens them is not isolated (inferred).

**03 benchmark A/B** (2026-10-11, unit 1, v5 defaults; envs `bench` = rotation 0, `bench-rot3` = rotation 3 with `flushRot3()`, `bench-rot3-gfx` = rotation 3 with `-DB4848_ROT3_FAST_FLUSH=0`, i.e. Arduino_GFX's rotated draw). FPS [render ms / flush ms]:

| Scene | rotation 0 | rotation 3, own flush | rotation 3, Arduino_GFX |
|---|---|---|---|
| Empty screen (full-screen flush) | 25 [10/20] | 21 [10/27] | 7 [9/110] |
| Moving wallpaper | 17 [30/20] | 14 [34/27] | 5 [31/152] |
| Screen sized text | 11 [60/20] | 10 [60/27] | 5 [61/123] |
| Containers with scrolling | 15 [41/20] | 13 [40/28] | 5 [40/144] |
| Multiple labels (small areas) | 32 [18/7] | 27 [19/11] | 28 [20/9] |
| Widgets demo | 9 [57/17] | 8 [57/27] | 4 [57/122] |
| **All scenes avg.** | **35 [22/9]** | **33 [23/13]** | **29 [22/49]** |

Reading: render time is the same in all three, so the difference is the flush. A full-screen flush costs 20 ms at rotation 0, 27 ms with `flushRot3()` and 110-152 ms with Arduino_GFX's rotated draw (4-5x). Small, scattered areas cost about the same either way (flush 9-11 ms on "Multiple labels"): there the per-segment write-back dominates. The remaining ~7 ms gap to rotation 0 is the transposed (strided) read of the draw buffer. `flushRot3()` stays the default; `B4848_ROT3_FAST_FLUSH=0` exists only for this A/B.

**Wi-Fi label throttle** (2026-10-11): `ui::topBarUpdate` now changes the RSSI text only when it moved by >= 3 dB, at most every 10 s. 10-minute soak (`soak-wifithrottle.log`) against the keep-alive soak:

| | Keep-alive soak (before) | Throttle soak (after) |
|---|---|---|
| Power flows at the time | battery idle at 100%, grid near 0 (1-2 flows animating) | battery discharging ~10.5 kW, exporting ~7.4 kW (all 4 flows animating) |
| Slow frames after boot (`[slow] render`) | 3 | 35 |
| Of those, with the Wi-Fi label in the first areas | 2 | 4 |
| Render time of slow frames | 134-189 ms | 143-232 ms (most 150-160 ms) |
| Main-screen FPS (last heartbeat) | 59.4 | 40.6 |

Reading: the throttle does what it should (the Wi-Fi label is rarely in a slow frame now), but the two soaks ran under very different loads, so the totals are not comparable. The slow frames are ~75-95 areas of ~100-150k px: the six power-flow value labels plus the moving chevrons, each chevron invalidated twice (old and new position). ~150 ms for ~100k px is far above the benchmark's cost per pixel, so the time is per-area overhead (each area re-renders every object under it: cards, borders, radius) rather than pixels (inferred).

**Follow-ups.** Reduce the number of invalidated areas per frame ([inv-redraw-load](#inv-redraw-load)): fewer chevron invalidations (e.g. one parent area per flow line), and spreading the label updates of one poll over several frames. Not started; waiting for the owner's choice.

**Sources.** `soak15.log`, `soak-flush.log`, `soak-flush2.log`, `soak-refactor.log`, `soak-keepalive.log`; transcript 2026-10-11; `bd3f28d` message; [hardware-log](../hardware-log.md) "Rotated flush".

---

### <a id="inv-redraw-load"></a>Constant redraw load from animations

| | |
|---|---|
| Date | 2026-10-11 |
| Status | mitigated (changes made; effect not isolated). Owner, 2026-10-11: the refactor's visual changes (health sheen without `clip_corner`, sun image hard edge, 58-bar wind strip, sun chip label layout) look good on the device |
| Commits | `300e7b5` (ENSO ticker 40 -> 20 px/s); `1f816a8` 05 refactor: UI core helpers, BarChart, screen registry, serial commands; `775b417`, `0042c90` (screens) |

**Findings** (consolidation review, read-only; marked "(bench)" where it needed measuring):
- The house screen moves up to 18 chevron `lv_line`s from a 16 ms timer. Each one invalidates 2 small areas per frame: up to ~36 render+flush calls per frame at 60 fps.
- The weather icon loop and the status/VPP pulse dots redraw every frame.
- The ENSO synopsis (~160 characters in 350 px) always scrolls (`LONG_SCROLL_CIRCULAR`). The user also asked for it slower.
- `onShow` fades started together with the 300 ms slide, so whole cards re-rendered on top of two full-screen renders per frame.
- `clip_corner` layers on the sun holder and the health bar sheen allocate corner layers on every redraw.
- Rows of `lv_obj` bars: about 120 objects across the four bar strips.

**Changes.**
- ENSO ticker at `lv_anim_speed(20)`.
- Fades start after the slide (`-DUI_FADE_AFTER_SLIDE`, default 1).
- `BarChart`: one object per strip; 120 objects -> 4.
- `clip_corner` removed (the sun image corners are painted in the background colour).
- `CHEVRON_TICK_MS` constant (still 16) so a 33 ms tick can be benchmarked.

**Measurement.** A new heartbeat metric `redraw: <px>/s, <n> flushes/s` exists from `1f816a8` onwards. It has no before value. On the main screen it read 260,075-304,846 px/s and 234-256 flushes/s (`soak-refactor.log`), and up to 389,540 px/s and 333 flushes/s (`soak-keepalive.log`; depends on live flows, inferred).

**Follow-ups.** Try 33 ms chevrons or one draw object per flow path, and compare FPS, px/s and flushes/s. The remaining ~135-190 ms frames (owner: stutter "a lot less noticeable", 2026-10-11) occur when all power-flow labels and the top-bar Wi-Fi label redraw together; the Wi-Fi label is now throttled, see [inv-stutter-rotated-flush](#inv-stutter-rotated-flush) for the soak.

**Sources.** Consolidation plan (session notes, 2026-10-11); `soak-refactor.log`; `soak-keepalive.log`; [05 README](../../projects/05-powerwall-display/README.md) (serial commands, heartbeat).

---

### <a id="inv-job-budget"></a>Long background jobs made live data stale

| | |
|---|---|
| Date | 2026-10-11 |
| Status | fixed |
| Fix | `ba7b097` (Job table with names in the [tls] line; every job runs under PW_JOB_BUDGET_MS (12 s) so live data no longer goes stale during long jobs (B1)) |

**Finding** (code review, not seen on screen). With one network task, a background job blocks gateway polling. The sun-image download had a 25 s deadline plus connect and handshake time. ENSO made 5 requests, each with 5 s connect, 5 s handshake and 6 s read plus a retry. Both can exceed `PW_STALE_MS` (20 s), so the live values would turn "--" and the energy totals would pause.

**Fix.** Every job runs under a 12 s session budget (`net::Options::budgetMs`). A `static_assert` keeps the budget under the stale limit. Skipped files are retried next period, and ENSO merges partial results.

**Measurement.** None on hardware. No `time budget (N ms) spent` line appears in `soak-refactor.log` or `soak-keepalive.log`, so the budget never had to cut a job in those runs.

**Sources.** Consolidation plan (session notes, B1); `ba7b097` message.

---

### <a id="inv-shared-modules"></a>Consolidation round: duplicated HTTP and UI code

| | |
|---|---|
| Date | 2026-10-11 |
| Status | done |
| Commits | `9c817a2` 05 refactor: shared contracts (shared_state, mem_util, time_util, net_http, ui_chart, ui_common helpers); `ba7b097`; `1f816a8`; `775b417`; `0042c90`; `496462e` 05 refactor: integration ... |

**Problem.** Four copies of the HTTPS client code with three different body readers, five lock idioms, two `daysFromCivil`, two ISO parsers and three month tables. Plus per-screen top-bar boilerplate and hex colour literals. Several files had been written by different agents in parallel.

**Method.** A read-only review produced a plan with file:line evidence. The shared headers were written first, then 4 agents worked in parallel on disjoint files, each in its own git worktree and build directory, and the result was integrated centrally.

**Before / after.**

| | Before | After |
|---|---|---|
| Network-side files (13 files) | 3,228 lines | 2,993 lines (+ `net_http.cpp` 237, `net_body.h` 227, `time_util.cpp` 136) |
| Firmware flash | 2,263,562 B | 2,250,190 B (-13.4 KB); "~2.25 MB, down 14 KB" after integration |
| Static RAM | 54,184 B | 54,232 B |
| Host tests | 0 | 217 passing (de-chunker at every split, read loop with a fake socket and clock, date parsers, ENSO parsers on real payloads) |
| `getString()` uses on the gateway | yes | none |

**Behaviour changes to note.**
- Weather is fetched over HTTP/1.0.
- The sun-image download is limited by the 12 s budget.
- The sun wind strip shows 58 bars instead of 60 (even spacing).
- Unknown values (not stale) show "--" in grey.

**Sources.** `ba7b097` and `496462e` messages; transcript 2026-10-11 (agent reports).

---

### <a id="inv-tls-gateway-refusals"></a>Gateway refuses TLS handshakes

| | |
|---|---|
| Date | 2026-10-11 |
| Area | Network |
| Status | open: the keep-alive trial looks better; not yet the default |
| Commits | `ba7b097` (`PW_GW_KEEPALIVE`, default 0; TLS connect counter); `496462e` (net stats in heartbeat) |

**Symptom.** The soaks log failed handshakes: `(-30592) SSL - A fatal alert message was received from our peer` and occasionally `(-28160) SSL - The handshake negotiation failed`. The background-fetch failure counter stayed at 0, so these were gateway polls (transcript 2026-10-11).

**Counts** (`start_ssl_client: connect failed` lines over the whole log):

| Log (build) | Duration | Failures | -30592 | -28160 | TLS connects | Requests |
|---|---|---|---|---|---|---|
| `soak15.log` (`300e7b5`) | 14.9 min | 4 | 3 | 1 | n/r (no counter yet) | n/r |
| `soak-flush.log` (old firmware, see above) | 9.85 min | 1 | 1 | 0 | n/r | n/r |
| `soak-flush2.log` (`bd3f28d`) | 9.9 min | 17 | 12 | 5 | n/r | n/r |
| `soak-refactor.log` (`496462e`, keep-alive off) | 9.86 min | 19 | 19 | 0 | 275 (27.9/min) | 722 |
| `soak-keepalive.log` (`-DPW_GW_KEEPALIVE=1`) | 9.85 min | 1 (at 23.6 s) | 1 | 0 | 22 (2.2/min) | 857 |

The transcript's own count, made with a narrower pattern (`fatal alert` only; refactor log cut at 401 s), was 3 / 1 / 12 / 16.

**Hypotheses.**

| Hypothesis | Verdict | Why |
|---|---|---|
| The refactor broke the gateway client | rejected | `soak-flush2` (pre-refactor) already had 17 |
| The gateway limits or refuses frequent handshakes (a new handshake every 2 s, ~30/min) | likely (inferred) | Keep-alive cut connects by 92% and failures from 19 to 1. Gateway-side behaviour is not observable |
| Memory pressure on our side | unlikely (inferred) | Errors are alerts from the peer, not allocation failures; internal min ever was 115-118 KB |

**Keep-alive trial** (`/aggregates` read with the own de-chunker on a kept-alive connection):

| | Keep-alive off (`soak-refactor`) | Keep-alive on (`soak-keepalive`) |
|---|---|---|
| TLS connects per minute | 27.9 | 2.2 |
| Handshake failures | 19 | 1 |
| Requests (fetch failures) | 722 (0) | 857 (0) |
| Min ever internal | 115,104 B | 105,400 B |
| Largest block | 59,380-71,668 B | 55,284-60,404 B |
| Task watchdog / panic | 0 / 0 | 0 / 0 |

**Open.** The keep-alive build's low point is ~10 KB lower: the first weather job dropped min ever from 132,780 to 105,400 B. That is still above 100 KB. The soak ran for 10 minutes against the ≥ 24 h the research doc asks for. One `aggregates HTTP 400` appeared in each of the refactor and keep-alive logs (at 40.5 s and 220.7 s); its cause was not investigated.

**Sources.** `soak*.log`; transcript 2026-10-11.

---

### <a id="inv-barchart-empty"></a>Aurora and sun bar strips empty

| | |
|---|---|
| Date | 2026-10-11 |
| Status | fixed |
| Fix | `496462e` (BarChart newest-bars used lv_obj_get_height before the first layout (aurora/sun strips were empty)) |

**How it was found.** The first full set of serial screenshots: `x` command plus `tools-dev/screendump.py --all` (10 screens, 480x480, 0 missing rows, CRC ok). The aurora activity strip and the sun wind strip were blank.

**Cause.** `barChartSetNewest()` computed bar tops from `lv_obj_get_height(chart)`. That is 0 until LVGL's first layout pass, so every bar got a zero or negative height and was hidden.

**Fix.** Store the chart height at creation.

The same screenshot pass fixed text fit (the "Geomagnetic" chip, the health lifetime line) and a sun-flare value of `A0.0` ([inv-minor](#inv-swpc-a00)).

**Sources.** Transcript 2026-10-11; `496462e` message; [05 README](../../projects/05-powerwall-display/README.md) serial commands.

---

### <a id="inv-tooling-pitfalls"></a>Measurements spoiled by tooling

| | |
|---|---|
| Date | 2026-10-11 |
| Status | documented |

| Pitfall | What happened | Lesson |
|---|---|---|
| <a id="inv-upload-race"></a>Upload collided with a concurrent tool install | `FileExistsError` during `pio run -t upload` while another agent's `pio check` installed `tool-cppcheck`. Twice the board kept the old firmware: the first SD backlight-off run ("backlight has stayed on the whole time") and the first rotated-flush soak (`soak-flush.log`) | Check the upload result before trusting a measurement; don't let agents install tools while the main session flashes |
| <a id="inv-serial-dtr-reset"></a>Opening the port reset the board | pyserial asserts DTR/RTS on open, and the CH340 auto-reset circuit reset the ESP32, so `screendump.py` timed out | Set `dtr = False; rts = False` on an unopened `Serial()` object, then `open()` |
| <a id="inv-pio-check-hang"></a>`pio check` hung | A `pio check` of 05 sat for about 3 hours with no CPU activity; it was found in the process list and killed | Look for stray `pio`/`python` processes before blaming the build |
| Agent killed shared processes | A lint agent stopped every `pio`/`python` process while cleaning up | Agents stop only the processes they started |
| Capture script lost a benchmark | The first 03 benchmark log was lost to a text-encoding error in the capture script on Windows | Write captured lines to the file as they arrive |

**Sources.** Transcripts 2026-10-10 and 2026-10-11.

---

### <a id="inv-minor"></a>Smaller findings

| Anchor | Date | Finding | Evidence | Fix (commit) | Status |
|---|---|---|---|---|---|
| <a id="inv-wifi-5ghz"></a>inv-wifi-5ghz | 2026-10-10 | `WiFiMulti: no matching wifi found` although the SSIDs exist | A scan showed only one of the configured networks at 2.4 GHz; the ESP32-S3 has no 5 GHz | Scan printout when no network is found (`ac1ad2b`); [07-gotchas](../07-gotchas.md) | fixed |
| <a id="inv-url-truncation"></a>inv-url-truncation | 2026-10-10 | `Weather: HTTP 400` | The `snprintf` buffer truncated the Open-Meteo query string | Bigger buffer (`2dde03f`); later 512 B | fixed |
| <a id="inv-multi-env-upload"></a>inv-multi-env-upload | 2026-10-10 | Uploading 03/04 flashed every env in turn; the last one won | Observed during the benchmark runs | `default_envs` (`ca34265`) | fixed |
| <a id="inv-icon-taps"></a>inv-icon-taps | 2026-10-10 | Tapping icon parts did not change brightness | Review: `lv_obj_create` children are `CLICKABLE` by default in LVGL 9.5 | `inert()` helper (`ca8bc70`) | fixed |
| <a id="inv-soc-scaling"></a>inv-soc-scaling | 2026-10-10 | Battery % may read up to 5% higher than the Tesla app | Review: the app shows `(raw - 5) / 0.95` | App-matching scaling, `-DPW_SOC_APP_SCALE=0` to disable (`ca8bc70`) | fixed; not compared with the app near empty |
| <a id="inv-millis-wrap-login"></a>inv-millis-wrap-login | 2026-10-11 | After ~25 days of uptime a login retry could be blocked for up to ~25 more days | Lint agent's code reading (signed/unsigned `millis()` comparison) | Fixed by the 05 fixes agent (`14a049f`) | fixed (not observable in tests) |
| <a id="inv-baseline-padding"></a>inv-baseline-padding | 2026-10-11 | Restoring today's totals from SD could produce a copy that fails its own checksum | Struct assignment may not copy padding that the FNV checksum covers | `memcpy` (`14a049f`) | fixed before reaching the device |
| <a id="inv-wifi-nvs-writes"></a>inv-wifi-nvs-writes | 2026-10-11 | Wi-Fi config written to NVS (internal flash) at runtime | Research: `WiFi.persistent()` defaults to true in core 3.2.1 (`WiFiGeneric.cpp`) | `WiFi.persistent(false)` (`cca53c5`) | fixed |
| <a id="inv-swpc-a00"></a>inv-swpc-a00 | 2026-10-11 | Sun screen showed flare class `A0.0` | Interpreted as SWPC reporting zero flux during a GOES data gap (not confirmed against NOAA documentation) | `A0.0` ignored, last class kept (`496462e`) | fixed |
| <a id="inv-sun-jpeg-6"></a>inv-sun-jpeg-6 | 2026-10-11 | `Space: sun JPEG decode failed (6)` once in `soak-flush2.log` | One occurrence in 5 soaks; TJpgDec error 6 not investigated | none | open |

---

## Sources

Repository files (paths relative to this file):
- [../hardware-log.md](../hardware-log.md): verified results on unit 1, benchmark tables, SD and rotated-flush rows.
- [../07-gotchas.md](../07-gotchas.md): symptom and fix table.
- [../12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md): sdkconfig facts, RAM budget, framework options.
- [../04-display-st7701.md](../04-display-st7701.md), [../02-pinout.md](../02-pinout.md): bounce buffers, backlight plan.
- [../../common/lib/SD/README-mini-screen.md](../../common/lib/SD/README-mini-screen.md): the SD driver patch.
- Project READMEs: [05](../../projects/05-powerwall-display/README.md), [06](../../projects/06-sd-card-test/README.md), [03](../../projects/03-lvgl-demos/README.md), [04](../../projects/04-lvgl-fb2-bench/README.md).
- Commit messages (`git log`), as cited per section.
- Session serial logs (`soak15.log`, `soak-flush.log`, `soak-flush2.log`, `soak-refactor.log`, `soak-keepalive.log`) and session transcripts of 2026-10-08, 2026-10-10 and 2026-10-11. These are not committed; the figures above are copied from them.

External references:
- Espressif LCD FAQ (RGB drift, bounce buffers, PSRAM bandwidth): https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html
- ESP-IDF 5.4 RGB LCD driver (bounce buffers, cache write-back): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- ESP-IDF 5.4 mbedTLS memory: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/mbedtls.html
- ESP-IDF 5.4 external RAM (`SPIRAM_MALLOC_ALWAYSINTERNAL`): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/external-ram.html
- ESP-IDF SDSPI host (20 MHz default): https://docs.espressif.com/projects/esp-idf/en/release-v4.0/api-reference/peripherals/sdspi_host.html
- Arduino-ESP32 core (SD, HTTPClient, LEDC, WiFi sources): https://github.com/espressif/arduino-esp32
- arduino-esp32 issue #9348 (XIP/RODATA defaults): https://github.com/espressif/arduino-esp32/issues/9348
- pioarduino platform (hybrid compile, issue #335, PR #353): https://github.com/pioarduino/platform-espressif32
- Arduino_GFX (`Arduino_RGB_Display`, rotated bitmap draw): https://github.com/moononournation/Arduino_GFX
- LVGL 9 documentation (memory pool, display flush, label scroll speed): https://docs.lvgl.io/9.5/
- LVGL forum: Wi-Fi drift fixed by bounce buffers: https://forum.lvgl.io/t/lvgl-horizontal-offset-when-esp32-s3-wifi-enabled-esp-idf/21888
- ArduinoJson external RAM allocator: https://arduinojson.org/v7/how-to/use-external-ram-on-esp32/
- SD Association simplified specifications (SPI mode, busy signalling): https://www.sdcard.org/downloads/pls/
- Parallax forum on SD cards failing below their rated clock: https://forums.parallax.com/discussion/comment/1388211
- SD card CID/manufacturer IDs: https://cameramemoryspeed.com/sd-memory-card-faq/reading-sd-card-cid-serial-psn-internal-numbers
- NOAA SWPC data access: https://www.spaceweather.gov/content/data-access
- RFC 9112 (HTTP/1.1 chunked transfer coding): https://www.rfc-editor.org/rfc/rfc9112
