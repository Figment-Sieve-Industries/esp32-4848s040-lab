# Debugging and verification playbook

[Guide index](README.md) | [01 Quirks](01-quirks-and-gotchas.md) | [02 Debugging](02-debugging-playbook.md) | [03 Investigations](03-investigations.md) | [04 Patterns](04-patterns-and-decisions.md) | [05 New project](05-new-project-checklist.md) | [06 Hardware](06-hardware-and-sourcing.md) | [Credits](99-sources-and-credits.md)

The methods we actually used while bringing up the Guition ESP32-4848S040 (ESP32-S3 N16R8, 480x480 ST7701 RGB panel, GT911, microSD, CH340C) with PlatformIO/pioarduino (Arduino core 3.2.1, IDF 5.4) and LVGL 9.5. Every method below was used on this hardware or this repo during 2026-10-08 to 2026-10-11. Each one lists when to reach for it, the exact commands or files, what it told us (with numbers), the traps, and what it costs.

How to read it:

- Start with the [decision table](#symptom-to-first-method). Pick the cheapest method that can separate the likely causes.
- Investigations are written up in [03-investigations.md](03-investigations.md). Links of the form `#inv-<slug>` go to the matching entry there.
- Hardware results are only claimed where a log or the user confirmed them. Where a result is a single run, it says so.
- Run `pio` and `esptool` from PowerShell, never Git Bash (MSYS breaks ESP's `idf_tools.py`). Examples assume the repo root as the working directory and the board on `COM9`.

## Index

| ID | Method | Layer | Cost | Main tool or file |
|---|---|---|---|---|
| [D-01](#d-01-serial-heartbeat) | Serial heartbeat | firmware | very low | `main.cpp` (10 s block) |
| [D-02](#d-02-boot-memstep-trace) | Boot `memStep` trace | firmware | very low | `memStep()` in `main.cpp` |
| [D-03](#d-03-tls-per-job-low-water-lines) | `[tls]` per-job low-water lines | firmware | very low | `runTls()` in `powerwall.cpp` |
| [D-04](#d-04-stall-and-slow-render-diagnostics) | `[stall]` / `[slow]` render diagnostics | firmware | low | LVGL display events in `main.cpp` |
| [D-05](#d-05-net-phase-correlation) | Net-phase flag correlation | firmware | very low | `g_netPhase` |
| [D-06](#d-06-ab-builds) | A/B builds (change one thing) | method | medium (a flash cycle each) | temporary edit + `tools/pio.ps1` |
| [D-07](#d-07-decode-backtraces) | Decode Guru Meditation and watchdog backtraces | post-mortem | low | `xtensa-esp32s3-elf-addr2line` |
| [D-08](#d-08-timestamped-serial-capture) | Timestamped serial capture | host | low | pyserial script |
| [D-09](#d-09-serial-command-interface) | Serial command interface | firmware | low | `s n p l e b bf bd br bs y v m x c k r h` |
| [D-10](#d-10-framebuffer-screenshots) | Framebuffer screenshots over serial | host + firmware | low | `tools-dev/screendump.py` |
| [D-11](#d-11-soak-tests) | Soak tests and log summaries | method | medium (wall-clock) | capture + grep |
| [D-12](#d-12-host-tests-with-msvc) | Host tests with MSVC | host | low | `hosttest/run.ps1` |
| [D-13](#d-13-curl-and-openssl-checks) | curl and openssl checks of APIs | host | very low | `curl.exe`, `openssl s_client` |
| [D-14](#d-14-lint) | Lint (cppcheck + clang-tidy) | host | low | `tools/lint.ps1` |
| [D-15](#d-15-build-all-and-isolated-builds) | Build-all and isolated builds | host | medium | `tools/build-all.ps1`, `PLATFORMIO_BUILD_DIR` |
| [D-16](#d-16-sd-test-bench) | SD test bench | firmware + host | low | `projects/06-sd-card-test`, `sd_serial.py` |
| [D-17](#d-17-lvgl-benchmark-and-variant-builds) | LVGL benchmark env and variant builds | method | medium | `03-lvgl-demos` `bench` env |
| [D-18](#d-18-flash-backup-and-restore) | Flash backup and restore | safety | low | `tools/backup-flash.ps1` |
| [D-19](#d-19-read-the-shipped-source-and-config) | Read the shipped source and config | research | low | `~/.platformio/packages` |
| [D-20](#d-20-sub-agent-workflow) | Parallel sub-agent workflow | process | n/a | worktrees, contracts first |
| [D-21](#d-21-rgb-scan-out-phase-monitor) | RGB scan-out phase monitor and forced resync | serial | firmware (board lib) | `lcd:` heartbeat, `[lcd] phase jump`, `k` |

## Symptom to first method

| Symptom | Try first | Then | Why |
|---|---|---|---|
| Random reboot, `Rebooting...` in the log | D-07 (decode the backtrace) | D-08 (loop capture until it recurs) | The `rst:` line and the backtrace name the cause. See [#inv-task-wdt-chunked-httpclient](03-investigations.md#inv-chunked-stall) |
| `task_wdt: IDLE0 (CPU 0)` | D-07 | D-05, D-19 | Something on core 0 busy-waits without yielding. The backtrace shows where |
| Visible stutter or dropped frames | D-04 (render areas) + D-01 (`max gap`, `redraw`) | D-05, D-17 | Separates "slow render" from "frame never started". See [#inv-main-screen-stutter](03-investigations.md#inv-stutter-rotated-flush) |
| Crackle or noise on the panel after running a while | D-01 (`redraw` px/s, `flushes/s`) | D-02, D-03, D-06 | Suspect PSRAM bandwidth. Check what else touches PSRAM (TLS buffers, JPEG, big redraws). See [#inv-display-crackle](03-investigations.md#inv-crackle) |
| Whole-screen flicker while drawing | D-06 (bounce buffer size) | D-17 | The RGB DMA is starved. See [#inv-rgb-flicker-bounce-buffers](03-investigations.md#inv-flicker-bounce) |
| Internal RAM low | D-02 (where it goes at boot) | D-03 (the low point), D-06 (attribute to a subsystem) | See [#inv-internal-ram-low-water](03-investigations.md#inv-internal-ram) |
| `SSL - Memory allocation failed` (-32512) | D-03 | D-19 (mbedTLS allocation mode) | TLS wants about 40-50 KB internal per session |
| Memory shrinks over hours | D-01 (`largest block`, `min ever`) | D-11 | A shrinking largest block means fragmentation |
| Stack overflow suspected | D-01 (`stack free`) | D-19 | High-water marks are in the heartbeat. See [#inv-loop-task-stack](03-investigations.md#inv-loop-stack-overflow) |
| Text clipped, overlap, odd layout | D-10 (screenshot every screen) | D-09 | Numbers cannot show a picture. See [#inv-screenshot-layout-bugs](03-investigations.md#inv-barchart-empty) |
| HTTP body garbled or truncated | D-12 (framing at every split) | D-13 | Chunked/Content-Length handling is deterministic. Test it on the PC |
| Need an API's size, headers, chain, Range support | D-13 | D-12 | Costs seconds. Do it before writing the fetcher |
| TLS handshake fails to a new host | D-13 (`openssl s_client -showcerts`) | D-19 | Find which root to pin. See [#inv-tls-ca-chains](03-investigations.md#inv-tls-gateway-refusals) |
| SD write or read errors | D-16 (sweep, registers, soak) | D-19 | Separate card, clock and driver. See [#inv-sd-multiblock-write](03-investigations.md#inv-sd-multiblock-write) |
| "Did that optimisation help?" | D-17 (benchmark) or D-04 + D-01 numbers | D-11 | Never trust a feeling. Compare logs |
| Behaviour did not change after a "fix" | Check the flash really happened (see D-11 pitfalls) | D-17 pitfall (byte-identical firmware) | Two separate ways to test the old binary. See [#inv-parallel-tool-collisions](03-investigations.md#inv-tooling-pitfalls) |
| Serial port opens and the board resets | D-08 (DTR/RTS before open) | | CH340 auto-reset |
| Suspect a C string or syntax slip from a scripted edit | D-14 (lint) | D-15 | Under a second per file versus about 60 s for a full build. See [#inv-heredoc-string-corruption](03-investigations.md#inv-heredoc-newlines) |
| Board will not boot or behaves oddly after flashing | D-18 (restore) | | The factory image is the way back |
| Parallel agents or sessions corrupt each other's builds | D-20 and D-15 | | Separate build dirs, one owner per file and per device |

---

## D-01 Serial heartbeat

**When.** Always on. It is the first thing to read after any change and the baseline for every other method.

**How.** `loop()` in `projects/05-powerwall-display/src/main.cpp` prints a block every 10 s (115200 baud, CH340 console on UART0). It prints:

| Line | Fields | Answers |
|---|---|---|
| Status | Wi-Fi name and dBm, live values, `screen N`, `fps`, `max gap ms` | Is it alive, how smooth was the window |
| `mem:` | `internal free`, `largest block`, `min ever`, `stack free net`, `stack free lvgl` | Leaks, fragmentation, low point, stack headroom |
| `lvgl pool:` | `% used`, `free`, `largest`, SD `ok/none`, clock MHz, lines, `io err` | LVGL heap use, SD health |
| `redraw:` | `px/s`, `flushes/s`, `TLS connects`, `requests`, `failures` | Idle redraw load, network churn |

Sample, values rounded and Wi-Fi name scrubbed:

```
[<home-ssid> -58 dBm] solar 2500 home 3400 grid 900 batt 0 soc 100.0% on-grid  | today solar 0.00 home 0.00 kWh | screen 0 | 46.4 fps, max gap 1547 ms
  mem: internal free 136144, largest block 71668, min ever 132652 | stack free net 7628 lvgl 9604
  lvgl pool: 16% used, 867000 free, largest 866348 | sd ok 20 MHz, 0 lines, io err 0
  redraw: 204962 px/s, 57 flushes/s | net: 2 TLS connects, 4 requests, 0 failures
```

The same `mem:` line is available on demand with the serial command `m` (D-09).

Code pattern for the counters (LVGL display events, registered after `lvglBegin()`):

```cpp
lv_display_add_event_cb(disp, frameCb,       LV_EVENT_REFR_READY,      nullptr); // frame done
lv_display_add_event_cb(disp, flushStartCb,  LV_EVENT_FLUSH_START,     nullptr); // flushes/s
lv_display_add_event_cb(disp, invalidateCb,  LV_EVENT_INVALIDATE_AREA, nullptr); // px/s
// per window: heap_caps_get_free_size / get_largest_free_block / get_minimum_free_size (MALLOC_CAP_INTERNAL),
//             uxTaskGetStackHighWaterMark(handle or nullptr), lv_mem_monitor(&lm)
```

**What it told us.**

- The 8 KB default `loopTask` stack (where LVGL renders) had under 1 KB free and overflowed once when LVGL's JPEG decoder was registered. The `stack free ... lvgl` field showed it, and the stack was raised to 16 KB (`SET_LOOP_TASK_STACK_SIZE`). See [#inv-loop-task-stack](03-investigations.md#inv-loop-stack-overflow).
- `redraw:` gave the idle-animation baseline on the main screen: in steady state about 270,000-300,000 invalidated px/s at 235-245 flushes/s (the first, shorter window read 205,000 px/s and 57 flushes/s). It is the number to compare when changing animations.
- `net: ... TLS connects` made the cost of a handshake per gateway poll visible: 275 connects for 722 requests in 590 s on one build, and 22 connects for 857 requests with the gateway keep-alive option on. See [#inv-gateway-keepalive](03-investigations.md#inv-tls-gateway-refusals).
- `min ever` is the low-water mark since boot. It is what exposed the 92 KB low point during a TLS handshake (D-03), which the "free now" figure (about 160 KB) hid.

**Pitfalls.**

- `internal free` is a snapshot. A reading of 121 KB while the gateway TLS session is open is normal (the session holds about 40 KB). Judge by `min ever` and by the value after the session closes.
- The first window after boot is shorter than 10 s. The `redraw:` rate divides by the real window length.
- The status line contains the Wi-Fi name. Scrub captured logs before sharing them (replace it with `<home-ssid>`).
- Counters are written on the LVGL core and read on the same core. Network counters (`net::stats()`) are written by the network task and read racily. That is fine for a diagnostic.

**Cost.** A few hundred bytes of code. One `printf` block per 10 s.

---

## D-02 Boot `memStep` trace

**When.** You need to know who spends internal RAM, or you are about to add a subsystem.

**How.** Call `memStep("name")` after each init step. It prints the internal free size and the delta from the previous call.

```cpp
static void memStep(const char *what) {
    static size_t prev = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("  [mem] %-10s internal free %6u (%+d)\n", what, (unsigned)now, (int)now - (int)prev);
    prev = now;
}
// setup(): memStep("boot"); initDisplay(); memStep("display"); historyStart(); memStep("sd task"); ... per screen ...
```

Capture the first 5-10 s after a reset (D-08) and read the deltas.

**What it told us** (project 05, early build, bytes):

| Step | Internal free | Delta |
|---|---|---|
| boot | 305,940 | 0 |
| display | 285,564 | -20,376 |
| sd task | 278,612 | -6,952 |
| lvgl | 230,344 | -48,268 |
| screens (house, today, health, weather) | 216,828 | -216, -9,108, -4,192, 0 |
| net tasks | 163,232 | **-66,572** |

- The network tasks (stacks, Wi-Fi, TLS state) were the biggest single cost. The LVGL draw buffer was second.
- Later screens (moon, aurora, settings, about) cost 0 bytes internal, because their widgets live in the LVGL PSRAM pool. This was the evidence that more screens were safe.
- Doubling the bounce buffers from 480x10 to 480x20 px showed up exactly in the `display` step: -20,376 became -39,880 bytes. That is the price of the crackle fix in [#inv-display-crackle](03-investigations.md#inv-crackle).

**Pitfalls.**

- The trace only covers `setup()`. Memory taken later by tasks, Wi-Fi and TLS needs D-01 and D-03.
- A delta can be positive: the moon step read `+12,976` right after "SD: card mounted" appeared in the log, so memory was released by the background SD task finishing its mount, not by the screen. Do not read a positive delta as a leak fix, and do not attribute a delta to the step that merely happened to print it. Unverified: the exact allocation that was released.

**Cost.** A dozen lines. One serial capture after a reset.

---

## D-03 `[tls]` per-job low-water lines

**When.** The "free now" figure looks fine but TLS fails, or you want to know which network job is the low point.

**How.** Wrap each TLS job and print the internal free size before it and the minimum-ever before and after (`runTls()` in `powerwall.cpp`):

```cpp
size_t before    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t minBefore = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
bool ok = j.fn();
Serial.printf("  [tls] %s: job free before %u, min ever %u -> %u\n", j.name, (unsigned)before,
              (unsigned)minBefore, (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
```

If `min ever` drops across a job, that job made the new low point. If it stays equal, the job was no worse than an earlier one.

**What it told us.**

(The original lines had no job name; the names were added later. The order identifies the job.)

```
[tls] weather: job free before 161096, min ever 116252 -> 92136     <- the first Open-Meteo fetch set the low point
[tls] kp:      job free before 159992, min ever  92136 ->  92136
[tls] moon:    job free before 159992, min ever  92136 ->  92136
```

- The Open-Meteo handshake used about 69 KB internal at its peak. The largest free block fell to 44 KB. That is below the 100 KB guideline and below what a second allocation of any size could use.
- After moving mbedTLS large allocations and JSON buffers to PSRAM, a single network task and `WiFi.persistent(false)`, the minimum internal free during TLS rose from 92 KB to about 140-155 KB and the largest block from 44 KB to about 90 KB (commit message `cca53c5`). See [#inv-internal-ram-low-water](03-investigations.md#inv-internal-ram).
- Raising the PSRAM threshold for mbedTLS blocks from 1 KB to 4 KB (to stop display crackle) moved the low point back down: later soaks read 115-118 KB (105 KB with the gateway keep-alive option). The trade-off was visible in the same line.

**Pitfalls.**

- Only jobs that go through the wrapper are covered. A fetch outside it (for example the gateway poll) needs its own line.
- `min ever` is global since boot, not per job. Compare the "before" and "after" values rather than the absolute number.

**Cost.** Four lines per job table. One extra log line per fetch.

---

## D-04 `[stall]` and `[slow]` render diagnostics

**When.** The display stutters, an animation hitches, or you want to know whether a slow frame was LVGL rendering, the flush, or something else holding up the loop.

**How.** Three LVGL display events plus two timers in `loop()` (all in `main.cpp`, marked `DIAG`):

| Log line | Meaning | Source |
|---|---|---|
| `[stall] N ms at T` | Gap between completed frames above 120 ms (below 2 s, to skip idle) | `LV_EVENT_REFR_READY` gap |
| `[slow] render N ms: A areas, P px, box x1,y1-x2,y2` | One frame took over 100 ms. Lists how many areas LVGL invalidated, the pixels, and their bounding box | `REFR_START` to `REFR_READY`, `INVALIDATE_AREA` |
| `[slow] first areas: x,y WxH ...` | Printed after a slow render that had more than 30 areas. The first 16 areas of the frame | `INVALIDATE_AREA` |
| `[slow] lv_timer_handler N ms (net P)` | The whole LVGL handler call over 100 ms | timer around `lv_timer_handler()` |
| `[slow] loop not run for N ms (net P)` | Time between loop iterations over 100 ms: something outside LVGL blocked `loop()` | loop timer |
| `[slow] refresh: energy A ms, screen B ms (net P)` | The 500 ms data refresh callback over 50 ms | `refreshCb` |

Real output (before the flush fix, scrubbed of nothing sensitive):

```
 10.7   [slow] render 292 ms: 106 areas, 130674 px, box 0,0-479,479
 10.7   [stall] 343 ms at 10342
 10.7   [slow] lv_timer_handler 312 ms (net 1)
139.3   [slow] render 293 ms: 81 areas, 135411 px, box 0,0-474,427
```

and the "first areas" line, which shows the same label invalidated repeatedly:

```
[slow] first areas: 282,147 9x9 282,147 9x9 62,85 75x27 132,308 73x30 ... 334,13 138x24 334,13 138x24 334,13 138x24 334,13 138x24
```

**What it told us.**

- The recurring slow frames were data-update frames: 76-106 invalidated areas covering only about 130,000-140,000 px (a quarter of the screen), yet 292-309 ms. The pixel count was small, so the cost per area was the problem, not the amount drawn. Reading the flush code found the rotated draw wrote column-wise and wrote back every full framebuffer row it touched (about 460 KB of cache write-back for a 480x48 strip).
- After the fix in `Board4848S040_lvgl.cpp` (contiguous row segments, per-segment write-back), a full-screen redraw went from 254 ms to 142 ms and the many-area redraws from about 300 ms to 145-185 ms. See [#inv-main-screen-stutter](03-investigations.md#inv-stutter-rotated-flush).
- Repeated identical rectangles in "first areas" (`334,13 138x24` five times) flagged redundant invalidations as a second, separate saving.

**Pitfalls.**

- The first frame after boot always reads as a big stall (`stall 1755 ms`, a 1.3-1.6 million px render). Ignore it.
- Pixels are summed per invalidated area, so overlapping areas count more than once. The figure measures work requested, not unique pixels.
- Printing to serial from the render path costs time. These lines only print above a threshold, which keeps the diagnostic from becoming the problem.
- Do not trust a soak that was run on the wrong binary. See D-11.

**Cost.** About 60 lines of diagnostic code. No measurable cost when nothing is slow.

---

## D-05 Net-phase correlation

**When.** A stall or slow frame might be caused by network activity on the other core.

**How.** The network task sets a global flag around its phases; the diagnostics print it:

```cpp
// powerwall.cpp
volatile uint8_t g_netPhase;   // 0 idle, 1 gateway poll, 2 background job
g_netPhase = 1; pollOnce();  g_netPhase = 2; runOneJob();  g_netPhase = 0;
// main.cpp
extern volatile uint8_t g_netPhase;
Serial.printf("  [slow] lv_timer_handler %lu ms (net %u)\n", ms, g_netPhase);
```

Count the phases across a soak:

```powershell
Select-String -Path soak.log -Pattern '\[slow\] lv_timer_handler' | ForEach-Object { $_.Line -replace '.*\(net (\d)\).*','net $1' } | Group-Object | Select-Object Name, Count
```

**What it told us.** In the pre-fix soaks every slow `lv_timer_handler` after the boot frame read `net 1` (5 of 5 in `soak-flush`, 3 of 3 in `soak15`), so the stalls lined up with the 2 s gateway poll rather than the background jobs (phase 2). That pointed at what a poll does on the UI core: new data, so many small invalidations. Combined with D-04 it led to the flush path, not the network. In the later keep-alive soak the flag was mixed (`net 0` three times, `net 1` once, `net 2` once), which showed the poll was no longer the only trigger once the flush was fixed. See [#inv-main-screen-stutter](03-investigations.md#inv-stutter-rotated-flush).

**Pitfalls.**

- The flag shows which phase was active when the line printed, not that the network caused the delay. Use it to narrow the field, then confirm with an A/B (D-06) or the area log (D-04).
- A single `volatile` byte written by one core and read by the other is fine for diagnostics. Do not use it for synchronisation.

**Cost.** Three assignments. Remove or keep as `DIAG`.

---

## D-06 A/B builds

**When.** You need to attribute a cost (RAM, time, crackle) to one subsystem and cannot tell by reading the code.

**How.**

1. Capture the baseline with the diagnostic you trust (D-02, D-03, D-04).
2. Make one temporary, clearly marked edit, for example comment out the SD task:

   ```powershell
   (Get-Content src\main.cpp -Raw).Replace("    historyStart();`n","    // historyStart(); // TEMP RAM A/B`n") | Set-Content src\main.cpp -NoNewline
   ```

   For C/C++ strings prefer the Edit tool or an editor over shell replace (see D-14 and [#inv-heredoc-string-corruption](03-investigations.md#inv-heredoc-newlines)).
3. Flash (`.\tools\pio.ps1 05-powerwall-display upload`), capture the same window, compare.
4. Revert the edit, check `git diff`, reflash the real build.

**What it told us** (SD task on versus off, same build otherwise):

| | With SD | Without SD |
|---|---|---|
| Free after startup | 163 KB | 172 KB |
| Free when idle | about 160 KB | about 168 KB |
| Lowest ever | **92 KB** | **110 KB** |

The SD subsystem cost about 8 KB normally and about 18 KB at the low point (its task and file buffers are live while a weather fetch runs). That was a single A/B run each, but the gap was large enough to act on. See [#inv-internal-ram-low-water](03-investigations.md#inv-internal-ram).

The bounce-buffer size (480x10 versus 480x20) was an A/B on the same principle for crackle: the user reported "crackle not showing up yet" after the change, but a long-run confirmation is not logged ([#inv-display-crackle](03-investigations.md#inv-crackle)).

**Pitfalls.**

- Change one thing. A build that also moves a threshold or a buffer size hides which change mattered.
- Mark the temporary edit (`TEMP RAM A/B`) so the revert and the `git diff` check are mechanical.
- Single runs have noise. Network jobs run on a schedule, so align the comparison windows (same time since boot).
- `-O2` placed in `build_flags` alone gave a byte-identical binary, because the core adds `-Os` afterwards. Verify an A/B really changed the firmware (see D-17).

**Cost.** One build and flash (about 60 s) plus one capture per arm.

---

## D-07 Decode backtraces

**When.** The log shows `Guru Meditation Error`, `abort()`, `task_wdt: ...`, `Backtrace: 0x...`, or an unexplained `rst:` line.

**How.**

1. Capture the boot banner and panic text (D-08). Read the `rst:` code. `rst:0xc (RTC_SW_CPU_RST)` follows an abort or panic; `rst:0x10 (RTCWDT_RTC_RST)` is the RTC watchdog; `rst:0x1 (POWERON)` is a power-on.
2. Copy the addresses from the `Backtrace:` line (the part before each colon).
3. Decode with the toolchain's addr2line against the ELF that was flashed:

   ```powershell
   $a2l = "$env:USERPROFILE\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-addr2line.exe"
   & $a2l -pfiaC -e projects\05-powerwall-display\.pio\build\esp32-4848s040\firmware.elf `
       0x42091834 0x4208fa6d 0x4211210e 0x420c8e31 0x420c2e55 0x420c3761
   ```

   `-p` pretty print, `-f` function names, `-i` inlined frames, `-a` print the address, `-C` demangle.
4. Check `ELF file SHA256: <prefix>` in the panic text against `Get-FileHash -Algorithm SHA256 firmware.elf`. If they differ, you are decoding the wrong build.

`monitor_filters = esp32_exception_decoder` is set in `common/platformio-base.ini`, so `pio device monitor` decodes live. We decoded by hand because we captured with our own scripts (D-08); the monitor filter has been configured but not relied on.

**What it told us.** A task-watchdog reset with `pw_gw` running on CPU 0 decoded to:

```
lwip_read -> esp_vfs_read -> read -> mbedtls_net_recv -> mbedtls_ssl_fetch_input -> ssl_get_next_record
-> mbedtls_ssl_read -> data_to_read (ssl_client.cpp) -> NetworkClientSecure::available()
-> HTTPClient::connected() -> HTTPClient::writeToStreamDataBlock -> HTTPClient::writeToStream -> HTTPClient::getString
```

That placed the stall in HTTPClient's chunked reader polling a kept-alive TLS socket without yielding, which starved the idle task. The fixes were HTTP/1.0 for chunked responses, network tasks at idle priority and a short read timeout. The theory first formed ("a response with no length") was wrong: `curl` (D-13) showed every response declared its length or was chunked, and the backtrace plus the library source (D-19) found the real loop. See [#inv-task-wdt-chunked-httpclient](03-investigations.md#inv-chunked-stall).

**Pitfalls.**

- The ELF must match the flashed build exactly. Rebuild without flashing, or flash a different env, and the lines will be plausible but wrong.
- Strip the toolchain paths if you paste the output into a public issue (they are `/home/runner/...` for framework code, which is harmless, but local paths are not).
- The last frame in a backtrace is often a FreeRTOS entry point; do not chase it.
- Run it from PowerShell. The decode itself works in any shell, but keep the habit for everything that touches `.platformio`.

**Cost.** About 30 seconds once you have the log. The hard part is capturing the crash (D-08).

---

## D-08 Timestamped serial capture

**When.** Anything that needs more than the live monitor: boot traces, soaks, rare crashes, before/after comparisons.

**How.** A small pyserial script run with PlatformIO's Python (it bundles pyserial). Prefix each line with seconds since start so stalls and periods are readable.

```python
# serial_capture.py PORT SECONDS OUTFILE [--no-reset] [--until TEXT]
import serial, sys, time
port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
until = sys.argv[sys.argv.index("--until") + 1].encode() if "--until" in sys.argv else None
s = serial.Serial()
s.port = port; s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False          # set BEFORE open(): otherwise the CH340 resets the board
s.open()
if "--no-reset" not in sys.argv:
    s.rts = True; time.sleep(0.1); s.rts = False   # pulse EN, IO0 stays high -> normal boot
end = time.time() + secs; t0 = time.time(); seen = b""
with open(out, "wb") as f:
    while time.time() < end:
        data = s.read(4096)
        if data:
            f.write(data); f.flush()
            seen = (seen + data)[-8192:]
            if until and until in seen and end - time.time() > 1:
                end = time.time() + 1      # stop 1 s after the marker
s.close()
```

A line-oriented variant that adds the timestamp prefix (used for the 10-15 minute soaks):

```python
import serial, time
s = serial.Serial(); s.port = "COM9"; s.baudrate = 115200; s.timeout = 0.5
s.dtr = False; s.rts = False; s.open()
s.rts = True; time.sleep(0.1); s.rts = False
t0 = time.time(); end = t0 + 900
while time.time() < end:
    l = s.readline().decode("utf-8", "replace").rstrip()
    if l: print("%6.1f %s" % (time.time() - t0, l), flush=True)
```

Run it:

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$env:PYTHONIOENCODING = "utf-8"
& $py -I serial_capture.py COM9 120 boot.log                       # reset, capture 120 s
& $py -I serial_capture.py COM9 120 run.log --no-reset --until "Rebooting"   # attach and wait for a crash
```

**Catch a rare crash.** Loop the capture over several boots and stop at the first one that panics (`--until "Rebooting"` ends each capture early on a reset):

```powershell
for ($k = 1; $k -le 5; $k++) {
    & $py -I serial_capture.py COM9 45 "wdt$k.log" --until "Rebooting"
    if (Select-String -Path "wdt$k.log" -Pattern 'task_wdt|Guru' -Quiet) { "caught in boot $k"; break }
}
```

**The CH340 reset gotcha.** Opening the port asserts DTR and RTS; the CH340 auto-reset circuit turns that into a board reset (and, with the wrong sequence, the bootloader). `serial.Serial('COM9')` opens immediately, so set `dtr`/`rts` on a port object **before** `open()`. `screendump.py` and `sd_serial.py` do this. When you want a clean boot capture, pulse `rts` yourself as above (DTR low keeps IO0 high, so the chip boots normally).

**What it told us.**

- The first screenshot attempt (D-10) reset the board when the port opened. That is how the gotcha was found. Setting both lines low before opening fixed it.
- A 120 s swipe-and-tap capture and five 45 s boots did not reproduce the task watchdog. It was first seen in an ordinary flash-and-monitor capture. Keep a capture running whenever the board is up, because rare faults arrive when you are not looking.
- Timestamped 15-minute logs are what made the soak comparisons in D-11 possible.

**Pitfalls.**

- A serial monitor, a capture and an upload cannot share the port. Close one before using another. "Failed to connect" on upload is often an open monitor.
- Plug-in events can drop the CH340 link: inserting an SD card briefly dropped the PC's USB-serial link on the tested unit, without resetting the ESP32. Reopen the port.
- Windows consoles default to cp1252. Printing a log line with a non-ASCII character raises `UnicodeEncodeError` (seen twice). Set `$env:PYTHONIOENCODING = "utf-8"` or write to a file in binary.
- `python -I` (isolated mode) stops a stray `serial.py` or `json.py` in the working directory from shadowing the real module.
- The heartbeat contains the Wi-Fi name and live power figures. Scrub before sharing a log.

**Cost.** Seconds to run. The scripts above are not in the repo yet; a sensible home is `tools/serial_capture.py`.

---

## D-09 Serial command interface

**When.** You want to drive the UI without touching it: automated screenshots, per-screen timing, reproducible captures.

**How.** `pollSerialCommands()` in `main.cpp` reads one line without blocking. One line in, one reply line ending `#ok` or `#err <reason>`.

| Command | Does |
|---|---|
| `s <n>` | Go to screen `n` (0..10) the way a swipe does (data refresh, slide, entry animations); `#err screen disabled` if `n` is switched off in Settings |
| `n` / `p` | Next / previous **enabled** screen (like a swipe; wraps) |
| `l` | List screens, current one marked `*`, disabled ones `-` |
| `e [n 0\|1]` | Screens on/off (same as the Settings switches, saved); `e` alone lists the mask |
| `b <0-255>` | Set backlight now (not saved; the panel driver cuts out below about 100) |
| `bf <hz>` | Backlight PWM frequency 100..1000000 Hz (not saved) |
| `bd <duty>` | Raw backlight duty at the current resolution (not saved) |
| `br` | Report backlight frequency, resolution, duty, max, assumed minimum |
| `bs [steps [ms [from [to]]]]` | Background duty sweep for watching the panel; `bs 0` or any `b`/`bd`/`bf` stops it |
| `y [d\|w\|m\|-\|+]` | History screen tab / period; also prints the day-history store status |
| `y s <1..50>` | Diagnostic: save today's record n times in a row (panel-drift investigation) |
| `v <0\|1>` | Privacy mode for screenshots (not saved) |
| `m` | Print the memory / heap line now |
| `x` | Screenshot (see D-10) |
| `c` | List the SD cache records (may do SD I/O; diagnostic) |
| `k` | Diagnostic: force an RGB scan-out restart at the next VSYNC (panel drift, see D-21) |
| `r` | Reboot (`ESP.restart()`; RTC data survives) |
| `h` | Help |

The source of truth is the comment block at the top of `projects/05-powerwall-display/src/main.cpp`; keep this table in step with it.

```powershell
# a pyserial one-liner, or type into any serial terminal at 115200
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $py -I -c "import serial,time; s=serial.Serial(); s.port='COM9'; s.baudrate=115200; s.timeout=2; s.dtr=False; s.rts=False; s.open(); s.write(b'l\n'); print(s.readline())"
```

**What it told us.** Stepping through all the screens over serial (ten at the time, 11 now) turned "I only tested the main screen" into a per-screen check, and it is the hook that makes D-10 possible. The commands were added at the user's request so every screen could be checked, after a stutter investigation that had only covered the main one.

**Pitfalls.**

- Commands are read in `loop()` (core 1), not in an interrupt. If `loop()` is blocked, they wait. That is itself a signal.
- `s <n>` for the screen already shown just replies `#ok`.
- Give a screen time to settle (default 2 s in `screendump.py`) before judging it: entry animations start after the 300 ms slide.

**Cost.** About 100 lines in `main.cpp`. Pays for itself the first time you need a screenshot.

---

## D-10 Framebuffer screenshots

**When.** Anything visual: clipping, overlap, colours, a new screen, a refactor that should not change the picture. Numbers cannot show layout.

**How.** The `x` command sends the native RGB565 framebuffer as base64 lines (`#px <row> <b64>`) at 921600 baud, with a CRC32 trailer. `screendump.py` switches baud, collects rows, rotates to what you see and writes a PNG without third-party imaging libraries.

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $py projects\05-powerwall-display\tools-dev\screendump.py COM9 shot.png               # current screen
& $py projects\05-powerwall-display\tools-dev\screendump.py COM9 shot.png --screen 4    # go to screen 4 first
& $py projects\05-powerwall-display\tools-dev\screendump.py COM9 shots\v1 --all         # every screen: v1-<n>-<name>.png
```

Output per image: `shots\v1-0-house.png: 480x480, 0 missing rows, crc ok`. A `crc MISMATCH` or missing rows means an interleaved log line or a dropped byte; retry.

The working tree also has an uncommitted `--privacy` option (sends `v 1` / `v 0` so the firmware shows a placeholder Wi-Fi name and town while capturing). Check `--help` before publishing screenshots; treat the screen as containing local details until you have used it.

**What it told us.** One `--all` run captured all 10 screens (the count at the time; 11 now) with good CRCs (about 460 KB each). Looking at them found two layout problems that no timing or heap number could show:

- Bar charts built by the new `BarChart` helper were sized wrongly because their height was read before layout settled; the fix stores the height at creation time.
- A "Geomagnetic" badge overflowed its chip, and a lifetime text line was too long on the health screen.

See [#inv-screenshot-layout-bugs](03-investigations.md#inv-barchart-empty). It also verifies refactors: capture before and after and compare.

**Pitfalls.**

- Opening the port resets the board unless DTR/RTS are held low first (D-08). `screendump.py` does this.
- The picture is the **framebuffer**, so it matches the panel only if the panel is healthy. It will not show crackle, tearing or a dead backlight.
- The firmware switches to 921600 for the transfer and back to 115200 itself afterwards. If the script dies mid-transfer, the PC side may be left at the wrong baud; reopen the port at 115200.
- `to_rgb()` handles rotations 0-3, but only rotation 3 (the project default) has been exercised. Check a new rotation against the real panel.
- Other log lines can interleave, but each pixel row is one `Serial.write`, so a row is never split.

**Cost.** About 7 s of transfer per screen (calculated: 480 rows of base64, about 614 KB, at 921600 baud) plus the 2 s settle time, so `--all` over the 11 screens is roughly 100 s (about 90 s when there were ten).

---

## D-11 Soak tests

**When.** After any change that touches memory, networking, timing or redraw. Faults like fragmentation, rare reboots and crackle only show after minutes.

**How.**

1. Flash, confirm the flash succeeded (below), then capture 10-15 minutes with timestamps (D-08).
2. Summarise with `Select-String` or a few lines of Python:

   ```powershell
   Select-String -Path soak.log -Pattern '\[stall\]|\[slow\]|Guru|task_wdt|Backtrace' | Select-Object -First 40
   Select-String -Path soak.log -Pattern 'min ever (\d+)' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Measure-Object -Minimum
   Select-String -Path soak.log -Pattern '([\d.]+) fps, max gap (\d+) ms' | Select-Object -Last 3
   ```

   ```python
   import re, sys
   t = open(sys.argv[1], encoding="utf-8", errors="replace").read()
   fps    = [float(x) for x in re.findall(r"(\d+\.\d) fps", t)]
   gaps   = [int(x)   for x in re.findall(r"max gap (\d+) ms", t)]
   mins   = [int(x)   for x in re.findall(r"min ever (\d+)", t)]
   render = sorted(int(x) for x in re.findall(r"\[slow\] render (\d+) ms", t))
   print("fps %.1f-%.1f  max gap %d  min internal %d  slow renders %s  stalls %d"
         % (min(fps), max(fps), max(gaps), min(mins), render, len(re.findall(r"\[stall\]", t))))
   ```

3. Compare against the previous soak line by line, not by memory.

**What it told us.** Four soaks of project 05's main screen (one 15 min, three about 10 min). Slow renders include the first full-screen frame after boot, which is always large:

| Log | Build | Slow renders over 100 ms | Stalls | fps range | Lowest `min ever` |
|---|---|---|---|---|---|
| `soak15` (15 min) | before the flush fix | 255, 293, 296 ms | 5 | 39-58 | 118,052 |
| `soak-flush` | **old firmware** (upload failed) | 254-309 ms (6) | 7 | 40-58 | 118,072 |
| `soak-flush2` | rotated-flush fix | **142, 144 ms (2)** | 2 | 40-50 | 117,576 |
| `soak-keepalive` | + gateway keep-alive | 134-189 ms (4) | 5 | 44-60 | 105,400 |

- `soak-flush` is the instructive row. It was meant to test the flush fix, but the upload had failed (a `FileExistsError` while another process was installing a tool), so the board still ran the old firmware. The unchanged 254-309 ms render times gave it away. Always confirm a flash before reading a soak: look for `SUCCESS` in the pio output, a fresh boot banner, and a log line or number that only the new build can produce.
- Fragmentation was not a problem in the refactor-build soak: `largest block` read 60,404 at 30 s and again at 581 s, and `min ever` moved by only 352 bytes (115,456 to 115,104) over the window.
- Comparing the refactor-build soak (275 TLS connects, 722 requests, `min ever` 115,104) with the keep-alive soak (22 connects, 857 requests, `min ever` 105,400) showed keep-alive cut handshakes by more than 10x and cost about 10 KB of low-water mark. These are different builds, not a pure A/B. See [#inv-gateway-keepalive](03-investigations.md#inv-tls-gateway-refusals).

**Pitfalls.**

- Verify the flash (above). This one mistake cost a full soak.
- Do not compare soaks from different times since boot. Weather, aurora and the sun image arrive in the first minute and set the memory low point; later windows are quieter.
- A soak on one screen says nothing about the others. Use D-09 to rotate screens.
- Tool collisions ruin captures: see D-15 and D-20.
- Logs can be large. Keep them out of git (the SD bench logs are git-ignored).

**Cost.** Wall-clock time only (10-15 minutes), but it can run in the background while you do other work. Start it, then lint or write host tests.

---

## D-12 Host tests with MSVC

**When.** Logic that does not need the hardware: HTTP framing (chunked decoding, Content-Length, read-until-close), date parsers, text parsers. Bugs here are deterministic and slow to find on the board.

**How.** `projects/05-powerwall-display/hosttest/` compiles `test.cpp` with MSVC on the PC. `run.ps1` finds Visual Studio with `vswhere` and sets `INCLUDE`/`LIB` itself, so no developer prompt is needed.

```powershell
.\projects\05-powerwall-display\hosttest\run.ps1      # prints "N passed, 0 failed"; exit code = failures
```

Design points that made it work:

- Put the logic in a plain C++ header (`src/net_body.h`: de-chunker and body read loop) with no Arduino dependencies, templated over the socket and clock, so a fake socket and fake clock can drive it.
- Feed every sample at every split: one cut at each byte offset, every pair of offsets for short samples, and byte by byte. A chunked decoder that works on whole buffers can still fail when a chunk-size line is split across reads.
- Use real payloads captured with curl (D-13), stored in `hosttest/data/`.
- Stub only what the code includes (`stubs/` holds two minimal ESP/Arduino headers). PlatformIO ignores the folder.

**What it told us.** 217 checks passed (de-chunker including trailers, bare LF, bad framing, oversized chunks and a sink that refuses; the body loop with a stalled server hitting its deadline without spinning; ISO 8601 and HTTP-date parsing; the ENSO page and file parsers on real payloads). This is the safety net that let the four separate HTTP body readers be merged into one without hardware time, and the stall in D-07 is the class of bug the read-loop deadline tests are aimed at. See [#inv-task-wdt-chunked-httpclient](03-investigations.md#inv-chunked-stall).

**Pitfalls.**

- It needs Visual Studio with the C++ workload and a Windows SDK. There was no `g++` on the PC.
- MSVC needs `localtime_r` / `gmtime_r` shims (`#define` to `localtime_s` / `gmtime_s`) for code written for newlib.
- Host tests prove the logic, not the timing or memory on the device. They complement D-01 to D-11; they do not replace them.
- Build output goes to `%TEMP%\hosttest-05`.

**Cost.** Seconds to build and run (not timed). Writing the fake socket takes the time.

---

## D-13 curl and openssl checks

**When.** Before writing a fetcher for a new API. Also when a TLS handshake fails or a response misbehaves.

**How.** Use `curl.exe` (not the PowerShell `curl` alias) and `openssl s_client`.

```powershell
# size, headers, keep-alive, chunked or not
curl.exe -s -D - -o NUL https://services.swpc.noaa.gov/json/solar_probabilities.json | Select-String '^HTTP|content-length|transfer-encoding|connection|last-modified|cache-control'

# does the server honour Range? expect 206 and the byte count
curl.exe -s -D - -o NUL -r -400 https://www.cpc.ncep.noaa.gov/data/indices/oni.ascii.txt | Select-String '^HTTP|content-range|content-length'

# which chain does the server send, and which root to pin?
echo | openssl s_client -connect api.open-meteo.com:443 -servername api.open-meteo.com -showcerts 2>$null | Select-String '^ ?[0-9] s:|i:'

# does a candidate root verify the chain?
echo | openssl s_client -connect api.open-meteo.com:443 -servername api.open-meteo.com -CAfile isrgrootx1.pem 2>&1 | Select-String 'Verify return code'

# the local gateway (placeholders, never commit real values)
curl.exe -sk --max-time 8 -D - -o NUL -H "Authorization: Bearer $env:GW_TOKEN" https://<gateway-ip>/api/meters/aggregates | Select-String '^HTTP|content-length|transfer-encoding'
```

**What it told us.**

- The Powerwall gateway's `/api/meters/aggregates` is `Transfer-Encoding: chunked`; `soe`, `grid_status` and login return `Content-Length`. That decided which endpoints needed the de-chunker and which could keep-alive safely.
- CPC (ENSO) files honour `Range: bytes=-N` (tested: `-400` on `oni.ascii.txt` returned 206 and 400 bytes), so a tail fetch replaces 23 KB to 148 KB downloads with 200-1,100 bytes. SWPC files send `Content-Length` and `Connection: keep-alive`. Sizes of every candidate endpoint are tabulated in [11-space-weather-and-climate-data](../11-space-weather-and-climate-data.md).
- The chain output decides which root to pin: SWPC chains to Amazon Root CA 1, CPC to Go Daddy Root CA G2, several NASA hosts to ISRG Root X1. See [#inv-tls-ca-chains](03-investigations.md#inv-tls-gateway-refusals).

**Pitfalls.**

- Some servers answer differently to the default curl user agent (BOM returned 403 until a browser UA was sent). Test with the UA the device will use.
- A passing curl does not prove the ESP32 will connect: mbedTLS on the device only has the roots you pin. Always test the pinned root with `-CAfile` as above.
- `-k` on the gateway is for the self-signed certificate during testing only. Do not copy it into firmware.
- Keep tokens in an environment variable, and never paste the gateway IP, password or token into docs, commits or logs.
- Check `Range` on each server individually; SWPC's Range support was untested.

**Cost.** Seconds. Do it first.

---

## D-14 Lint

**When.** After every C/C++ edit and before every commit. It catches syntax slips and a class of real defects in under a minute without flashing.

**How.**

```powershell
.\tools\lint.ps1 05-powerwall-display            # one project
.\tools\lint.ps1                                  # all projects
.\tools\lint.ps1 05-powerwall-display -All        # include low-severity style findings
.\tools\lint.ps1 05-powerwall-display -Refresh    # rebuild the cached compile database
```

What it does (from the script header):

1. `pio run -t compiledb` writes `compile_commands.json` (about 7 s, no compile) into an **isolated build dir**, `$env:TEMP\pio-lint-<project>\build`, so it never collides with a build or flash running in the project.
2. The GCC command lines are translated for clang: only `-D`, `-I`, `-isystem`, `-include`, `-std`, `-O`, `-fexceptions`, `-frtti` are kept, the target is `i686` (32-bit int/pointer like Xtensa), the toolchain's libstdc++/newlib/GCC headers are added with `-isystem`, and `__XTENSA__` is defined so ESP-IDF headers take their Xtensa branches.
3. clang-tidy runs in parallel on our files only (checks and header filter from `.clang-tidy`); cppcheck runs through `pio check`.
4. Only findings in our code are shown (the project `src/`, `common/lib/Board4848S040`, `SdStore`). `common/lib/SD` (patched upstream) and `secrets.h` are never reported. Exit code 1 if any medium or high finding remains.

Suppress a deliberate finding with `// NOLINT(<check>)` or `// cppcheck-suppress <id>`.

**What it told us.**

- Raw `pio check` on project 05 produced 357 findings, of which 336 were in library headers (`.pio/libdeps`) and 208 were `bugprone-macro-parentheses` in library headers. Only 21 were ours (5 medium, 16 low). Tightening the `HeaderFilterRegex` and the cppcheck suppressions turned the output into something readable. After that, project 05 lints clean (28 files).
- Running clang-tidy through `pio check` without `--skip-packages` took over 12 minutes because it analysed the framework headers; with `--skip-packages` it failed to find `Arduino.h`. The translated compile database solved both.
- The linters found no real bugs on their own. A manual read in the same session found a real one (a `millis()` wrap in login back-off after about 24.8 days). Lint is a cheap guard, not a substitute for review.
- The reason lint became a rule: shell heredocs twice turned `\n` inside C string literals into real newlines. A full build takes about 60 s to say "unterminated string"; lint says it in under a second. See [#inv-heredoc-string-corruption](03-investigations.md#inv-heredoc-newlines).

**Pitfalls.**

- A `pio check` can hang with no CPU activity, apparently when another `pio` is installing tools or running at the same time (seen at least twice; the exact cause was not isolated). Run one lint at a time, never alongside another `pio` command that may download packages, and do not kill processes by name (D-20).
- Concurrent `pio` commands can collide on tool installation: `FileExistsError ... tool-cppcheck`. See [#inv-parallel-tool-collisions](03-investigations.md#inv-tooling-pitfalls).
- The first run builds the compile database; later runs reuse it until `platformio.ini`, the shared ini, the script or the source list changes.
- If scripts are blocked: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

**Cost.** Seconds when cached, under a minute cold. Zero hardware time.

---

## D-15 Build-all and isolated builds

**When.** Before any commit (`build-all`); whenever more than one build or agent is active (isolated dirs).

**How.**

```powershell
.\tools\build-all.ps1                                 # builds every projects/*/platformio.ini, prints a PASS/FAIL table, exit 1 on any failure
.\tools\pio.ps1 05-powerwall-display upload -Port COM9   # build + flash one project

# isolated build dir, so parallel builds do not share .pio output
$env:PLATFORMIO_BUILD_DIR = "$env:TEMP\pio-build-agentA"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d projects\05-powerwall-display
Remove-Item Env:\PLATFORMIO_BUILD_DIR
```

For extra compiler warnings without editing the ini, add flags per run: `$env:PLATFORMIO_BUILD_FLAGS = "-Wall -Wextra -Wformat=2"` with its own build dir.

**What it told us.** The extra-warnings pass over projects 01 and 02 (`-Wall -Wextra -Wformat=2`) reported one warning from our code and none with the default flags. After the refactor, `build-all` passed on all six projects and flash fell from 2,263,562 B to 2,250,190 B (-13.4 KB) with RAM unchanged (54,184 to 54,232 B), which confirmed the consolidation had not grown the image.

**Pitfalls.**

- Never build from Git Bash. ESP's tool installer rejects MSYS.
- Parallel builds sharing one `.pio/build` corrupt each other. Give each its own `PLATFORMIO_BUILD_DIR`.
- Do not build or lint while another process may be installing PlatformIO packages (cppcheck, clang-tidy, toolchain). The collision above cost a flash and a soak.
- `custom_sdkconfig` rewrites the shared global framework package and thrashes between projects (see [12-esp32-performance-and-framework](../12-esp32-performance-and-framework.md), section 4c). Do not use it casually in a multi-project repo.
- An upload needs the port free. Close monitors and captures first.

**Cost.** About 55-75 s to build and flash project 05 (one 150 s outlier).

---

## D-16 SD test bench

**When.** Bringing up a card or driver, choosing a clock, or checking hot-swap and durability.

**How.** `projects/06-sd-card-test` is a serial-driven firmware; `sd_serial.py` runs steps in order, waits for each `#done <c>`, and saves `sd_serial_<time>.log` (git-ignored).

```powershell
.\tools\pio.ps1 06-sd-card-test upload -Port COM9
cd projects\06-sd-card-test
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" sd_serial.py COM9 --reset i c t s soak:120 i
```

| Step | Does |
|---|---|
| `i` | Card state, counters |
| `c` | Card registers: CID (maker, model, date), CSD (size, rated clock), SCR |
| `t` | 19 functional tests (folders, text, 256 KB pattern, 1 MB throughput, 500 appends, atomic files incl. torn/orphan/stale copies, 100 files, long names, a PC-written file) |
| `s` | Clock sweep 1-25 MHz, then remember one step below the fastest pass |
| `soak:N` | One CSV record per second with read-back every 10th, for N seconds. Pull the card mid-soak to test hot-swap |
| `wait:N` | Just listen, for example while you pull the card |
| `1/4/9/2/5` | Remount at 1/4/10/20/25 MHz |

For the PC-written file test, put `pc-check.txt` on the card first: `python make_pc_check.py E:/`.

**What it told us.**

- **The driver bug.** On a 16 GB SDHC card the sweep passed at some clocks and failed at others, differently every run (for example 25 MHz passing while 10 and 20 MHz failed `write`). Failures independent of clock pointed away from signal integrity and at the driver. The Arduino core 3.2.1 SD driver did not wait for the card's busy period after the multi-block Stop Tran token, so the next command hit a busy card. A patched copy (`common/lib/SD`) fixed it; afterwards 4 of 4 runs passed at every clock from 1 to 25 MHz. See [#inv-sd-multiblock-write](03-investigations.md#inv-sd-multiblock-write).
- **Healthy sweep after the patch** (16 GB SDHC, KB/s):

  | MHz | 1 | 4 | 10 | 20 | 25 |
  |---|---|---|---|---|---|
  | Write | 101 | 302 | 505 | 650 | 631 |
  | Read | 113 | 407 | 847 | 1329 | 1326 |

  Throughput flattens above 20 MHz, so 20 MHz is the default. The 1 MB test gave about 670 KB/s write and 1.47 MB/s read; a log-line append (open, write, close) averaged about 17 ms.
- **The card.** The register read identified an old 2 GB SDSC card (CSD v1.0, 2011, no speed class). It passed at 1 and 4 MHz (flaky at 4 MHz in other runs) and returned corrupt responses (CRC errors) from 10 MHz, so it was retired. See [#inv-sd-2gb-card](03-investigations.md#inv-sd-card-clock).
- **Supply ruled out.** Running with the backlight off made no difference to failures, which excluded a power-supply cause. (One "backlight off" run was invalid because an upload collided with a tool install; it was repeated.)
- Hot-swap worked: pulling and re-inserting the card recovers without a reset.

**Pitfalls.**

- `--reset` pulses EN first. Without it the port opens without resetting the board.
- Card serial numbers appear in the CID. Do not publish raw register dumps.
- Inserting a card can briefly drop the PC's USB-serial link (not an ESP32 reset). Reopen the port.
- The SD shares the LCD's init-bus pins. Mount after `initDisplay()`.
- Only some failures are card-specific. Vary the card, the clock and the file size before blaming the driver.

**Cost.** Minutes per full run (`t` up to 180 s, `s` up to 240 s).

---

## D-17 LVGL benchmark and variant builds

**When.** Changing a performance default (compiler flags, buffer size, fast memory, threading, framebuffer layout). The rule in `CLAUDE.md` is: benchmark before changing.

**How.** The `bench` env of `projects/03-lvgl-demos` boots straight into LVGL's Benchmark demo, with no touch needed. Extra flags can be appended through `PLATFORMIO_BUILD_FLAGS`. Build each variant in its own directory (so they can be built in parallel), flash one, capture, and parse the summary table.

```powershell
$env:PLATFORMIO_BUILD_DIR   = "$env:TEMP\bench-v3"
$env:PLATFORMIO_BUILD_FLAGS = "-DB4848_LV_FAST_MEM -DB4848_LVGL_BUF_LINES=120"   # -O2 comes from the base ini (build_unflags = -Os)
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d projects\03-lvgl-demos -e bench -t upload --upload-port COM9
& $py -I serial_capture.py COM9 300 bench-v3.log --until "All scenes avg"
```

The benchmark prints a CSV-like block starting `Name, Avg. CPU`; a short parser reads every row up to `All scenes avg.` and compares variants as `FPS (CPU%) [render ms / flush ms]`.

**What it told us** (average over all scenes):

| Variant | FPS | CPU | Render / flush ms | Free internal at boot |
|---|---|---|---|---|
| v0 baseline (`-Os`) | 30 | 83% | 35 / 9 | 267 KB |
| v2 + `LV_ATTRIBUTE_FAST_MEM` | 31 | 84% | 32 / 9 | 212 KB |
| v3 + 120-line buffer | 33 | 82% | 24 / 9 | 143 KB |
| v4 + 2 draw threads | 31 | 100% | 31 / 9 | n/a |
| **v5 + real `-O2`** | **35** | 79% | 22 / 9 | 87 KB |
| v6/v7 double framebuffer | 20 / 14 | 92-95% | 40-59 / 10-12 | 314 KB |

- The larger draw buffer was the biggest single win (render -31%), then `-O2`, then IRAM.
- Two LVGL draw threads and double framebuffers were both slower, and the CPU reading is pinned at 100% for the threaded variant. These were recorded as measured defaults in `CLAUDE.md`.
- A full-screen flush costs about 20 ms (CPU copy of 460 KB into PSRAM). It sets the ceiling.

See [#inv-lvgl-performance](03-investigations.md#inv-perf-defaults) and the full table in [hardware-log](../hardware-log.md).

**Pitfalls.**

- `-O2` in `build_flags` alone gives a **byte-identical** binary, because the Arduino core appends `-Os` after it and the last wins. Add `build_unflags = -Os` too. This is why "v1" is missing from the table. Compare hashes of builds that should differ.
- The benchmark measures scenes, not your UI. Use D-04 and D-01 on the real screens before and after.
- `LV_ATTRIBUTE_FAST_MEM` and a big draw buffer cost internal RAM that mbedTLS needs (about 40-50 KB per session). Network projects must trade speed for RAM; project 05 uses a 48-line buffer and no fast memory.
- Single runs per variant; differences of 1-2 FPS are noise.

**Cost.** A 300 s capture plus a flash per variant. Variant builds can run in parallel.

---

## D-18 Flash backup and restore

**When.** Before the first upload to a new board, and before anything that rewrites the partition table.

**How.**

```powershell
.\tools\backup-flash.ps1 -Port COM9                                  # reads all 16 MB to firmware\factory\<mac>-<date>.bin
.\tools\restore-flash.ps1 -File firmware\factory\<file>.bin -Port COM9
```

`backup-flash.ps1` reads the MAC with esptool, then `read-flash 0 0x1000000` at 921600 baud (a few minutes) and prints the SHA-256. `restore-flash.ps1` writes the image at 0x0 after a confirmation prompt (`-Force` skips it).

**What it told us.** The backup recorded the facts the docs rely on: 16 MB flash (mfr 0x68, dev 0x4018) although the factory bootloader header says 8 MB, 8 MB octal PSRAM, and the factory partition layout (nvs, otadata, one 7.9 MB app0, coredump). It is the only way back to the shipped firmware.

**Pitfalls.**

- Back up before the first upload, not after.
- A flash dump can contain NVS data from earlier use, including Wi-Fi credentials, and the file name contains the MAC. Inspect it (or leave it out of a public repository) before publishing.
- Each image is 16 MB. The repo's `.gitignore` has an exception so backups can be committed on purpose; decide that per repository.

**Cost.** A few minutes once per board.

---

## D-19 Read the shipped source and config

**When.** A framework behaviour you cannot explain, a backtrace that ends in library code, or a memory question that depends on how the core was built.

**How.** The Arduino core and libraries are on disk under `~/.platformio`. Read them rather than guess, and tag what you learned by how sure you are.

```powershell
$pkg = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32"
Select-String -Path "$pkg\libraries\HTTPClient\src\HTTPClient.cpp" -Pattern 'writeToStreamDataBlock|while \(connected' -Context 2,6
# the authoritative build-time config for this env:
"$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\qio_opi\include\sdkconfig.h"
```

Evidence tags used in [12-esp32-performance-and-framework](../12-esp32-performance-and-framework.md): **[local]** read from the installed packages, **[web]** from a linked source, **[unverified]** plausible but not confirmed on this board.

**What it told us.**

- HTTPClient's `writeToStreamDataBlock` loop (`while (connected() && (len > 0 || len == -1))`) calls `connected()`, which calls `available()` on the TLS client, which does a read. It never yields while waiting, so it starves the idle task. This is the root cause in D-07.
- `sdkconfig.h` for `qio_opi` shows mbedTLS allocates **internal RAM only** (`MBEDTLS_MEM_ALLOC_MODE` internal; 16 KB in plus 16 KB out record buffers) and `SPIRAM_MALLOC_ALWAYSINTERNAL` is 4096. That explained the TLS cost in D-03 and justified a runtime `mbedtls_platform_set_calloc_free()` hook. The same read found that the Arduino Wi-Fi default `WiFi.persistent(true)` writes to NVS, which breaks the no-flash-writes rule.
- LVGL's own sources (for example `lv_anim.c`) were read the same way while the watchdog cause was still open.

**Pitfalls.**

- `esp32s3/sdkconfig` in the same package is a **different variant** (it says QUAD PSRAM). Read `qio_opi/include/sdkconfig.h`.
- The library at the faulting address might not be the version you are reading. Check the tag against the core version in `platformio.ini` (3.2.1 here).
- Do not edit these packages. Copy what you need into `common/` or a project. A patched `SD` library lives in `common/lib/SD` for this reason, with a `README-mini-screen.md` listing the changes to re-apply on upgrade.

**Cost.** Minutes, and often the shortest path to a root cause.

---

## D-20 Sub-agent workflow

**When.** Work splits cleanly into parts with their own files: a refactor across many screens, independent doc files, a research task alongside a code task.

**How.** What worked for the project 05 consolidation (four agents in parallel on 2026-10-11):

1. **Plan first, read-only.** A review agent wrote a consolidation plan with file and line evidence and a split into work packages with disjoint file ownership.
2. **Contracts first, committed.** The shared headers (`shared_state.h`, `mem_util.h`, `time_util.h`, `net_http.h`, UI helpers) were written and committed as `refactor-05` before any agent started, so every agent coded against the same APIs.
3. **Isolation.** Each implementation agent ran in its own git worktree on its own branch, with its own `PLATFORMIO_BUILD_DIR` and `TEMP` for builds and lint. Each owned a list of files and was told not to touch anything else or the board.
4. **Match model and effort to the task.** The network side (protocol and error handling) ran on the main model at high effort; the screen files and UI core ran on Sonnet at high effort against the written contract.
5. **Central integration.** The main session merged the branches, fixed the integration seams, built, linted, flashed and soaked. Agents never flashed the board.

Sample brief fragments that mattered: "You own ONLY these files", "do not run pio check / tools/lint.ps1 while another agent is reworking the lint config", "build with an isolated build dir", "do not kill processes", "KEEP every existing Serial log line format" (so soak comparisons stay valid).

**What it told us.** Four branches (network, UI core and main, screens A, screens B) were merged into `refactor-05` with no conflicts reported, which is what disjoint file ownership is for. The network agent also added the host tests (D-12), which is what let the merge proceed with confidence before any hardware time. The measured outcome was -13.4 KB of flash and 217 host checks.

**Lessons and pitfalls.**

- **Tool collisions.** An upload collided with an agent installing cppcheck (`FileExistsError: ... tool-cppcheck`), so a "flash and soak" ran on the old firmware (D-11) and one SD experiment was invalid. Serialise anything that may install packages; confirm each flash.
- **A `pio check` that never finishes.** Twice, `pio check` hung with no CPU use. It looked like another session's stuck process.
- **Do not kill by name.** One agent, cleaning up a hung run, stopped every process named `pio`, `python`, `cppcheck` or `clang-tidy`, which could have killed another session's flash. Tell agents to stop only processes they started.
- **One owner per device.** Two agents never publish to the same artifact or flash the same board. Capture and upload cannot share a COM port.
- **Shell heredocs corrupt C strings.** Python heredocs in Git Bash turned `\n` inside C string literals into real newlines, more than once. Agents that write C/C++ use the Edit tool and run lint on their own files (D-14).
- **Temporary state.** Use per-agent `TEMP` and build dirs for lint and builds so they do not share caches.
- **Review before relying.** Agent results are reported with confidence. The integration pass still found seams (a stale clock check, a lint finding, design-letter comments) and the screenshot pass (D-10) found layout bugs the agents' builds could not.
- **Do not split tightly coupled work.** If the pieces share a file or a lock ordering, one agent is cheaper than the coordination.
- **A report is not a change order.** When the user describes a symptom, give a diagnosis and options, and wait for a request before editing code or flashing.

**Cost.** Tokens, plus the time to write the contracts. It paid for itself on the 4-way refactor; it would not for a one-file change.

---

## D-21 RGB scan-out phase monitor

**When.** The panel image is shifted, wrapped or rolling while screenshots (D-10) look correct: the framebuffer is right and the RGB scan-out has lost its place. Use it to catch a drift without someone watching the screen, and to correlate it with what the firmware was doing.

**How.** `initDisplay()` registers two esp_lcd RGB panel callbacks (`board4848::lcdDiagAttach`): VSYNC, and bounce-frame-finish (the bounce-buffer ISR has filled the last chunk of a frame). Every frame it measures the time from VSYNC to that last refill. While the refill position and the scan-out agree, this phase is constant; a skipped or doubled refill moves it by one bounce buffer (rows x line time: 20 x 45.7 us = ~0.9 ms at 12 MHz PCLK) and it stays moved. Arduino_GFX keeps the panel handle private; the board library reads it with the explicit-instantiation access idiom (no layout assumptions).

- Heartbeat (every 10 s): `lcd: diag on, <vsyncs>, <refill frames>, phase <min>..<max> us (last ..), jumps N, restarts N`. Healthy unit 1 (2026-10-11): 42 vsyncs/s, phase ~21.35-21.94 ms, last ~21.41 ms.
- `[lcd] phase jump #n: a -> b us ... | screen, net phase, sd write age`: printed as soon as a frame-to-frame change exceeds `B4848_LCD_DIAG_JUMP_US` (default 300). Single ~0.4 ms jumps that return the next frame are ISR jitter; a drift is a move that stays (the window minimum moves).
- `k`: `esp_lcd_rgb_panel_restart()`; the driver restarts the scan-out DMA at the next VSYNC. Tests whether a live drift can be healed without a reboot.

**Pitfalls.** The bounce-frame-finish callback is deprecated in IDF 5.4 but still called; registering callbacks replaces any the library set (Arduino_GFX sets none). Keep the callbacks short and in IRAM.

**Cost.** Two ISR callbacks per frame and one heartbeat line.

---
## Combining methods: three worked chains

**Intermittent task watchdog reset.** D-08 (capture until it happens) -> D-07 (decode: `lwip_read` under `HTTPClient::connected()`) -> D-19 (read the loop) -> D-13 (curl shows which endpoints are chunked) -> fix -> D-12 (test framing at every split) -> D-11 (soak). [#inv-task-wdt-chunked-httpclient](03-investigations.md#inv-chunked-stall).

**Stutter on the main screen.** D-04 + D-01 (slow renders: 76-106 areas, about 300 ms) -> D-05 (all in the gateway poll phase) -> D-19 (read the rotated flush) -> fix in the board library -> D-11 (soak: 254-309 ms became 142-144 ms; check the flash first) -> D-10 (screenshots to confirm nothing changed visually). [#inv-main-screen-stutter](03-investigations.md#inv-stutter-rotated-flush).

**Internal RAM low point.** D-02 (boot deltas: net tasks -66,572) -> D-03 (Open-Meteo handshake sets min 92,136) -> D-06 (SD off: 110 KB versus 92 KB) -> D-19 (mbedTLS is internal-only) -> runtime PSRAM hook -> D-03 again (about 140-155 KB) -> D-04 and D-01 (crackle appears, threshold raised) -> D-11. [#inv-internal-ram-low-water](03-investigations.md#inv-internal-ram), [#inv-display-crackle](03-investigations.md#inv-crackle).

## Sources

Tools and documents referenced above.

- PlatformIO `pio check`: https://docs.platformio.org/en/latest/core/userguide/cmd_check.html
- PlatformIO compilation database: https://docs.platformio.org/en/latest/integration/compile_commands.html
- PlatformIO monitor filters (`esp32_exception_decoder`): https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
- pioarduino platform: https://github.com/pioarduino/platform-espressif32
- ESP-IDF fatal errors and Guru Meditation: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/fatal-errors.html
- ESP-IDF watchdogs: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/wdts.html
- ESP-IDF heap debugging (`heap_caps_get_minimum_free_size`): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/heap_debug.html
- ESP-IDF RGB LCD and bounce buffers: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- esptool: https://docs.espressif.com/projects/esptool/en/latest/
- GNU addr2line: https://sourceware.org/binutils/docs/binutils/addr2line.html
- Arduino-ESP32 3.2.1 HTTPClient source: https://github.com/espressif/arduino-esp32/blob/3.2.1/libraries/HTTPClient/src/HTTPClient.cpp
- LVGL benchmark demo: https://github.com/lvgl/lvgl/tree/master/demos/benchmark
- Arduino_GFX: https://github.com/moononournation/Arduino_GFX
- pyserial: https://pyserial.readthedocs.io/
- cppcheck: https://cppcheck.sourceforge.io/
- clang-tidy: https://clang.llvm.org/extra/clang-tidy/
- vswhere: https://github.com/microsoft/vswhere
- curl manual (`--range`, `-D`): https://curl.se/docs/manpage.html
- openssl s_client: https://docs.openssl.org/master/man1/openssl-s_client/
- SD Physical Layer Simplified Specification (CID, CSD, SCR registers, Stop Tran): https://www.sdcard.org/downloads/pls/
- Local companion documents: [07-gotchas](../07-gotchas.md), [11-space-weather-and-climate-data](../11-space-weather-and-climate-data.md), [12-esp32-performance-and-framework](../12-esp32-performance-and-framework.md), [hardware-log](../hardware-log.md)
