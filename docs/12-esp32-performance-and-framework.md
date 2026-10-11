# ESP32-S3 performance, memory and framework choice

Research notes (2026-10-11) for growing project 05 on this board: ESP32-S3 N16R8, 480×480 RGB panel with bounce buffers, LVGL 9.5, Wi-Fi + mbedTLS, SD over SPI. The toolchain is pioarduino `54.03.21-2` (Arduino 3.2.1 / IDF 5.4).

Trigger: 05's internal RAM free fell from ~161 KB to ~121 KB after adding screens (min ever ~101.5 KB, largest block ~41 KB). One TLS session needs ~40-50 KB of internal RAM ([07-gotchas](07-gotchas.md)).

Evidence tags:
- **[local]**: read from the installed packages under `~/.platformio` (paths given)
- **[web]**: linked source
- **[unverified]**: plausible but not confirmed by a source or a measurement on this board

Nothing here has been measured on hardware yet. Everything in section 6 is a proposal.

---

## 0. Short version

1. **Stop mbedTLS using only internal RAM, at runtime with no rebuild.** The prebuilt core exports `mbedtls_platform_set_calloc_free()` [local]. Point it at a calloc that prefers PSRAM for large blocks. Expected: the TLS session's 2×16 KB record buffers and handshake temporaries leave internal RAM, so min-free should rise by roughly 30-40 KB [unverified estimate].
2. **Lower the malloc PSRAM threshold at runtime.** `heap_caps_malloc_extmem_enable(512)` (the core sets 4096 at boot [local]). Small `String`, ArduinoJson, FATFS and other allocations then go to PSRAM instead of fragmenting internal RAM.
3. **Call `WiFi.persistent(false)`.** The Arduino default is `true` [local], so Wi-Fi config is stored in NVS, which is a flash write. That conflicts with the no-flash-writes rule and is a known RGB drift trigger.
4. **Fold `pw_wx` into `pw_gw`.** The two tasks are already serialised by `s_tlsLock`, so this saves a 10 KB internal stack. Trim the other stacks from measured high-water marks.
5. **Framework:** stay on Arduino. If runtime hooks aren't enough, prefer `framework = arduino, espidf` (Arduino as an IDF component, built per project) over pioarduino `custom_sdkconfig`. In this multi-project repo, `custom_sdkconfig` rewrites the **shared global** framework package and thrashes between projects (section 4).

---

## 1. What the current core actually ships with

The authoritative file is `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h` [local]. Note that `esp32s3/sdkconfig` in the same package is a *different* variant (it says `SPIRAM_MODE_QUAD`, static TX buffers, BA win 16), so don't read settings from it.

| Option | Core 3.2.1 `qio_opi` | IDF default / note | Why it matters here |
|---|---|---|---|
| `MBEDTLS_MEM_ALLOC_MODE` | **INTERNAL** | INTERNAL | All TLS allocations come from internal RAM: the main pressure |
| `MBEDTLS_ASYMMETRIC_CONTENT_LEN` | **not set** (16 KB in + 16 KB out) | `y` (in 16 KB / out 4 KB) in IDF Kconfig [web: Kconfig] | Costs 12 KB extra per session |
| `MBEDTLS_DYNAMIC_BUFFER` | not set | n | ~20 KB less per session when on [web: IDF mbedTLS doc] |
| `MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` | y | n | ~4 KB per session |
| `MBEDTLS_SSL_MAX_CONTENT_LEN` | 16384 | | |
| `SPIRAM_MALLOC_ALWAYSINTERNAL` | 4096 | 4096 | `malloc` ≤ 4 KB goes internal first. Changeable at runtime |
| `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | y | n | Wi-Fi/lwIP dynamic buffers already try PSRAM |
| `ESP_WIFI_STATIC_RX_BUFFER_NUM` | 8 (~1.6 KB each, internal) | 10 | ~13 KB fixed internal |
| `ESP_WIFI_DYNAMIC_TX_BUFFER` / `RX_BA_WIN` | dynamic 32 / 6 | | |
| `SPIRAM_XIP_FROM_PSRAM` (= `FETCH_INSTRUCTIONS` + `RODATA`) | **not set** | n | Code runs from flash, so flash writes stall the PSRAM framebuffer |
| `ESP32S3_DATA_CACHE_LINE_SIZE` | **32 B**, D-cache 32 KB, I-cache 16 KB | | The Espressif FAQ asks for 64 B in bounce-buffer mode [web: FAQ] |
| `LCD_RGB_RESTART_IN_VSYNC` | **y** | n | Auto-recovers from drift (the FAQ calls it a fallback, not a fix) |
| `LCD_RGB_ISR_IRAM_SAFE` | not set | | |
| `SPIRAM_SPEED` | 80 MHz octal | | 120 MHz is experimental (section 3) |
| `FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` | y | | PSRAM stacks are possible via `xTaskCreateStatic` |
| `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` | not set | | `EXT_RAM_BSS_ATTR` has no effect without it |
| `COMPILER_OPTIMIZATION` (framework libs) | `-Os` | | Our `-O2` covers only our code and libs (LVGL), not Wi-Fi/mbedTLS |
| `BT_ENABLED` (Bluedroid) | y | | `initArduino()` already calls `esp_bt_controller_mem_release()` when BT is unused [local: `cores/esp32/esp32-hal-misc.c:309`], so nothing to gain at runtime |
| `ESP_TIMER_TASK_STACK_SIZE` | 8192 | 3584 | 8 KB internal, fixed |
| `ESP_TASK_WDT_TIMEOUT_S` | 5, idle CPU0 checked | | See the gotcha on HTTPClient busy-waits |
| `HEAP_POISONING_LIGHT` | y | | Small allocation overhead |

## 2. Internal RAM budget for 05 (estimate)

| Consumer | Internal bytes | Source |
|---|---|---|
| LVGL draw buffer, 48 lines × 480 × 2 | 46,080 | `Board4848S040_lvgl.cpp:46`, `-DB4848_LVGL_BUF_LINES=48` |
| RGB bounce buffers, 2 × 480 × 10 × 2 | 19,200 | `B4848_LCD_BOUNCE_PX` |
| Task stacks: loop 8 K, `pw_gw` 12 K, `pw_wx` 10 K, `sd` 6 K | 36,864 | `powerwall.cpp:434`, `history.cpp:233` |
| System stacks: esp_timer 8 K, tcpip 4 K, event 2 K, Wi-Fi, idle, IPC | ~20 K | sdkconfig above |
| Wi-Fi static RX buffers | ~13 K | sdkconfig above |
| **TLS session (transient, while polling)** | **~40-50 K** | 07-gotchas, IDF table (42 KB default) |
| Small allocations < 4 KB (String, ArduinoJson pools, FATFS objects, SD) | unknown | Likely source of the 161 → 121 KB drop together with new `.bss` and the new SD task [unverified] |

LVGL objects are not in this table: they live in the 1 MB PSRAM pool (`LV_MEM_POOL_ALLOC`).

**Find the 40 KB first.** The new screens' widgets should cost no internal RAM, so the drop is more likely from:
- new static data (check `.dram0.bss`/`.dram0.data` in `firmware.map`, or run `pio run -t size` before and after)
- the SD task and its buffers
- small heap allocations

Use `heap_caps_print_heap_info(MALLOC_CAP_INTERNAL)` and `heap_caps_dump()`. Both are in the prebuilt libs [local].

## 3. Common problems and workarounds that worked

| Problem | Cause | What worked | Source |
|---|---|---|---|
| Screen drifts/shifts sideways when Wi-Fi is on or on flash writes | PSRAM bandwidth shared by EDMA and CPU. **PSRAM is unreachable while flash is written** (Wi-Fi/NVS/OTA), so the RGB DMA underruns | **Bounce buffers** (confirmed by the OP: "single buffer in PSRAM + 10 lines in RAM"). We already do this, verified stable here. `XIP_FROM_PSRAM` did *not* fix it for that OP | [LVGL forum 21888](https://forum.lvgl.io/t/lvgl-horizontal-offset-when-esp32-s3-wifi-enabled-esp-idf/21888) |
| Same, residual | 32 B cache line in bounce mode; code/rodata fetched from flash | FAQ checklist: `DATA_CACHE_LINE_64B`, `SPIRAM_FETCH_INSTRUCTIONS` + `SPIRAM_RODATA` (= XIP), `FREERTOS_HZ=1000`, no long critical sections, `COMPILER_OPTIMIZATION_PERF`. For one-off flash ops: drop PCLK to ~6 MHz with `esp_lcd_rgb_panel_set_pclk()` for the operation. Last resort: `RESTART_IN_VSYNC` (lowers FPS) | [Espressif LCD FAQ](https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html), [IDF RGB LCD](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html) |
| Wanting these as Arduino defaults | | arduino-esp32 declined (#9348, "won't fix"). Jason2866 saw **crashes with FETCH_INSTRUCTIONS/RODATA on QIO-PSRAM S3**; octal (ours) wasn't reported. Core 3.2.1 *does* ship `RESTART_IN_VSYNC=y` [local] | [arduino-esp32 #9348](https://github.com/espressif/arduino-esp32/issues/9348) |
| `SSL - Memory allocation failed` / handshake errors | Internal heap or **largest block** too small. Total free looks fine because PSRAM is counted | Watch `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` before connecting. Shrink LVGL internal buffers; enable `TRY_ALLOCATE_WIFI_LWIP`; fix CA-cert leaks in WiFiClientSecure. The FAQ says handshake errors are "usually" memory | [esp32.com 152261](https://esp32.com/viewtopic.php?p=152261), [Espressif mbedTLS FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/mbedtls.html), [arduino-esp32 #9636](https://github.com/espressif/arduino-esp32/issues/9636) |
| TLS RAM per session | mbedTLS keeps in/out buffers, peer cert and keys for the whole session | Default 42 KB → **22 KB** with `DYNAMIC_BUFFER` + `DYNAMIC_FREE_CONFIG_DATA` + `DYNAMIC_FREE_CA_CERT`; −4 KB without `KEEP_PEER_CERTIFICATE`; −12 KB with `ASYMMETRIC_CONTENT_LEN` (out 4 KB). Out-buffer 2 KB saved ~14 KB in Espressif's case study | [IDF mbedTLS](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/mbedtls.html), [Kconfig](https://codex.ac6.fr/Zephyr/raw/hal_espressif-latest/components/mbedtls/Kconfig), [memory case study](https://developer.espressif.com/blog/esp32-memory-analysis-case-study/) |
| Moving TLS to PSRAM | | `MBEDTLS_EXTERNAL_MEM_ALLOC` is an official choice (needs `SPIRAM_USE_MALLOC`). Caveats: key material sits in PSRAM (only "safe" with flash encryption on S2/S3); one S3 project **disabled HW AES after hangs in the PSRAM DMA path** [single report] | [Kconfig](https://codex.ac6.fr/Zephyr/raw/hal_espressif-latest/components/mbedtls/Kconfig), [commander1024 commit](https://git.commander1024.de/Commander1024/ESP32_Serial_Swiss_Army_Knife/commit/c018bfe361198e7b99f00121048ba588e27ba085), [esp32.com 127666](https://esp32.com/viewtopic.php?p=127666) |
| Wi-Fi breaks once LVGL is added | Oversized LVGL internal buffers / LV_MEM pool | Smaller draw buffers; Wi-Fi/lwIP in PSRAM | [LVGL forum 21391](https://forum.lvgl.io/t/waveshare-esp32-s3-touch-lcd-4-3b-lvgl-wifi-no-luck/21391) |
| Task watchdog (IDLE0) during HTTPS | HTTPClient chunked reader busy-waits on a kept-alive TLS socket | Already fixed in 05: `useHTTP10(true)`, short read timeout, net tasks at idle priority | [07-gotchas](07-gotchas.md) |
| ArduinoJson filling internal RAM | Pool blocks < 4 KB go internal | `SpiRamAllocator` passed to `JsonDocument` (or lower the extmem threshold globally) | [ArduinoJson: external RAM](https://arduinojson.org/v7/how-to/use-external-ram-on-esp32/) |
| SD + display | Sharing an SPI bus with the panel's init SPI or touch | Not an issue here: SD has its own pins/bus. The real SD issue was the core's multi-block write bug (fixed in `common/lib/SD`) | [hardware-log](hardware-log.md) |
| Arduino_GFX regressions on this exact board | ST7701 4848S040 broke between GFX 1.2.9 and 1.4.6 for one user | We pin GFX 1.6.9 (works). Re-test after any GFX bump | [Arduino_GFX #465](https://github.com/moononournation/Arduino_GFX/issues/465) |

**PCLK headroom** (Espressif, limited testing): octal PSRAM at 80 MHz gives ~22 MHz max PCLK; at 120 MHz, ~30 MHz. We run 12 MHz, so we're far from the limit. 120 MHz PSRAM needs `IDF_EXPERIMENTAL_FEATURES` and is risky above 65 °C [web: FAQ]. There's no reason to try it here.

## 4. Knobs: what's reachable from stock Arduino, and the cost of the rest

### 4a. Reachable now, with no framework rebuild (verified symbols in the prebuilt libs [local])

| Knob | How | Gain | Risk |
|---|---|---|---|
| mbedTLS allocator → PSRAM | `mbedtls_platform_set_calloc_free(cb_calloc, cb_free)` in `setup()` before any TLS. Exported by `libmbedcrypto.a`; `esp_config.h` defines `MBEDTLS_PLATFORM_MEMORY` without a fixed macro, so runtime override is allowed. Suggested: `heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM)` when `n*sz >= 1024`, else internal, with a fallback to the other. Free with `heap_caps_free` (it handles both) | Most of the 40-50 KB session moves to PSRAM [unverified] | **Medium.** HW AES/SHA DMA with PSRAM buffers: IDF supports the equivalent Kconfig mode, but there is one hang report. Slightly slower handshake. Test ≥ 24 h of polling |
| Malloc threshold | `heap_caps_malloc_extmem_enable(512)` after boot. The core calls it with 4096 in `esp32-hal-psram.c:100` [local] | Less internal churn and fragmentation from String/JSON/FATFS. Size unknown: measure | Low-medium. Any library that DMAs from a plain `malloc()` buffer, or touches it while the cache is off (flash write), breaks. Arduino SPI/SD here use no DMA [unverified for SdStore paths] |
| ArduinoJson in PSRAM | `SpiRamAllocator` | Subset of the threshold change | Low |
| No NVS Wi-Fi writes | `WiFi.persistent(false)` before `WiFi.mode()`. Default `_persistent = true` → `WIFI_STORAGE_FLASH` [local: `WiFiGeneric.cpp:451/367`] | No runtime flash writes from Wi-Fi (IDF probably writes only when the config changes, e.g. on a WiFiMulti AP switch [unverified]) | None (credentials come from `secrets.h`) |
| Stacks | Measure `uxTaskGetStackHighWaterMark` (already logged for gw/wx), then trim. Merge `pw_wx` into `pw_gw` | 10 KB + trim | Low |
| PSRAM stacks | `xTaskCreateStaticPinnedToCore` with a PSRAM stack (allowed by the core) | 6-12 KB per task | **Medium-high:** a task with a PSRAM stack must never run while the cache is disabled. Not for TLS tasks (DMA). The SD task is the only candidate |
| Failed-alloc hook | `heap_caps_register_failed_alloc_callback()` logs the caller/caps of the first failure | Diagnosis | None |
| Drift insurance | `esp_lcd_rgb_panel_set_pclk()` around unavoidable flash ops (Arduino_GFX exposes the panel handle [unverified]) | | Brief blank |

### 4b. Needs a custom sdkconfig (see 4c for how)

| Option(s) | Gain | Risk | Bench? |
|---|---|---|---|
| `MBEDTLS_ASYMMETRIC_CONTENT_LEN=y`, `SSL_OUT_CONTENT_LEN=4096` | −12 KB/session | Low (IDF default) | No |
| `MBEDTLS_DYNAMIC_BUFFER` + `DYNAMIC_FREE_CONFIG_DATA` + `DYNAMIC_FREE_CA_CERT` | −~20 KB/session | Low-medium: the CA cert must be re-registered for a re-handshake. WiFiClientSecure re-parses on every `connect()` [unverified] | No |
| `MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=n` | −4 KB | Low | No |
| `MBEDTLS_EXTERNAL_MEM_ALLOC=y` | Same as the runtime hook | Same | No |
| `ESP_WIFI_STATIC_RX_BUFFER_NUM=4`, `RX_BA_WIN` smaller / AMPDU RX off | ~6 KB+ | Lower throughput (irrelevant for this app) | No |
| `ESP32S3_DATA_CACHE_LINE_64B=y` | Drift margin in bounce mode (FAQ) | Perf change, either way | **Yes** |
| `SPIRAM_XIP_FROM_PSRAM=y` | Display survives flash writes/OTA; cache stays on | Uses PSRAM equal to the app's text+rodata (~2 MB+ with `moon_photo`). Shares PSRAM bandwidth with the framebuffer. Crash reports only on QIO PSRAM | **Yes** |
| `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` + `EXT_RAM_BSS_ATTR` | Move big zero-init statics out | Low | No |
| `FREERTOS/RINGBUF/HEAP_PLACE_FUNCTIONS_INTO_FLASH` | A few KB IRAM → heap [web: RAM usage guide] | Low unless called from IRAM ISRs | Maybe |
| `ESP_TIMER_TASK_STACK_SIZE=4096` | 4 KB | Low if timers are light | No |
| `COMPILER_OPTIMIZATION_PERF` for the framework | Faster Wi-Fi/TLS/libc | Bigger binary | **Yes** |
| `LCD_RGB_ISR_IRAM_SAFE` | Pairs with XIP | Callbacks must be IRAM | **Yes** |

### 4c. How custom sdkconfig works in pioarduino 54.03.21, and what it costs

Read from `~/.platformio/platforms/espressif32/builder/frameworks/arduino.py` and `espidf.py` [local]:

- `custom_sdkconfig = ...` lines in an env (or a URL or file) trigger **"Hybrid compile"**. It runs a full ESP-IDF build of the Arduino libs (`call_compile_libs()` → `espidf.py`), then **copies the resulting `.a` files and `sdkconfig` into the global package `~/.platformio/packages/framework-arduinoespressif32-libs`** (`espidf.py` ~line 2185-2201). It writes `sdkconfig.defaults` into the project with an MD5 header (`# TASMOTA__<hash>`).
- `custom_component_remove` can drop unused components (Zigbee, RainMaker, esp-sr…) to speed the build. See `examples/tasmota_platformio_override.ini`.
- **Thrash:** `check_reinstall_frwrk()` reinstalls the stock libs (re-download) whenever any env without `custom_sdkconfig` builds after a custom build. A different project directory without the matching `sdkconfig.defaults` hash also triggers a reinstall plus a recompile. `tools/build-all.ps1` across 01-06 would therefore rebuild and re-download every time.
- **Isolation:** give 05 its own `[platformio] core_dir` (or set `PLATFORMIO_CORE_DIR` in `pio.ps1` for that project). The cost is a second toolchain/package download (~GBs) [unverified that this isolates fully].
- **`framework = arduino, espidf`** (Arduino as an IDF component) **switches the hybrid logic off** (`arduino.py` ~line 921). It builds IDF + Arduino from source *inside the project's `.pio`* with the project's own `sdkconfig.defaults`, so it doesn't touch the shared package.
- Build time for either route isn't documented anywhere I found [unverified; expect several minutes for a clean build, then incremental]. Known breakage: `custom_sdkconfig` link failure on 55.03.34 ([pioarduino #335](https://github.com/pioarduino/platform-espressif32/issues/335)); LTO must stay off in hybrid builds ([PR #353](https://github.com/pioarduino/platform-espressif32/pull/353)).

## 5. Arduino vs ESP-IDF for this app

| | Stay Arduino (now) | `custom_sdkconfig` hybrid | Arduino as IDF component (`arduino, espidf`) | Pure ESP-IDF |
|---|---|---|---|---|
| sdkconfig control | Runtime hooks only (4a) | Most options | Full | Full |
| Keeps our code (Arduino_GFX, WiFi, HTTPClient, SD patch, ArduinoJson) | Yes | Yes | Yes | No: port to `esp_lcd_st7701`, `esp_http_client`, `sdmmc`/`esp_vfs_fat` |
| Build | Fast, prebuilt | Long first build; **shared-package thrash** in this repo | Long clean build, per project, no thrash | Long clean build |
| Tooling (`pio.ps1`, `lint.ps1`, compiledb) | Works | Probably works | Needs checking (CMake-based) | Rework |
| Gains | | TLS −30 KB, XIP, cache line | Same, plus IDF components (`esp_lvgl_port`, OTA rollback) | Same, plus a cleaner memory map |
| Risk | Low | Medium (pioarduino edge cases) | Medium | High effort |

**What we would *not* gain:**
- `esp_lvgl_port`'s avoid-tearing (2 PSRAM framebuffers + direct mode) is the same approach as our v6/v7 experiments, which were measured **slower** ([hardware-log](hardware-log.md)).
- Its S3 SIMD blend helpers *might* help render time [unverified; not benchmarked here].

**Recommendation:** stay on Arduino and use the 4a hooks. Moving to a custom sdkconfig is not decided; these are the triggers:

| Switch to… | When |
|---|---|
| `arduino, espidf` for **05 only** (preferred middle path) | After the 4a fixes, polling min-free is still < ~80 KB or the largest block is < ~25 KB. **Or** drift appears with Wi-Fi/flash activity that bounce buffers don't cure (needs XIP + 64 B cache line). **Or** OTA with rollback is wanted |
| `custom_sdkconfig` instead | Only with an isolated `core_dir`, and only if `arduino, espidf` turns out to break `lint.ps1`/tooling |
| Pure ESP-IDF | Battery/light-sleep with fine power management, secure boot/flash encryption, or a move to the ESP32-P4 ([10](10-esp32-p4-displays.md)) where Arduino support lags. Not for performance alone |

## 6. What to try first in this repo (prioritised)

| # | Change | Expected gain | Risk | Measure |
|---|---|---|---|---|
| 0 | **Find the 40 KB**: compare `firmware.map` `.dram0.bss/.data` with the last commit; `heap_caps_print_heap_info(MALLOC_CAP_INTERNAL)` at boot and mid-poll | Knowing what to fix | None | Boot log |
| 1 | `WiFi.persistent(false)` | No runtime flash writes | None | — |
| 2 | mbedTLS calloc hook → PSRAM for ≥ 1 KB | +30-40 KB min-free [est.] | Medium (HW AES/DMA) | 05 `mem:` line: min ever + largest block over ≥ 24 h of polling, plus weather fetches; watch for TLS errors |
| 3 | Merge `pw_wx` into `pw_gw`; trim stacks to high-water + 1.5 KB | +10-15 KB | Low | Stack free in the `mem:` line |
| 4 | `heap_caps_malloc_extmem_enable(512)` (or `SpiRamAllocator` for JSON only) | Less fragmentation; largest block steadier | Low-medium | Largest block trend over a day |
| 5 | Optional: keep the gateway TLS session alive across polls (fixed-length endpoints only) | Fewer handshakes = less churn and CPU | Medium (the stall gotcha) | Poll latency, wdt |
| 6 | LVGL draw buffer 48 → 40 lines, only if still short | +7.7 KB | Render slower | **`03-lvgl-demos` `bench` env** with `-DB4848_LVGL_BUF_LINES=40` vs 48 |
| 7 | Trial `framework = arduino, espidf` in a branch copy of 05 with `sdkconfig.defaults`: mbedTLS asymmetric/dynamic/no-peer-cert, Wi-Fi RX 4, 64 B D-cache line, XIP | TLS −30 KB more; drift margin | Medium; long builds | Memory as #2. FPS/CPU via a matching `bench` env built the same way (perf options must be benchmarked, per repo rule) |
| 8 | 120 MHz PSRAM, double FB, 2 draw threads | — | High / measured slower | Don't |

**How to measure, consistently:**
- **Memory:** the existing 05 serial `mem:` line (internal free, largest block, min ever, stack free). Record it at boot, during a gateway poll and during a weather fetch, then after 1 h and 24 h.
- **Performance:** any change that can affect render or flush (cache line, XIP, optimisation level, malloc threshold, buffer lines) goes through `03-lvgl-demos` `env:bench`, compared against the v5 row in [hardware-log](hardware-log.md). Log results there.
- **Status:** results only count as "works on hw" once the user reports them.

## Sources
- Espressif LCD FAQ (drift, bounce buffer, cache line, PCLK limits, 120 MHz): https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html
- IDF 5.4 RGB LCD (bounce buffer, XIP, cache must stay on, PSRAM bandwidth split): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- IDF 5.4 external RAM (ALWAYSINTERNAL, RESERVE_INTERNAL, BSS, PSRAM stacks, XIP): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/external-ram.html
- IDF 5.4 RAM usage guide (IRAM→flash options, stack options, cache size): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/performance/ram-usage.html
- IDF 5.4 mbedTLS (heap table 42 KB → 22 KB): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/mbedtls.html
- mbedTLS Kconfig (alloc modes, asymmetric lengths, dynamic buffer): https://codex.ac6.fr/Zephyr/raw/hal_espressif-latest/components/mbedtls/Kconfig
- Espressif memory case study: https://developer.espressif.com/blog/esp32-memory-analysis-case-study/
- Espressif mbedTLS FAQ: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/mbedtls.html
- arduino-esp32 #9348 (XIP/RODATA defaults declined; QIO crash note): https://github.com/espressif/arduino-esp32/issues/9348
- arduino-esp32 #9636 (WiFiClientSecure memory): https://github.com/espressif/arduino-esp32/issues/9636
- LVGL forum: Wi-Fi horizontal offset, fixed by bounce buffer: https://forum.lvgl.io/t/lvgl-horizontal-offset-when-esp32-s3-wifi-enabled-esp-idf/21888
- LVGL forum: Waveshare S3 LVGL + Wi-Fi: https://forum.lvgl.io/t/waveshare-esp32-s3-touch-lcd-4-3b-lvgl-wifi-no-luck/21391
- esp32.com: S3 internal RAM overflow / largest block: https://esp32.com/viewtopic.php?p=152261
- esp32.com: INTERNAL vs DEFAULT mbedTLS alloc: https://esp32.com/viewtopic.php?p=127666
- mbedTLS in PSRAM on S3, HW AES caveat: https://git.commander1024.de/Commander1024/ESP32_Serial_Swiss_Army_Knife/commit/c018bfe361198e7b99f00121048ba588e27ba085
- ArduinoJson external RAM: https://arduinojson.org/v7/how-to/use-external-ram-on-esp32/
- Arduino as IDF component: https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html
- Arduino lib builder: https://docs.espressif.com/projects/arduino-esp32/en/latest/lib_builder.html
- pioarduino issues/PRs: https://github.com/pioarduino/platform-espressif32/issues/335, https://github.com/pioarduino/platform-espressif32/pull/353
- Arduino_GFX #465: https://github.com/moononournation/Arduino_GFX/issues/465
- Local: `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h`, `.../esp32s3/lib/libmbedcrypto.a` (nm), `~/.platformio/platforms/espressif32/builder/frameworks/{arduino,espidf}.py`, `~/.platformio/packages/framework-arduinoespressif32/{cores/esp32/esp32-hal-psram.c, cores/esp32/esp32-hal-misc.c, libraries/WiFi/src/WiFiGeneric.cpp}`
