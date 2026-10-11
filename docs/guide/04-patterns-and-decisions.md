# Patterns and decisions

[Guide index](README.md) | [01 Quirks](01-quirks-and-gotchas.md) | [02 Debugging](02-debugging-playbook.md) | [03 Investigations](03-investigations.md) | [04 Patterns](04-patterns-and-decisions.md) | [05 New project](05-new-project-checklist.md) | [06 Hardware](06-hardware-and-sourcing.md) | [Credits](99-sources-and-credits.md)

The architecture patterns that survived on the Guition ESP32-4848S040 (ESP32-S3 N16R8, 480x480 ST7701 RGB panel, GT911, microSD, CH340C), with the reasoning behind each. Toolchain: PlatformIO with pioarduino `54.03.21-2` (Arduino core 3.2.1, IDF 5.4), LVGL 9.5.0, Arduino_GFX 1.6.9.

The patterns come mostly from project 05 (a network-connected LVGL dashboard), the largest and longest-running project here. They apply to any ESP32-S3 project with an RGB panel in PSRAM, Wi-Fi/TLS and LVGL. Starting a project: [05-new-project-checklist.md](05-new-project-checklist.md).

## How to read this

Each decision is a short ADR (architecture decision record):

| Field | Meaning |
|---|---|
| Context | The problem, and what was seen |
| Decision | What the code does now |
| Alternatives | What was considered or tried, and why not |
| Consequences | What it costs, what it constrains |
| Evidence | Where the reasoning or measurement lives |

Evidence links use two kinds of target. `03-investigations.md#inv-<slug>` is the write-up of a measurement or root-cause hunt. Plain `docs/...` and source paths are the repo files that hold the facts. Where a decision rests on reasoning only, it says so.

Status words follow the repo vocabulary (`idea` / `wip` / `builds` / `works on hw` / `done`). "Measured" means a number was recorded in [hardware-log.md](../hardware-log.md) or a serial log on one board (unit 1). Nothing here has been tested on a second unit.

### ADR index

| # | Decision | Area |
|---|---|---|
| [01](#adr-01-one-shared-board-library-one-project-per-folder) | One shared board library, one project per folder | Layout |
| [02](#adr-02-pins-live-in-one-header) | Pins live in one header | Layout |
| [03](#adr-03-arduino-core-on-pioarduino-with-pinned-versions) | Arduino core on pioarduino, versions pinned | Layout |
| [04](#adr-04-lvgl-runs-on-core-1-only) | LVGL runs on core 1 only | Tasks |
| [05](#adr-05-one-network-task-on-core-0-with-a-job-table) | One network task on core 0 with a job table | Tasks |
| [06](#adr-06-one-tls-session-at-a-time) | One TLS session at a time | Tasks |
| [07](#adr-07-background-tasks-run-at-idle-priority) | Background tasks run at idle priority | Tasks |
| [08](#adr-08-lvgls-heap-is-a-1-mb-psram-pool) | LVGL's heap is a 1 MB PSRAM pool | Memory |
| [09](#adr-09-internal-ram-is-a-budget-keep-about-100-kb-free) | Internal RAM is a budget: keep about 100 KB free | Memory |
| [10](#adr-10-bodies-json-and-images-live-in-psram) | Bodies, JSON and images live in PSRAM | Memory |
| [11](#adr-11-mbedtls-allocator-hook-with-a-4-kb-threshold) | mbedTLS allocator hook with a 4 KB threshold | Memory |
| [12](#adr-12-static-init-mutexes-and-sharedt) | Static-init mutexes and `Shared<T>` | Memory |
| [13](#adr-13-bounce-buffers-are-always-on) | Bounce buffers are always on | Display |
| [14](#adr-14-own-rotation-3-flush-path) | Own rotation-3 flush path | Display |
| [15](#adr-15-partial-rendering-with-a-small-internal-ram-buffer) | Partial rendering with a small internal-RAM buffer | Display |
| [16](#adr-16--o2-yes-lv_attribute_fast_mem-not-in-network-projects) | `-O2` yes, `LV_ATTRIBUTE_FAST_MEM` not in network projects | Display |
| [17](#adr-17-single-framebuffer-and-one-draw-thread) | Single framebuffer and one draw thread | Display |
| [18](#adr-18-change-only-setters) | Change-only setters | Display |
| [19](#adr-19-one-barchart-object-per-chart) | One `BarChart` object per chart | Display |
| [20](#adr-20-entry-animations-start-after-the-slide) | Entry animations start after the slide | Display |
| [21](#adr-21-avoid-clip_corner-and-other-layer-forcing-styles) | Avoid `clip_corner` and other layer-forcing styles | Display |
| [22](#adr-22-one-http-module-netsession-with-its-own-de-chunker) | One HTTP module (`net::Session`) with its own de-chunker | Network |
| [23](#adr-23-http10-for-one-shot-gets-keep-alive-is-opt-in) | HTTP/1.0 for one-shot GETs; keep-alive is opt-in | Network |
| [24](#adr-24-job-budget-below-the-staleness-limit) | Job budget below the staleness limit | Network |
| [25](#adr-25-pinned-root-cas-per-host) | Pinned root CAs per host | Network |
| [26](#adr-26-identifying-user-agent-and-polite-intervals) | Identifying User-Agent and polite intervals | Network |
| [27](#adr-27-background-jobs-wait-for-a-valid-clock) | Background jobs wait for a valid clock | Network |
| [28](#adr-28-no-internal-flash-writes-at-runtime) | No internal-flash writes at runtime | Storage |
| [29](#adr-29-sdstore-owns-the-sd-bus-and-never-formats) | SdStore owns the SD bus and never formats | Storage |
| [30](#adr-30-the-sd-clock-is-tuned-per-card-and-remembered-on-the-card) | The SD clock is tuned per card and remembered on the card | Storage |
| [31](#adr-31-atomic-crc-writes-for-state-that-must-survive) | Atomic CRC writes for state that must survive | Storage |
| [32](#adr-32-hot-swap-by-polling) | Hot-swap by polling | Storage |
| [33](#adr-33-a-patched-sd-library) | A patched SD library | Storage |
| [34](#adr-34-sd-cache-of-fetched-data) | SD cache of fetched data | Storage |
| [35](#adr-35-day-history-month-files-with-fixed-slots-and-per-slot-crc) | Day history: month files with fixed slots and per-slot CRC | Storage |
| [36](#adr-36-release-pipeline-and-browser-flashing) | Release pipeline and browser flashing | Tooling |

Sections after the ADRs cover [UI conventions](#ui-conventions), [code conventions](#code-conventions), [repository layout](#repository-and-project-layout) and [open points](#open-points).

---

## Layout and toolchain

### ADR-01: One shared board library, one project per folder

- **Context.** Several projects target the same board. Each needs the same pin map, ST7701 init, GT911 driver and LVGL glue.
- **Decision.** `common/lib/Board4848S040` holds all board knowledge (`board4848` namespace). `common/platformio-base.ini` holds the shared PlatformIO environment `[base_4848s040]`. Each project lives in `projects/NN-name/`, extends the base with `extends = base_4848s040` and pulls the library with `symlink://../../common/lib/Board4848S040`. Other shared code (`SdStore`, patched `SD`) sits next to it in `common/lib/`.
- **Alternatives.** Copy the board code into every project: fixes would have to be repeated. A published PlatformIO library: slower iteration, and the board code is not general enough to publish. A vendor framework such as esp32-smartdisplay: fastest start, but it hides the init and flush paths that this repo needed to change (see ADR-14).
- **Consequences.** A change to the library affects every project, so performance-sensitive changes are benchmarked first (the `03-lvgl-demos` `bench` env) and `tools/build-all.ps1` builds all projects before a commit. Projects cannot be moved out of the repo without copying `common/`.
- **Evidence.** [projects/README.md](../../projects/README.md), [06-frameworks-and-libraries.md](../06-frameworks-and-libraries.md), `common/platformio-base.ini`.

### ADR-02: Pins live in one header

- **Context.** The board has shared pins: IO1/IO2/IO40 serve relays and I2S audio (selected by 0 ohm links), the SD card shares SCK/MOSI with the LCD init bus, and GPIO0/3/45/46 are strapping pins used by the LCD and touch. Community sources disagree in places.
- **Decision.** Every pin is defined once, in `Board4848S040_pins.h`, and nowhere else. The vendor schematic is the authority; community configs are cross-checks. Application code uses the macros (`B4848_*`), never numbers.
- **Alternatives.** Per-project pin lists: they drift apart, and a wrong pin can drive a strapping pin or a relay.
- **Consequences.** A pin question has one answer. Porting to another board means replacing this header (see the porting section of the checklist). Adding load to a strapping pin (touch SCL on GPIO45) is a hardware risk and is called out in the header and in [07-gotchas.md](../07-gotchas.md).
- **Evidence.** [02-pinout.md](../02-pinout.md), `common/lib/Board4848S040/src/Board4848S040_pins.h`.

### ADR-03: Arduino core on pioarduino, with pinned versions

- **Context.** The official `espressif32` PlatformIO platform ships Arduino core 2.x. Arduino_GFX 1.6.x needs core 3.x. Newer pioarduino releases than the pinned one need PlatformIO Core 6.2 or later.
- **Decision.** The base ini pins pioarduino `54.03.21-2` (core 3.2.1, IDF 5.4). Arduino_GFX is pinned to 1.6.9 in the library's `library.json` only. LVGL is pinned to an exact version per project (9.5.0), and so is ArduinoJson. `memory_type = qio_opi` and `ARDUINO_USB_CDC_ON_BOOT=0` stay (octal PSRAM; the console is the CH340C on UART0 because the native USB pins are used by touch and the LCD).
- **Alternatives.** Unpinned latest versions: an Arduino_GFX change once broke this exact board between releases for another user. ESP-IDF or a hybrid build: not rejected, only not needed yet. [06](../06-frameworks-and-libraries.md) and [12](../12-esp32-performance-and-framework.md) set out the trade-offs and the triggers for moving. **Arduino versus ESP-IDF is documented as reasoning, not as a decision.** Do not present it as decided.
- **Consequences.** The core's sdkconfig is fixed, so memory behaviour is tuned with runtime hooks (ADR-11) instead of sdkconfig edits. `custom_sdkconfig` is avoided because it rewrites the shared global framework package and would thrash between projects.
- **Evidence.** [12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md) sections 4 and 5, [07-gotchas.md](../07-gotchas.md).

## Tasks and cores

### ADR-04: LVGL runs on core 1 only

- **Context.** LVGL 9 is not thread-safe without an OS lock. Wi-Fi, TLS and SD work block for long periods.
- **Decision.** `loop()` (Arduino's loop task, core 1) owns LVGL: `lv_timer_handler()`, every widget call, the refresh timer, serial commands. Nothing else calls LVGL. `LV_USE_OS` is `LV_OS_NONE`. Other tasks publish data and the UI copies it out (ADR-12). The loop task stack is raised with `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`; the default 8 KB ran under 1 KB free and once overflowed after LVGL's JPEG decoder was registered.
- **Alternatives.** A dedicated LVGL task with `lv_lock()`: more moving parts for no gain here. LVGL's own multi-thread draw (two draw units): measured slower (ADR-17).
- **Consequences.** Anything slow must happen on the other core. UI code never takes a long lock: it copies a snapshot under a mutex and releases at once. A fetched image or large buffer must be fully built before it is swapped in for the UI to read.
- **Evidence.** `projects/05-powerwall-display/src/main.cpp`, `include/lv_conf.h`.

### ADR-05: One network task on core 0 with a job table

- **Context.** Project 05 polls a LAN gateway every 2 s and also fetches weather, space weather, aurora, moon times, a sun image and an ENSO report from the internet. It started as two tasks (gateway, weather) serialised by a lock.
- **Decision.** One task (`pw_net`, core 0, 12 KB stack). Each pass polls the gateway, then runs at most one due background job from a `JOBS[]` table. Each row has a name, a function, a TLS flag, a period after success, a retry period after failure, optional exponential back-off, and an optional `needed()` gate. The table order is the priority order.
- **Alternatives.** One task per data source: each needs its own stack (about 10 KB of scarce internal RAM each) and a lock to stop concurrent TLS anyway. An if/else chain: replaced because it hid the timings and gave no per-job log name.
- **Consequences.** Adding a source is one row plus a fetch function. A slow job delays gateway polling, so jobs have a time budget (ADR-24). Back-off state lives in the table, not in each fetcher. The merge freed one 10 KB internal-RAM stack.
- **Evidence.** `powerwall.cpp` (`JOBS[]`, `runOneJob`), [12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md) section 0 item 4. Review round: see `inv-job-budget` in [03-investigations.md](03-investigations.md#inv-job-budget).

### ADR-06: One TLS session at a time

- **Context.** An mbedTLS session needs about 40 to 50 KB of internal RAM in the stock core (two 16 KB record buffers plus handshake state). `SSL - Memory allocation failed` (-32512) appears when the heap or its largest free block is too small.
- **Decision.** The task structure guarantees it. The gateway's long-lived TLS client is closed (`stop()`) before any TLS job starts, and every job opens and closes its own `net::Session`. The task logs `[tls] <job>: ... min ever` so the low-water mark of each job is visible.
- **Alternatives.** Concurrent sessions with a bigger RAM budget: not affordable beside the LVGL buffer and bounce buffers. Smaller TLS buffers through a custom sdkconfig: possible later (ADR-03), see doc 12 section 4b.
- **Consequences.** Total TLS cost is one session wide, whatever the number of sources. The price is latency: sources wait their turn.
- **Evidence.** [07-gotchas.md](../07-gotchas.md) (`SSL - Memory allocation failed`), [12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md), `powerwall.cpp` (`runTls`). Write-up: [inv-tls-memory](03-investigations.md#inv-tls-memory).

### ADR-07: Background tasks run at idle priority

- **Context.** The task watchdog reset the board (`task_wdt: IDLE0`, backtrace in `HTTPClient::getString` -> `writeToStreamDataBlock` -> `readBytes`). Arduino's `Stream::readBytes` busy-waits without yielding until its timeout, which starves core 0's idle task.
- **Decision.** The network and SD tasks are created at `tskIDLE_PRIORITY` (0), pinned to core 0. At equal priority they time-slice with the idle task, so the idle task always runs and the watchdog is fed. Waiting loops also yield (`delay(2)`) and have hard deadlines (ADR-22).
- **Alternatives.** Raising the watchdog timeout or removing core 0's idle task from the watchdog: hides the starvation instead of removing it. Fixing every library's busy-wait: not possible.
- **Consequences.** Background work gets whatever CPU the idle task leaves, which is plenty on core 0 because LVGL is on core 1. A priority above 0 for these tasks would bring the watchdog problem back.
- **Evidence.** [07-gotchas.md](../07-gotchas.md) (last row), `powerwall.cpp` (`powerwallStart`). Write-up: [inv-httpclient-chunked-stall](03-investigations.md#inv-httpclient-chunked-stall).

---

## Memory strategy

Three memories matter: internal RAM (small, needed by Wi-Fi, TLS, DMA, ISRs and task stacks), PSRAM (8 MB octal, shared with the LCD scan-out), and RTC memory (survives soft resets).

### ADR-08: LVGL's heap is a 1 MB PSRAM pool

- **Context.** With `LV_STDLIB_CLIB`, LVGL uses `malloc`. The core's `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` sends every allocation under 4 KB to internal RAM first, which means every widget. Four screens left about 38 KB for TLS.
- **Decision.** `LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN`, `LV_MEM_SIZE (1024*1024U)` and `LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. LVGL's pool is allocated once, in PSRAM. `LV_USE_STDLIB_STRING` stays `CLIB`.
- **Alternatives.** The C heap (kept in `03-lvgl-demos`, a project without Wi-Fi). A static internal pool (`02-lvgl-hello` uses 48 KB): fine for a handful of widgets, not for ten or more screens (11 now).
- **Consequences.** Widgets cost PSRAM, not internal RAM. LVGL's render and tree walks run against PSRAM, so object count still matters (ADR-19). A serial run with ten screens (the count then; 11 now) showed the pool about 16 percent used. The `m` command prints the pool state.
- **Evidence.** [07-gotchas.md](../07-gotchas.md) ("Internal RAM drains as you add LVGL screens"), `projects/05-powerwall-display/include/lv_conf.h`. Write-up: [inv-lvgl-heap-psram](03-investigations.md#inv-lvgl-heap-psram).

### ADR-09: Internal RAM is a budget: keep about 100 KB free

- **Context.** Internal RAM is consumed by the LVGL draw buffer, bounce buffers, task stacks, Wi-Fi static buffers, ISR-safe code and one TLS session at a time. When a transient TLS peak finds too little, the handshake fails or the system misbehaves.
- **Decision.** Treat about 100 KB of internal RAM free (low-water mark, "min ever") as the floor, and track it. Boot logs `[mem]` after each init step. The 10 s heartbeat and the `m` command print free, largest block, min ever and stack headroom. Every TLS job logs its low-water mark. Size the LVGL draw buffer last, from what is left.
- **Alternatives.** Hard-coded limits from first principles: the numbers moved by 40 KB between commits for reasons nobody predicted (see consequences). Measuring beats estimating.
- **Consequences.** Typical figures (unit 1): the 05 budget at boot was about 188 KB free and about 140 KB while polling in the early build. Growing to ten screens (11 now) plus an SD task took free internal RAM from about 161 KB to about 121 KB (doc 12), and the low point to about 92 KB before ADR-11. A 10 minute serial run after ADR-11 showed a minimum of about 115 KB and 0 failures. Largest free block matters as much as the total: a 60 KB block is enough for a session, a 20 KB one is not.
- **Evidence.** [12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md) section 2 (the budget table), [hardware-log.md](../hardware-log.md). Write-up: [inv-internal-ram-budget](03-investigations.md#inv-internal-ram-budget).

### ADR-10: Bodies, JSON and images live in PSRAM

- **Context.** Arduino `String` bodies under 4 KB are `malloc`ed in internal RAM (`ALWAYSINTERNAL=4096`). A poll every 2 s with several small bodies churns and fragments internal RAM. ArduinoJson pool blocks under 4 KB behave the same.
- **Decision.** `mem_util.h` provides `psramAlloc`, `internalAlloc`, an RAII `HeapBuf`, `allocCanvas` (64-byte aligned RGB565 canvas) and a PSRAM `ArduinoJson::Allocator` (`psramJson()`). HTTP bodies are read into a reusable PSRAM buffer (`net::Session::get`, the gateway's static 16 KB buffer) and parsed in place. Images (sun photo, moon canvas) are PSRAM canvases.
- **Alternatives.** Lowering the global malloc threshold with `heap_caps_malloc_extmem_enable(512)`: proposed in doc 12, not adopted, because any library that DMAs from a plain `malloc` buffer would break. Not measured here.
- **Consequences.** Internal RAM is kept for what needs it. Small PSRAM accesses add traffic that the LCD also uses, which is why the TLS hook has a threshold (ADR-11).
- **Evidence.** `mem_util.h`, [12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md) section 4a, `projects/05-powerwall-display/src/net_http.cpp`.

### ADR-11: mbedTLS allocator hook with a 4 KB threshold

- **Context.** The core's mbedTLS allocates only from internal RAM (`MBEDTLS_MEM_ALLOC_MODE` internal). The first version of the fix sent all blocks of 1 KB and above to PSRAM. Internal RAM then recovered, but the gateway poll does a full handshake every 2 s, and its many small big-number blocks in PSRAM produced constant screen crackle once it started.
- **Decision.** `mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree)` is installed in `powerwallStart()`, before any TLS session. Blocks of 4 KB or more (mainly the two 16 KB record buffers) go to PSRAM; smaller blocks go to internal RAM; each falls back to the other if full.
- **Alternatives.** Stock behaviour (everything internal): lowest point was about 92 KB. A 1 KB threshold: crackle. `MBEDTLS_EXTERNAL_MEM_ALLOC` through a custom sdkconfig: same effect, but a framework rebuild (ADR-03). Smaller TLS buffers through sdkconfig (asymmetric content length, dynamic buffers): not tried; listed in doc 12 section 4b.
- **Consequences.** Most of the saving is kept without the PSRAM traffic. A caveat from doc 12: PSRAM-resident TLS buffers and hardware AES/SHA DMA have one hang report, and the repo records no long soak of the 4 KB version (only a 10 minute serial run). The owner confirmed visually on 2026-10-11 that the crackle is gone ("screen looks good now") with this, the 2x bounce buffers, the rotated flush and gateway keep-alive together; which change did the most is not isolated. A long soak (several hours) for leaks and fragmentation is still planned; soak it before relying on it in a new project.
- **Evidence.** `powerwall.cpp` (`tlsCalloc`), commit `300e7b5` message, [12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md) sections 4a and 6. Write-up: [inv-crackle-psram](03-investigations.md#inv-crackle-psram).

### ADR-12: Static-init mutexes and `Shared<T>`

- **Context.** Five different lock idioms had grown (`xSemaphoreTake` with and without guards, manual take/give, `if (!s_lock)` guards). The SD task read `powerwallData()` before `powerwallStart()` created the lock.
- **Decision.** `shared_state.h` provides `Mutex` (a FreeRTOS mutex created in its constructor), `LockGuard` (RAII) and `Shared<T>` (a value copied in and out under its own mutex, plus `with(f)` for read-modify-write). They are namespace-scope objects, so the mutex exists from static initialisation, before any task starts. `isStale(valid, atMs, maxAgeMs)` is the one staleness test.
- **Alternatives.** Creating mutexes in `begin()` functions (the old pattern): order-of-call bugs. Lock-free double buffers: more code for data that changes every 2 s.
- **Consequences.** Data crosses cores by copy, so the structs must be small and plain. `with()` must not call back into the same object (deadlock); no code does.
- **Evidence.** `shared_state.h`, [consolidation notes in 03-investigations](03-investigations.md#inv-shared-modules).

---

## Display pipeline

### ADR-13: Bounce buffers are always on

- **Context.** The RGB panel is scanned out of a 460 KB framebuffer in PSRAM. When the CPU writes PSRAM heavily, the LCD DMA misses data. Without bounce buffers the whole screen flickered while finger-painting (verified on unit 1).
- **Decision.** `initDisplay()` enables bounce buffers by default: 480x10 pixels (about 19 KB of internal RAM for two). Project 05 uses 480x20 (about 38 KB) because 480x10 crackled while TLS and JPEG work also used PSRAM. The size must divide the pixel count (480x480).
- **Alternatives.** Lower PCLK (12 MHz is already low). Zero front/back porch timings: tried only by others. XIP from PSRAM and a 64-byte cache line: needs a custom sdkconfig and a benchmark (doc 12). Not tried here.
- **Consequences.** About 19 to 38 KB less internal RAM. Residual risk: a flash write stalls PSRAM, so runtime flash writes are avoided anyway (ADR-28).
- **Evidence.** [04-display-st7701.md](../04-display-st7701.md), [hardware-log.md](../hardware-log.md) ("Flicker", "Rotated flush"), [Espressif LCD FAQ](https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html). Write-up: [inv-bounce-buffers](03-investigations.md#inv-bounce-buffers).

### ADR-14: Own rotation-3 flush path

- **Context.** Rotation 3 puts the USB-C port at the bottom. Arduino_GFX rotates in software: its `draw16bitRGBBitmap` wrote each LVGL strip column by column (each pixel on a different 960-byte framebuffer row, so the PSRAM cache thrashed) and then wrote back every full framebuffer row it touched. For a 480x48 strip that is about 460 KB of cache write-back for 46 KB of change.
- **Decision.** `Board4848S040_lvgl.cpp` has `flushRot3`: logical pixel (x, y) lives at framebuffer row `W-1-x`, column `y`. Each framebuffer row gets one contiguous segment, and only that segment is written back with `Cache_WriteBack_Addr`. Other rotations and out-of-range areas fall back to `draw16bitRGBBitmap`.
- **Alternatives.** LVGL's own display rotation (`lv_display_set_rotation`): rotates inside LVGL at CPU cost. Changing Arduino_GFX (a pinned dependency). Matching the panel orientation in the init sequence: not available on an RGB panel.
- **Consequences.** Measured on 05: a full-screen redraw dropped from 254 ms to 142 ms, and many-area redraws (about 300 ms, visible stutter) to about 145 to 185 ms. The code assumes a square panel (width used for both framebuffer dimensions). `auto_flush` is still on; the path bypasses it for the rotated case. A further saving (turn `auto_flush` off and write back exactly) is not done.
- **Evidence.** [hardware-log.md](../hardware-log.md) ("Rotated flush"), `common/lib/Board4848S040/src/Board4848S040_lvgl.cpp`. Write-up: [inv-rotated-flush](03-investigations.md#inv-rotated-flush).

### ADR-15: Partial rendering with a small internal-RAM buffer

- **Context.** LVGL renders into a draw buffer, then flushes areas to the framebuffer. Rendering straight into PSRAM is much slower than into internal RAM.
- **Decision.** `lvglBegin(gfx, touch, bufLines)` uses `LV_DISPLAY_RENDER_MODE_PARTIAL` with a `bufLines`-line buffer in internal RAM (480 x lines x 2 bytes). The default is 120 lines (115 KB), which is the best speed for a project without Wi-Fi. Network projects pass `-DB4848_LVGL_BUF_LINES=48` (46 KB) to leave room for TLS (ADR-09).
- **Alternatives.** FULL or DIRECT mode with two PSRAM framebuffers (ADR-17). A buffer in PSRAM when internal is full: the library falls back to it, but it is slower.
- **Consequences.** In the benchmark the 120-line buffer was the single biggest win (average render time down 31 percent compared with 48 lines). A network project therefore pays that cost for RAM. If a project has room, raise the line count; if not, lower it, and benchmark (the `bench` env).
- **Evidence.** [hardware-log.md](../hardware-log.md) (LVGL performance experiments, v3 column), `Board4848S040_lvgl.h`. Write-up: [inv-lvgl-perf-defaults](03-investigations.md#inv-lvgl-perf-defaults).

### ADR-16: `-O2` yes, `LV_ATTRIBUTE_FAST_MEM` not in network projects

- **Context.** The Arduino core adds `-Os` after `build_flags`, and the last `-O` wins, so `-O2` in `build_flags` alone produced a byte-identical firmware. `LV_ATTRIBUTE_FAST_MEM=IRAM_ATTR` puts LVGL's hot functions in IRAM, which costs about 55 KB of internal RAM.
- **Decision.** The base ini sets `build_unflags = -Os` and `-O2` (measured about 10 percent faster rendering). Projects without TLS (02, 03) use `LV_ATTRIBUTE_FAST_MEM`. Project 05 does not.
- **Alternatives.** Keep `-Os` (smaller, slower). IRAM everywhere (the 05 TLS headroom was needed more).
- **Consequences.** The IRAM saving for network projects was decided while mbedTLS still allocated only internally. With the allocator hook (ADR-11) the pressure is different. The comment in `lv_conf.h` still carries the old reasoning, and the trade-off has **not been re-measured**. Treat "FAST_MEM off in network projects" as a default to re-test, not as a proven requirement.
- **Evidence.** [hardware-log.md](../hardware-log.md) (v2 and v5 columns, free-heap table), [07-gotchas.md](../07-gotchas.md) ("`-O2` in `build_flags` has no effect"), `common/platformio-base.ini`. Write-up: [inv-lvgl-perf-defaults](03-investigations.md#inv-lvgl-perf-defaults).

### ADR-17: Single framebuffer and one draw thread

- **Context.** Two ideas to speed up rendering: a second PSRAM framebuffer swapped on vsync (no copy, no tearing), and LVGL's second draw thread.
- **Decision.** Neither is the default. Project 04 keeps the double-buffered path as an opt-in experiment (`lvglBeginDoubleBuffered`).
- **Alternatives.** Both were built and benchmarked on unit 1 (see consequences).
- **Consequences.** Two draw threads: average 31 FPS against 33 with the same other settings, CPU pinned at 100 percent (threading overhead outweighs the second core). Double framebuffer: 20 FPS (direct) and 14 FPS (full) against 35 for the single-buffer defaults, because rendering into PSRAM is much slower and each frame waits up to a refresh (about 24 ms at 42 Hz). The swap itself works. Choose it only for a UI where tearing matters more than speed. Untried: asynchronous DMA copy for the flush, and `esp_lvgl_port` SIMD blends.
- **Evidence.** [hardware-log.md](../hardware-log.md), [04-lvgl-fb2-bench README](../../projects/04-lvgl-fb2-bench/README.md). Write-up: [inv-double-framebuffer](03-investigations.md#inv-double-framebuffer).

### ADR-18: Change-only setters

- **Context.** Setting a label or colour to the value it already has still invalidates the object, which forces a redraw and makes animations judder.
- **Decision.** All value updates go through `ui::setText`, `setTextF`, `setTextColor`, `setBgColor`, `setHidden`, `setWidth/Height/X`, `hbarSet` and `setValueF`, which compare before writing. Screens whose inputs rarely change also use `ui::ChangeGate<K>` to skip the whole refresh. The 500 ms refresh timer is therefore cheap, and the screen's idle redraw load is dominated by running animations only.
- **Alternatives.** Calling `lv_label_set_text` every refresh: LVGL does compare strings, but colour, size and position setters do not all do so cheaply.
- **Consequences.** A new screen must use the helpers to stay quiet. The heartbeat prints `redraw: <px/s> px/s, <n> flushes/s`, the number to compare when changing idle animations.
- **Evidence.** `ui_common.h`, `ui_common.cpp`, consolidation notes ([inv-redraw-load](03-investigations.md#inv-redraw-load)).

### ADR-19: One `BarChart` object per chart

- **Context.** Bar strips built from `lv_obj` rows cost a widget each: about 120 objects across four screens. Every object is PSRAM-pool memory and is walked on each render.
- **Decision.** `ui::barChartCreate/Set/SetNewest/Grow` implement one object that draws all its bars in `LV_EVENT_DRAW_MAIN` with `lv_draw_rect`. Changing bar i invalidates only that bar's area.
- **Alternatives.** Keep `lv_obj` bars: simple but heavy. LVGL's `lv_chart`: a bar chart with axes and series that is bigger than needed.
- **Consequences.** About 116 fewer objects and smaller invalidation areas. The custom draw code is LVGL-version specific, so re-check it when LVGL changes.
- **Evidence.** `ui_chart.h`, `ui_chart.cpp`. Write-up: [inv-redraw-load](03-investigations.md#inv-redraw-load).

### ADR-20: Entry animations start after the slide

- **Context.** A screen change slides the new screen in over 300 ms, which redraws the full screen every frame. Fades that start at the same moment re-render whole cards on top.
- **Decision.** `gotoScreen` loads the screen with `lv_screen_load_anim(..., 300 ms)` and calls `onShow()` with `ui::setShowDelay(300)`, so fades, value animations and bar grows take their start state immediately (no pop) but only begin when the slide ends. `-DUI_FADE_AFTER_SLIDE=0` restores the old behaviour for comparison. Animations of hidden screens are paused (`pauseScreenAnimations`).
- **Alternatives.** No entry animation: simplest, but loses the polish. Starting from the slide's ready callback: equivalent; a fixed delay was simpler.
- **Consequences.** Less render load during the slide. One constant (`SLIDE_MS`) drives both the slide and the delay, so they stay tied.
- **Evidence.** `main.cpp` (`gotoScreen`), `ui_common.h`. Write-up: [inv-redraw-load](03-investigations.md#inv-redraw-load). Whether it removed visible stutter on its own was not isolated from the other changes in the same round.

### ADR-21: Avoid `clip_corner` and other layer-forcing styles

- **Context.** In LVGL 9.5, plain `opa` is applied per draw call and needs no layer. Only `opa_layered`, transforms, blend modes and `clip_corner` do. `clip_corner` draws children into corner layers whose buffers come from the LVGL pool, which is in PSRAM. A redraw then allocates and fills PSRAM, and an animated child (a charging sheen) did it every frame.
- **Decision.** Do not use `clip_corner` on objects that redraw often. The health bar's sheen has its own radius and is clamped to the fill. The sun photo's corners are painted in the background colour into the image itself, so its holder needs no clip. One low-priority use remains on the ENSO diverging bar.
- **Alternatives.** Keep the style: identical look, higher cost. The benchmark shows the order of magnitude: "containers with opa_layer" ran at 10 to 21 FPS against 54 for "containers with opa".
- **Consequences.** The look must be reproduced by geometry or by painting the pixels. This is a design constraint for new screens: check any new style against the layer list before using it.
- **Evidence.** [hardware-log.md](../hardware-log.md) (benchmark rows), LVGL 9.5 source (`lv_obj_style.c`, `lv_refr.c`) as reviewed in the consolidation notes. Write-up: [inv-redraw-load](03-investigations.md#inv-redraw-load). The effect of removing it on a real display was not benchmarked separately.

---

## Networking

### ADR-22: One HTTP module (`net::Session`) with its own de-chunker

- **Context.** Four copies of the HTTP client code existed, with three different body readers and four sets of timeouts and User-Agent strings. `HTTPClient`'s chunked reader on a kept-alive TLS connection once stalled in a loop that never yielded (ADR-07).
- **Decision.** `net_http.h` is the one place for background fetches. `net::Session` is configured by `net::Options` (tag, pinned CA, keep-alive, connect/read/handshake timeouts, body deadline, session budget). `get()` reads into a buffer, `stream()` pushes through a sink. Bodies are read by Content-Length, de-chunked by `net::Dechunker` (never by `HTTPClient`) or read until close. The reader yields while waiting and obeys a hard deadline. `net_body.h` is plain C++ with no Arduino types, so it is tested on the PC. Errors are logged in one shape: `<tag>: <path> -> HTTP n | error`.
- **Alternatives.** `HTTPClient::getString()`: de-chunks, but is the busy-wait source. `esp_http_client`: not evaluated. A cleaner third-party client: no obvious candidate for Arduino.
- **Consequences.** One place to fix transport bugs and to add counters (`net::stats()` gives TLS connects, requests and failures in the heartbeat). The de-chunker is code that must be right; it is tested with every split offset of each sample.
- **Evidence.** `net_http.h`, `net_http.cpp`, `net_body.h`, `projects/05-powerwall-display/hosttest/README.md`, [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112) (framing). Write-up: [inv-httpclient-chunked-stall](03-investigations.md#inv-httpclient-chunked-stall).

### ADR-23: HTTP/1.0 for one-shot GETs; keep-alive is opt-in

- **Context.** HTTP/1.1 servers answer with chunked bodies, which need a correct chunk reader. HTTP/1.0 gets a plain body that ends at Content-Length or when the server closes. The gateway's `/aggregates` is chunked under HTTP/1.1, and a handshake per poll is expensive.
- **Decision.** Every one-shot fetch uses HTTP/1.0 with connection close (`Options::keepAlive = false`). Keep-alive is opt-in per session: only the ENSO job uses it today (several requests to one host in one job). The gateway poll defaults to HTTP/1.0 (a handshake per 2 s poll); `PW_GW_KEEPALIVE=1` switches to HTTP/1.1 reuse with `net::readBody`. **Update 2026-10-11 (`c9a6630`): keep-alive is now the default** after a 10-minute soak: 22 TLS connects instead of 275, 1 handshake refusal instead of 19, 59 fps, no watchdog resets. `-DPW_GW_KEEPALIVE=0` restores the one-shot path.
- **Alternatives.** Keep-alive everywhere to save handshakes: higher reward but the stall risk (ADR-07). Persistent connection with a custom parser: that is the opt-in path.
- **Consequences.** With the old one-shot default, a 10 minute serial run counted about 270 TLS connections and 0 background-fetch failures (gateway polls: 19 refusals); with keep-alive now the default it is about 22. Each handshake is a burst of CPU and internal-RAM churn, which is the cost kept in exchange for robustness. The keep-alive default has had a 10-minute soak, not yet a long one (several hours: projects/README.md follow-up).
- **Evidence.** `powerwall.cpp` (`PW_GW_KEEPALIVE`), `powerwall.h`, [07-gotchas.md](../07-gotchas.md) ("JSON from an HTTP API parses as a number"). Write-up: [inv-httpclient-chunked-stall](03-investigations.md#inv-httpclient-chunked-stall).

### ADR-24: Job budget below the staleness limit

- **Context.** Live data turns to `--` after `PW_STALE_MS` (20 s). One job can pause gateway polling. A review found jobs that could exceed 20 s: the sun image had a 25 s deadline plus connect and handshake, and the ENSO job made five requests with their own timeouts.
- **Decision.** Every job runs inside `Options::budgetMs = PW_JOB_BUDGET_MS` (12 s). When the budget is spent, remaining requests return `Skipped` and are retried next period; each phase of a request in flight is clamped to what is left. A `static_assert` ties the numbers: budget + 2 s poll + 3 s overrun and reconnect must not exceed the staleness limit.
- **Alternatives.** Raise the staleness limit: the display would show out-of-date power for longer. A second task for slow jobs: needs a second TLS budget (ADR-06).
- **Consequences.** Partial results are normal and handled (ENSO merges what it got; SWPC keeps old values unless both scales and flares arrived). A job can overrun by about one request's phase floors.
- **Evidence.** `powerwall.h` (the `static_assert`), `net_http.h`. Write-up: [inv-job-budget](03-investigations.md#inv-job-budget).

### ADR-25: Pinned root CAs per host

- **Context.** Public hosts must be authenticated. The gateway uses a self-signed certificate on the LAN.
- **Decision.** Each background host uses a pinned **root** CA in `ca_certs.h` (`inline constexpr`, one copy in the image): Let's Encrypt roots (weather, sun image), Amazon Root CA 1 (space weather), HARICA (moon times), Go Daddy G2 (ENSO), each with its expiry noted. The LAN gateway uses `setInsecure()` and stays on the LAN only. A pinned root fails closed when the host changes issuer, and the failure shows in the log.
- **Alternatives.** `setInsecure()` for everything: no authentication of public data (it does not carry credentials in this project, but integrity still matters). Pinning the leaf: rotates every few months. The full Mozilla bundle through `setCACertBundle`: not evaluated; flash and RAM cost were not measured.
- **Consequences.** Roots are valid for years (2035 to 2045 here) but not forever; a calendar reminder or a failure log line is the renewal trigger. Adding a source means fetching and checking its root. TLS validity needs a valid clock (ADR-27).
- **Evidence.** `ca_certs.h`; pinning arrived with the cold-review fixes of 2026-10-10 (commit `ca8bc70`). The alternatives were not recorded; the bundle option is the gap.

### ADR-26: Identifying User-Agent and polite intervals

- **Context.** The sources are public services (a national space-weather agency, an open weather API, a solar observatory, a climate centre) that ask clients not to hammer them. Some give no key, so the operator's only signal is the request itself.
- **Decision.** One `USER_AGENT` string in `net_http.h`, applied to every request: project name and version, what it is ("personal ESP32 display"), and a contact. Use a real contact in your own build. Periods per source: gateway 2 s (LAN only), weather 15 min, space weather and Kp and aurora 30 min, sun image 60 min, moon times once per local day, ENSO 12 h (it changes weekly or monthly). Back-off on failure, never a tight retry loop. Show attribution for each data source in the About screen.
- **Alternatives.** The default `ESP32HTTPClient` agent: anonymous. Shorter periods: the data does not change that fast (SWPC JSON carries `Cache-Control: max-age=60`; the guideline used is one fetch per 5 to 30 min per product).
- **Consequences.** Data is up to one period old, which the UI marks with staleness thresholds. New sources need their own terms read first: one image API was noted as "be polite, at most one request per 30 min".
- **Evidence.** [11-space-weather-and-climate-data.md](../11-space-weather-and-climate-data.md), `net_http.h`, `powerwall.cpp` (`JOBS[]`).

### ADR-27: Background jobs wait for a valid clock

- **Context.** TLS certificate validity is checked against the system time. Several data items (moon, aurora hours, space weather) are compared with real time as well.
- **Decision.** `tu::clockValid()` (time at or after 2024-01-01) gates `runOneJob()`. NTP is configured with `configTzTime` at start. Parsers (`tu::parseIso8601`, `parseHttpDate`) are shared and tested on the PC.
- **Alternatives.** Two thresholds had been in use (`1700000000` and `tm_year < 124`); one function replaced both.
- **Consequences.** Nothing but the gateway poll runs until NTP answers. Without Wi-Fi there is no clock after a power cut (no RTC chip on the board).
- **Evidence.** `time_util.h`, `powerwall.cpp`.

---

## Storage

### ADR-28: No internal-flash writes at runtime

- **Context.** Internal flash has a limited write life, and the owner ruled out runtime wear. Separately, PSRAM is unreachable while flash is being written, which can disturb the RGB scan-out.
- **Decision.** Runtime state stays in RAM or RTC memory (`RTC_NOINIT_ATTR`); durable data goes to the SD card. No NVS, LittleFS or SPIFFS. `WiFi.persistent(false)` stops the Wi-Fi stack storing its config in NVS. Credentials come from `secrets.h` at build time. Demos that are not meant to run unattended (the `03` launcher stores its choice in NVS) are the exception, not the pattern.
- **Alternatives.** NVS for small state: the owner ruled it out for wear. EEPROM emulation: same. RTC memory: survives soft resets and crashes, not a power cut or the reset button. That gap is covered by the SD backup (ADR-31).
- **Consequences.** "Today's totals" are a lifetime counter minus a midnight snapshot in RTC memory, backed up to the SD card. After a cold reset without a card the screen says "since HH:MM" until the next midnight.
- **Evidence.** `energy.cpp`, `history.cpp`, [12-esp32-performance-and-framework.md](../12-esp32-performance-and-framework.md) section 3 (drift when PSRAM is blocked by flash writes). The wear concern is an owner requirement; no wear measurement exists.

### ADR-29: SdStore owns the SD bus and never formats

- **Context.** SD SCK/MOSI (GPIO48/47) are the same pins as the ST7701's 3-wire init bus. `SD.begin()` with no arguments would claim the default FSPI pins 10 to 13, which are LCD red bits on this board. There is no card-detect pin.
- **Decision.** `common/lib/SdStore` creates its own `SPIClass` on the SD pins. It is initialised only after `board4848::initDisplay()`; after init the panel's CS (GPIO39) stays high and ignores SD traffic. Every card access, from any task, takes one recursive mutex (the lock serialises access; it does not confine it to one task). It never formats a card. The helpers open, write and close in one call, so no `File` survives a card swap.
- **Alternatives.** SDMMC mode: the slot is wired for SPI. A shared bus with locking against the panel: not needed once the panel is initialised.
- **Consequences.** Boot order is a rule: display first, then SD (see the init order in the checklist). Use `sdcard::Lock` for direct `SD.*` calls. In project 05 card I/O comes from more than one task: the SD task (`history.cpp`: minute log, day store, settings, baseline) reads and writes, and the fetch jobs on the network task write and read cache records (`cache::save` / `cache::load`) under the same lock. A slow or retrying card can therefore delay the network task for as long as the SdStore retry/step-down takes; an open option is to queue cache writes to the SD task. Serial diagnostic commands (for example `c`) may also do card I/O.
- **Evidence.** `sd_store.h` header comment, `Board4848S040_pins.h`. Write-up: [inv-sd-bring-up](03-investigations.md#inv-sd-bring-up).

### ADR-30: The SD clock is tuned per card and remembered on the card

- **Context.** A card's rated clock (CSD) is nearly always "25 MHz" and is not trustworthy in SPI mode. A 16 GB SDHC card passed 19 of 19 tests at 20 to 25 MHz; a 2 GB SDSC card gave corrupt responses above 4 MHz and was flaky at 4 MHz.
- **Decision.** `sdcard::begin(maxHz)` tries 25, 20, 10, 4 and 1 MHz, proving each with a link test (write and read back 32 KB). The result is written to `/.sdstore.cfg` on the card, keyed by the card's CID, so the next mount starts there. An I/O error on a present card is retried once at the same clock, then once one step lower, and the lower clock is remembered. `forgetClock()` clears it.
- **Alternatives.** A fixed clock: right for one card, wrong for the next. Remembering the clock in NVS: a flash write (ADR-28), and wrong if the card is swapped.
- **Consequences.** A first mount on a new card takes longer. The card carries one hidden file. Retired cards stay retired: the clock tuning protects a mount, not a card that corrupts data.
- **Evidence.** `sd_store.h`, [hardware-log.md](../hardware-log.md) (SD card row). Write-up: [inv-sd-card-clock](03-investigations.md#inv-sd-card-clock).

### ADR-31: Atomic CRC writes for state that must survive

- **Context.** A power cut mid-write can leave a half-written file. Today's energy baseline and settings must survive.
- **Decision.** `sdcard::writeAtomic(path, data, len)` (up to 4 KB) writes `path.tmp` with a 16-byte header `{magic "SDA1", seq, len, crc32}`, then removes `path` and renames. `readAtomic` checks both files and returns the valid copy with the higher sequence number, tidying an orphan. Logs use `appendLine`, which can leave a partial last line, so readers skip lines that do not parse. A plain `writeFile` is not atomic.
- **Alternatives.** Journaling filesystems: not on FAT. Writing twice to alternate slots: the tmp+rename sequence gives the same guarantee.
- **Consequences.** At least one valid copy exists after a cut at any point. The 06 test bench covers torn, orphaned and stale copies.
- **Evidence.** `sd_store.h`, `projects/06-sd-card-test/README.md`. Write-up: [inv-sd-bring-up](03-investigations.md#inv-sd-bring-up).

### ADR-32: Hot-swap by polling

- **Context.** No card-detect pin. Users pull and insert cards while the device runs.
- **Decision.** One task calls `sdcard::poll()` every 1 to 2 s (05 uses 2 s). It reads sector 0 (about 1 ms at 20 MHz). If the card is gone it unmounts; if unmounted it retries the mount every 2 s. That task owns the polling and the history store's reads and writes; it is not the only card user (fetch jobs on the network task save and load cache records under the SdStore lock, see ADR-29), and UI code only copies data in and out.
- **Alternatives.** Mounting once at boot: a loose card would need a reboot. Interrupt on a detect pin: no such pin.
- **Consequences.** Pull and re-insert recovers without a reset (06 verified). Because the network task also touches the card, a slow card can delay a fetch job; queueing cache writes to the SD task is an open option. Inserting a card can briefly drop the PC's USB-serial link; the ESP32 does not reset.
- **Evidence.** `sd_store.h`, `history.cpp`, [hardware-log.md](../hardware-log.md).

### ADR-33: A patched SD library

- **Context.** In Arduino core 3.2.1 `sdStop()` does not wait for the busy period after the multi-block Stop Tran token, so the next CMD13 meets a busy card. Any write of 1 KB or more failed intermittently at every clock from 1 to 25 MHz.
- **Decision.** `common/lib/SD` is a patched copy of the core's SD library, listed in `lib_deps` so it replaces the built-in one. Search `src/` for "mini-screen" to find the two changes (the busy wait, and register reads for card reports). On a core upgrade, re-copy upstream and re-apply or drop the patch.
- **Alternatives.** Retries only: hide the bug and lose data. Another SD library: not needed once the cause was found.
- **Consequences.** The fix is local. The patched library is excluded from lint (`common/lib/SD` is upstream code).
- **Evidence.** `common/lib/SD/README-mini-screen.md`, [hardware-log.md](../hardware-log.md). Write-up: [inv-sd-multiblock-write](03-investigations.md#inv-sd-multiblock-write).

### ADR-34: SD cache of fetched data

- **Context.** After a reboot every screen is empty until each source is fetched again, which takes minutes with one TLS session at a time. Sources should not be fetched again while their data is still fresh.
- **Decision.** `cache.h` defines the design, `cache.cpp` implements it: files `/powerwall/cache/<name>.bin`, written with `writeAtomic`. A record is a 24-byte header (magic `PWC1`, version, size, fetched UTC) plus a plain-data payload. A record is accepted only if magic, version and size match, so a changed struct is ignored rather than misread. Time is stored as UTC and converted back to `millis()` age on load (`msFromUtc`), so the existing staleness checks still work. `dueInMs` lets the scheduler skip jobs that are still inside their window.
- **Alternatives.** Cache in RTC memory: lost on power cut. Cache on internal flash: ADR-28.
- **Consequences.** Status: works on hw (2026-10-11, `c9a6630`): all 7 records restore about 2 s after boot (at SD mount, without waiting for the clock) and fresh ones skip their first fetch; serial `c` lists them. Version numbers must be bumped when a cached struct changes meaning.
- **Evidence.** `projects/05-powerwall-display/src/cache.h`.

### ADR-35: Day history: month files with fixed slots and per-slot CRC

- **Context.** The history screen (day, week, month charts) needs hourly energy for today and daily totals plus hourly bins for past days, kept for months, with no internal-flash writes (ADR-28) and no card access from the UI thread (ADR-32). The Powerwall's local API only gives lifetime counters, so the history has to be built on the device.
- **Decision.** `day_history.cpp` turns changes in the gateway's lifetime counters into hourly bins of today's `DayRecord` (448 bytes: date, flags, day totals, 24 hourly solar, home, import and export values, and the battery percentage at the end of each hour, with a CRC). Today's record lives in RTC memory, so the UI reads it live. The SD task saves it every 10 minutes, on the hour and at midnight into `/powerwall/days/YYYY-MM.bin`: 31 fixed slots (slot = day of month minus 1, 13,888 bytes, no header), each with its own CRC, so one bad slot costs one day. The file is replaced by writing `.tmp` whole, then remove and rename, because `writeAtomic` is capped at 4 KB ([Q-SD-08](01-quirks-and-gotchas.md#q-sd-08)); a cut in between leaves a complete `.tmp` that the reader uses. The SD task also reads month files into a three-month PSRAM cache. The UI asks for a mode and period and copies a `View`; a sequence number in the view lets the screen skip redrawing unless something changed. The UI never waits on the card and shows "loading" or empty bars while a past month is being read.
- **Alternatives.** One CSV per day re-read for each chart: a month view would parse up to about 45,000 minute rows for every swipe, and the minute log is for inspection, not charts. One small file per day: 365 files a year, slow directory scans on FAT and a `writeAtomic` call each. Rewriting the whole history file each time: write amplification and one CRC for everything. NVS or other internal flash: ruled out by ADR-28 (repeated writes). RTC memory alone: lost on a power cut, and too small for months.
- **Consequences.** Fixed slots make a read or an update a seek, and a month file is about 14 KB, so a save is one short write. Changing the `DayRecord` layout needs a new magic (now `DAY1`); old records are then ignored. A day the device was off, or where totals start after midnight (power cut or first boot), is flagged partial and shown as such. With no card only today is available. DST: the repeated hour adds into one bin.
- **Evidence.** `projects/05-powerwall-display/src/day_history.h`, `day_history.cpp` (header comment), `day_history_calc.h` (pure logic, host-tested), `screen_history.cpp`. No power-cut or long-soak measurements recorded yet; status `builds` for the save path until the maintainer reports otherwise.

### ADR-36: Release pipeline and browser flashing

- **Context.** Installing needed PlatformIO, a toolchain and (for project 05) a hand-made `secrets.h`. Owners who only want to try the board, or the demo projects, should be able to flash from a browser. Project 05 reads Wi-Fi, gateway and location from `secrets.h` at build time, so a generic prebuilt binary cannot be personal until the planned on-device setup portal (hotspot plus phone form, settings in NVS) exists.
- **Decision.** `.github/workflows/release.yml` runs on tags `v*` (and by hand). Per project (01, 02, 03, 05) it builds with the CI-pinned PlatformIO 6.1.18 and merges bootloader (0x0), partition table (0x8000), `boot_app0` (0xe000) and app (0x10000) into one 16 MB-layout image with `tools/make-flash-image.py` (esptool 5.0.2 `merge_bin`; it checks the built partition table really puts the app at 0x10000) and writes an ESP Web Tools manifest. Project 05 is built from `secrets.example.h` (placeholders). A `site` job assembles `docs/flash/index.html`, the manifests and `firmware/*.bin`; tag runs attach the images and `SHA256SUMS.txt` to a GitHub Release and deploy the same folder to GitHub Pages (source: GitHub Actions). The page loads `esp-web-tools@10.4.0` from unpkg and its manifests use relative paths, so the browser fetches everything from one origin. The page carries a single removable notice block saying prebuilt firmware cannot join Wi-Fi yet. With placeholders the firmware does not crash: Wi-Fi never associates, `netTask` sets "Wi-Fi not connected", scans every 30 s and retries every 2 s, and the screens show offline or "--" (read from `powerwall.cpp`, not run on hardware with this image).
- **Alternatives.** Manifest pointing at Release asset URLs: GitHub release downloads (the `github.com` redirect and the `release-assets.githubusercontent.com` target) send no CORS headers, so a browser page cannot fetch them (reported by other ESP Web Tools projects; not tested here). `raw.githubusercontent.com` from committed binaries: has CORS but puts megabytes of binaries in git history on every release. A `gh-pages` branch with force-pushed binaries: works but needs a write token and history cleanup; the Actions Pages deploy needs neither. Offering only source builds: keeps the setup friction. Per-user builds in the browser: not possible, secrets are compile-time.
- **Consequences.** The Release is the permanent archive (manual `esptool write-flash 0x0 <image>` works); Pages always shows the newest tag. One-time maintainer setup: repo Settings > Pages > Build and deployment > Source = **GitHub Actions**; Settings > Environments > github-pages > Deployment branches and tags must allow tags matching `v*` (the default rule allows only the default branch, which rejects tag runs). Once the setup portal ships, remove the `SETUP-NOTICE` block in `docs/flash/index.html`, and the "can't join Wi-Fi" sentences in `docs/getting-started-powerwall.md` and the README. A merged image holds the bootloader's flash header from the PlatformIO build; the Web Tools "erase" option wipes the factory firmware, so the page links the backup instructions ([D-18](02-debugging-playbook.md#d-18-flash-backup-and-restore)).
- **Evidence.** `.github/workflows/release.yml`, `tools/make-flash-image.py`, `docs/flash/index.html`. `merge_bin` was run locally on an existing project 05 build and produced a valid image (header 16 MB, 80 MHz, DIO as in the build output). The workflow and the browser install itself are untested (status `builds`); first test: run the workflow by hand on a branch.

---

## UI conventions

These are conventions rather than decisions with serious alternatives, so each is a rule with its reason.

### Palette

Dark theme, defined once in `ui_common.cpp` and shown on the design mock-ups. Screens use the names, never hex literals (a consolidation round replaced the repeated literals).

| Name | Value | Use |
|---|---|---|
| `BG` | `0B0F14` | Screen background |
| `CARD` / `CARD_HI` | `151B23` / `1F2731` | Card and raised card |
| `TRACK` | `2A3440` | Bar tracks, inactive dots |
| `TEXT` / `MUTED` | `E6EDF3` / `8B98A5` | Primary and secondary text; MUTED is also the stale colour |
| `C_SOLAR`, `C_GRID`, `C_HOME`, `C_BATT` | `F5B700`, `9AA5B1`, `3DB2FF`, `3DDC84` | The four power nodes |
| `C_ALERT` / `C_ALERT_BG` | `E5484D` / `2A1215` | Alert and its background |
| Severity ramp `levelColor(n)` | `3DDC84`, `F5D400`, `FF8A3D`, `E5484D` | 0 green, 1 yellow, 2 orange, 3 and up red (battery bands, Kp, scales) |
| Extras | `3B82F6`, `B266FF`, `3A4654` | Blue, purple, dim bar |

A value that is missing shows `--` in MUTED. A value that is merely old shows in MUTED too (`setValueF(label, have, stale, ...)`).

### Fonts

LVGL's built-in Montserrat, enabled in `lv_conf.h`: **12, 14, 16, 18, 20, 22, 26, 28, 30, 40, 48**. Default is 16.

- They hold ASCII, the degree sign (write it as `"\xC2\xB0"`) and LVGL's symbol glyphs.
- They have **no middle dot**. Use `/` or `-` as a separator.
- Text from the network must be reduced to ASCII before display (the ENSO parser maps accented letters to plain ones).
- Some sizes are used once (18, 28, 30, 40). Merging them would save tens of KB of flash, but it is a design change, so it starts on the mock-up.

### Screen registry, top bar, page dots

- `main.cpp` has one `ScreenDef` table: `{name, create, refresh, onShow, obj}`. The order is `ui::ScreenIndex`; a `static_assert` checks the count.
- Contract per screen: `create()` builds the screen once at boot; `refresh()` updates from the latest data (every 500 ms for the visible screen, and once just before it is shown); `onShow()` starts entry animations.
- The top bar (time at 28, date at 14, Wi-Fi name and signal right-aligned) and the page dots (centred at y = 456; active page is a 20x8 pill in `TEXT`, others 8x8 in `TRACK`) are created by `ui::screenCreate(index)` and updated centrally by `ui::topBarUpdateFor`. A screen does not build its own.
- Navigation is a swipe (left or right, wrapping) with a 300 ms slide; animations of hidden screens are paused.
- Layout work starts on a design mock-up (a canvas or a sketch). Change that first, then build the chosen layout on the device. Do not redesign directly in firmware.

### Shared widget helpers

`ui::box`, `card`, `label`, `caption`, `rect`, `flex`, `valueUnit`, `labelAt`, `cardCaption`, `hbar`, `Banner`, `fadeIn`, `ChangeGate`, `barChart*`, `inert`. Prefer these to open-coded `lv_obj_create` sequences. Use the `LV_LABEL_LONG_MODE_*` constant names. Endless-scrolling long text (`SCROLL_CIRCULAR`) redraws every frame: slow it (`lv_anim_speed_clamped`) or wrap it.

---

## Code conventions

- **Contracts first.** For any work that will be split between people or agents, write the headers first: data structs, function signatures with doc comments that state the guarantees (thread, units, ownership, what is logged), file ownership, and the acceptance checks. Then fill in the `.cpp` files. The shared modules (`shared_state.h`, `mem_util.h`, `time_util.h`, `net_http.h`, `ui_common.h`, `ui_chart.h`, `cache.h`) were written this way, and parallel agents then each owned disjoint files. Integration, build, flash and serial checks stay in one place.
- **Additive first.** A refactor adds the new API next to the old one so every branch still builds, and removes the old one only after all callers moved.
- **Lint always.** Run `tools/lint.ps1 <project>` after editing C or C++ and before committing. It runs cppcheck through PlatformIO and clang-tidy on a translated compilation database; configuration is in `common/platformio-base.ini` and `.clang-tidy` (bug-finding checks only, no naming or style rules). Fix medium and high findings, or justify with `// NOLINT(<check>)` or `// cppcheck-suppress <id>` plus a reason. `tools/build-all.ps1` must pass before a commit.
- **Naming.** File-scope mutable state is `s_name`; constants are `UPPER_SNAKE`; types are `PascalCase`; free functions and members are `camelCase`. Helpers live in namespaces (`ui::`, `net::`, `tu::`, `board4848::`, `sdcard::`, `cache::`), not behind `using namespace`. Public screen entry points are `<screen>Create/Refresh/OnShow`. Give each fetched struct a `fetchedMs` and test it with `isStale()` (`PwData` and `WeatherData` still call theirs `updatedMs`; unifying the name is open).
- **Comments.** A header comment states what the file is for, who calls it, on which task, and what it guarantees. Keep log line formats stable: acceptance checks grep them. Update the comment when the code changes; stale comments were a large part of one review.
- **Pure logic is tested on the PC.** Code with no Arduino types (`net_body.h`, `time_util.cpp`, the ENSO parsers, `cache.h` helpers) is built with MSVC and run by `hosttest/run.ps1` against real payloads. Hardware behaviour is tested on the board.
- **Serial is the control channel.** One line in, one line out ending `#ok` or `#err <reason>`. 05 has `s <n>`, `n`, `p`, `l`, `b <0-255>`, `m`, `x` (screenshot over serial at 921600 baud, decoded by `tools-dev/screendump.py`), `r`, `h`, and a 10 s heartbeat (fps, longest frame gap, memory, LVGL pool, SD state, TLS counters, redraw px/s). Commands are read without blocking so the UI keeps running. Build and test scripts parse these lines.
- **Credentials.** Only in `src/secrets.h` (git-ignored). Commit `secrets.example.h` with obviously fake values.
- **Status words.** A project is `builds` until the owner has seen it work on hardware. Only claim hardware results that were observed or reported.

---

## Repository and project layout

```
mini-screen/
  README.md                  master index
  CLAUDE.md                  rules for agents and contributors
  docs/                      hardware notes 01-12, hardware-log.md, guide/, datasheets/, vendor/
  common/
    platformio-base.ini      shared env [base_4848s040]
    boards/                  optional board JSONs (not used by default)
    lib/Board4848S040/       pins, ST7701 init, GT911, LVGL glue
    lib/SdStore/             SD access (ADR-29..32)
    lib/SD/                  patched core SD (ADR-33)
  projects/NN-name/          one PlatformIO project each; projects/README.md is the index
  reference/                 prior-art submodules (read-only)
  tools/                     PowerShell: pio, build-all, lint, backup/restore flash
  firmware/                  flash backups, vendor images
```

| Rule | Why |
|---|---|
| Projects are numbered `NN-name` and each has a row in `projects/README.md` | One index, one status |
| A project's `platformio.ini` only adds what is project-specific | The base carries the settings that must not diverge (`qio_opi`, `-O2`, no USB CDC) |
| Pins only in `Board4848S040_pins.h` | ADR-02 |
| `reference/` is never edited | Copy what is needed into `common/` or a project |
| Run `pio` and `esptool` from PowerShell, never Git Bash | ESP's `idf_tools.py` installer rejects MSYS shells |
| Back up the factory flash before the first upload | It is the only way back to the shipped firmware |

Which project to copy: `02-lvgl-hello` for LVGL work, `01-hello-display-touch` for plain graphics, `05-powerwall-display` as the model for a networked, multi-screen application, `06-sd-card-test` as the SD bench, `03-lvgl-demos` (`bench` env) for performance measurements.

---

## Open points

These came up while writing this guide, and have no settled answer in the repo.

| Point | State |
|---|---|
| Arduino versus ESP-IDF | Reasoning only (doc 06 and doc 12), no decision. Triggers for moving are listed in doc 12 section 5 |
| `LV_ATTRIBUTE_FAST_MEM` in network projects | Rejected while mbedTLS was internal-only; not re-measured since the allocator hook (ADR-16) |
| Gateway keep-alive (`PW_GW_KEEPALIVE`) | Default on since `c9a6630`; 10-minute soak only, long soak pending (ADR-23) |
| Exact write-back in the rotated flush (turn `auto_flush` off) | Proposed; not done (ADR-14) |
| SD cache I/O (`cache.cpp`) | Works on hw; restores at SD mount (ADR-34) |
| Pinned CA versus the certificate bundle | Not evaluated (ADR-25) |
| Why the gateway poll period is 2 s | Not recorded; it matches the gateway's own update rate as far as seen, but no source says so |
| PCLK 12 MHz | Taken from Arduino_GFX's config for this board; 8 to 16 MHz is the quoted safe range; no sweep was made here |
| Second unit | Nothing has been tested on another board |

---

## Sources

- LVGL documentation: https://docs.lvgl.io/ and source https://github.com/lvgl/lvgl
- Espressif LCD FAQ (drift, bounce buffers, cache line, PCLK limits): https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html
- ESP-IDF 5.4 RGB LCD driver: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- ESP-IDF 5.4 external RAM (`ALWAYSINTERNAL`, PSRAM use): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/external-ram.html
- ESP-IDF 5.4 mbedTLS (per-session heap, dynamic buffers): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/mbedtls.html
- Espressif mbedTLS FAQ: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/mbedtls.html
- Espressif memory analysis case study: https://developer.espressif.com/blog/esp32-memory-analysis-case-study/
- arduino-esp32 core: https://github.com/espressif/arduino-esp32
- pioarduino platform: https://github.com/pioarduino/platform-espressif32
- Arduino_GFX: https://github.com/moononournation/Arduino_GFX (issue 465 on this board between releases: https://github.com/moononournation/Arduino_GFX/issues/465)
- ArduinoJson external RAM: https://arduinojson.org/v7/how-to/use-external-ram-on-esp32/
- LVGL forum, Wi-Fi horizontal offset fixed by a bounce buffer: https://forum.lvgl.io/t/lvgl-horizontal-offset-when-esp32-s3-wifi-enabled-esp-idf/21888
- RFC 9112, HTTP/1.1 message framing (chunked, Content-Length): https://www.rfc-editor.org/rfc/rfc9112
- Architecture decision records (format): https://adr.github.io/
