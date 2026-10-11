# 01 Quirks and gotchas catalogue

[Guide index](README.md) | [01 Quirks](01-quirks-and-gotchas.md) | [02 Debugging](02-debugging-playbook.md) | [03 Investigations](03-investigations.md) | [04 Patterns](04-patterns-and-decisions.md) | [05 New project](05-new-project-checklist.md) | [06 Hardware](06-hardware-and-sourcing.md) | [Credits](99-sources-and-credits.md)

Every hardware, toolchain, library, framework, network-API and tooling quirk found so far on the Guition ESP32-4848S040 (ESP32-S3 N16R8, 480x480 ST7701 RGB panel, GT911 touch, microSD, CH340C) with PlatformIO/pioarduino (Arduino core 3.2.1, IDF 5.4) and LVGL 9.5. It is a catalogue: look an entry up by ID, check the status, apply the fix. The reasoning behind the bigger ones is in [03-investigations.md](03-investigations.md).

## How to read an entry

| Field | Meaning |
|---|---|
| Symptom | What you see |
| Root cause | Why, with the reasoning. Marked "(inferred)" when it is not proven |
| Fix | What we did, with repo paths |
| Evidence | Measurement, log line, commit or date. All dates are 2026-10-08 to 2026-10-11 |
| Status | `fixed`, `worked-around`, `open`, or `documented` (nothing to fix, just know it) |
| Applies to | How far it generalises: this board only, any ESP32-S3 RGB panel, any Arduino-ESP32 3.x, and so on |

Conventions:
- "Unit 1" is the one board tested. A hardware result is only claimed where the user reported it or a captured log shows it. Everything else is marked "(unverified)".
- Commit hashes are from this repo's history. Log lines are scrubbed: Wi-Fi names appear as `<home-ssid>`, addresses and serials are removed.
- Investigation links use `03-investigations.md#inv-<slug>`.
- Related reading: [docs/07-gotchas.md](../07-gotchas.md) (the original short symptom table), [docs/hardware-log.md](../hardware-log.md) (measurements), [docs/12](../12-esp32-performance-and-framework.md) (memory and framework research).

## Index

Status key: F fixed, W worked-around, O open, D documented.

### HW: board and hardware

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-HW-01](#q-hw-01) | Sticker and listings name a different model | D | This board family |
| [Q-HW-02](#q-hw-02) | Octal PSRAM needs `qio_opi` | F | ESP32-S3 with octal PSRAM |
| [Q-HW-03](#q-hw-03) | Native USB is unavailable; USB-CDC-on-boot must be 0 | F | Boards that reuse GPIO19/20 |
| [Q-HW-04](#q-hw-04) | Strapping pins carry the LCD and touch bus | D | This board |
| [Q-HW-05](#q-hw-05) | IO1, IO2 and IO40 are shared (relays, I2S, a second I2C bus) | D | This board |
| [Q-HW-06](#q-hw-06) | `ESP.restart()` once came back as an RTC watchdog reset | O | This board (maybe any RGB panel) |
| [Q-HW-07](#q-hw-07) | Wrong or misleading reference data | D | Reference repos for this board |
| [Q-HW-08](#q-hw-08) | Power, battery and RF notes (schematic only) | D | This board |

### GFX: display and backlight

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-GFX-01](#q-gfx-01) | Flicker while the CPU draws | F | Any ESP32-S3 RGB panel with a PSRAM framebuffer |
| [Q-GFX-02](#q-gfx-02) | Constant crackle under combined PSRAM load | W | Same |
| [Q-GFX-03](#q-gfx-03) | Rotated flush about 2x slow in Arduino_GFX | F (rotation 3) | Arduino_GFX RGB display + LVGL partial rendering |
| [Q-GFX-04](#q-gfx-04) | Backlight PWM frequency silently ignored | F | Any Arduino-ESP32 3.x |
| [Q-GFX-05](#q-gfx-05) | Backlight cut-out and a nearly invisible slider range | O | This board |
| [Q-GFX-06](#q-gfx-06) | `initDisplay()` aborts instead of failing when PSRAM is short | F | Arduino_GFX RGB panels |
| [Q-GFX-07](#q-gfx-07) | RGB timing variants, drift reports and a PCLK typo | D | ST7701 RGB panels |
| [Q-GFX-08](#q-gfx-08) | PSRAM is unreachable during flash writes (not seen here) | D | Any ESP32-S3 RGB panel |

### TOUCH: GT911

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-TOUCH-01](#q-touch-01) | INT and RST are not wired | D | This board |
| [Q-TOUCH-02](#q-touch-02) | "No new data" is not "released" | F | Any GT911 polling driver |
| [Q-TOUCH-03](#q-touch-03) | `begin()` accepted any device at the address | F | Any I2C probe |
| [Q-TOUCH-04](#q-touch-04) | Touch must be rotated with the display; reference INT mapping is backwards | D | Any GT911 + rotated RGB panel |

### SD: microSD

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-SD-01](#q-sd-01) | Core 3.2.1 SD driver fails multi-block writes | F (patched copy) | Arduino-ESP32 3.2.1 SD over SPI |
| [Q-SD-02](#q-sd-02) | Old 2 GB card fails above 4 MHz; the rated clock is meaningless | W | Any SD over SPI |
| [Q-SD-03](#q-sd-03) | SD shares the LCD init bus; default SPI pins are LCD pins | W | This board |
| [Q-SD-04](#q-sd-04) | No card-detect pin | W | This board |
| [Q-SD-05](#q-sd-05) | Harmless "no token received" warning on every mount | D | Arduino-ESP32 SD |
| [Q-SD-06](#q-sd-06) | Filesystem constraints and power-cut-safe writes | W | Any FatFs on SD |
| [Q-SD-07](#q-sd-07) | Inserting a card can drop the PC's USB-serial link | O | This board + CH340 |

### MEM: memory

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-MEM-01](#q-mem-01) | mbedTLS allocates only from internal RAM | W | Arduino-ESP32 core 3.2.1 prebuilt libs |
| [Q-MEM-02](#q-mem-02) | Moving mbedTLS to PSRAM: the threshold matters | W | Same |
| [Q-MEM-03](#q-mem-03) | Allocations under 4 KB always go to internal RAM | W | Any Arduino-ESP32 3.x with PSRAM |
| [Q-MEM-04](#q-mem-04) | Total free hides the largest block and the low-water mark | D | Any ESP32 with TLS |
| [Q-MEM-05](#q-mem-05) | Speed options spend internal RAM that TLS needs | D | LVGL + Wi-Fi/TLS projects |
| [Q-MEM-06](#q-mem-06) | `loopTask` 8 KB stack overflows under LVGL | F | Any Arduino-ESP32 with LVGL in `loop()` |

### LVGL

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-LVGL-01](#q-lvgl-01) | `LV_STDLIB_CLIB` puts every widget in internal RAM | F | LVGL 9 on ESP32 with PSRAM |
| [Q-LVGL-02](#q-lvgl-02) | The built-in TJPGD decoder inspects every image | F | LVGL 9 with `LV_USE_TJPGD` |
| [Q-LVGL-03](#q-lvgl-03) | Montserrat has ASCII and the degree sign only | D | LVGL built-in fonts |
| [Q-LVGL-04](#q-lvgl-04) | `lv_obj_get_height()` before the first layout is not the real height | F | LVGL 9 |
| [Q-LVGL-05](#q-lvgl-05) | Objects are clickable by default and swallow taps and swipes | F | LVGL 9 |
| [Q-LVGL-06](#q-lvgl-06) | Hidden screens keep animating | F | LVGL 9 |
| [Q-LVGL-07](#q-lvgl-07) | Image cache is keyed by the descriptor | D | LVGL 9 |

### PERF: performance

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-PERF-01](#q-perf-01) | `-O2` in `build_flags` has no effect | F | PlatformIO + Arduino-ESP32 |
| [Q-PERF-02](#q-perf-02) | A bigger draw buffer was the biggest win | D | LVGL partial rendering on this board |
| [Q-PERF-03](#q-perf-03) | Double framebuffers and two draw threads are slower | D | This board / LVGL 9.5 |
| [Q-PERF-04](#q-perf-04) | Idle animation redraws are the standing load | W | LVGL on a PSRAM RGB panel |

### NET: network stack

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-NET-01](#q-net-01) | HTTPClient's chunked reader stalls on a kept-alive TLS socket | W | Arduino-ESP32 3.2.1 HTTPClient |
| [Q-NET-02](#q-net-02) | Chunked JSON parses as a number | W | Arduino-ESP32 HTTPClient + ArduinoJson |
| [Q-NET-03](#q-net-03) | Default TLS handshake timeout is 120 s | F | Arduino-ESP32 WiFiClientSecure |
| [Q-NET-04](#q-net-04) | The gateway refuses some handshakes at about 30 per minute | O | Powerwall 3 gateway |
| [Q-NET-05](#q-net-05) | One TLS session at a time; job time budget vs stale data | F | Any RAM-tight ESP32 |
| [Q-NET-06](#q-net-06) | TLS needs a valid clock first | F | Any ESP32 with certificate checks |
| [Q-NET-07](#q-net-07) | Which root CA to pin for which host | D | The hosts listed |
| [Q-NET-08](#q-net-08) | A truncated URL gives HTTP 400 | F | Any `snprintf` URL |
| [Q-NET-09](#q-net-09) | 2.4 GHz only | D | Any ESP32 |
| [Q-NET-10](#q-net-10) | Wi-Fi settings are saved to flash by default | F | Any Arduino-ESP32 3.x |

### PW: Powerwall LAN API

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-PW-01](#q-pw-01) | Only three endpoints answer on basic access | D | Powerwall 3, firmware 26.26.4 |
| [Q-PW-02](#q-pw-02) | Login, token expiry and lockout risk | F | Same |
| [Q-PW-03](#q-pw-03) | `/aggregates` is chunked; a missing meter must not become 0 | F | Same |
| [Q-PW-04](#q-pw-04) | Raw state of charge includes a hidden 5 % reserve | W | Same |
| [Q-PW-05](#q-pw-05) | Lifetime counters: resets, gaps and persistence | F | Same |
| [Q-PW-06](#q-pw-06) | The gateway may be on another network segment | D | Home networks with VLANs |
| [Q-PW-07](#q-pw-07) | The grid-services (VPP) flag is untested | O | Same |

### DATA: public data sources

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-DATA-01](#q-data-01) | Many tutorial SWPC endpoints are gone | D | NOAA SWPC |
| [Q-DATA-02](#q-data-02) | SWPC JSON shapes vary file to file | F | NOAA SWPC |
| [Q-DATA-03](#q-data-03) | "A0.0" flare class during a GOES data gap | F | NOAA SWPC |
| [Q-DATA-04](#q-data-04) | History files are too big for the chip | D | NOAA SWPC, CPC |
| [Q-DATA-05](#q-data-05) | NASA DONKI returned a web page, not JSON | D | NASA DONKI |
| [Q-DATA-06](#q-data-06) | SDO "latest" images were three weeks stale | D | NASA SDO |
| [Q-DATA-07](#q-data-07) | Only baseline JPEG decodes; the decoder is on your side | D | TJpgDec |
| [Q-DATA-08](#q-data-08) | CPC ENSO: Range works, caching headers do not, the page is HTML | W | NOAA CPC |
| [Q-DATA-09](#q-data-09) | AuroraWatch UK and MET Norway ask for a User-Agent and credit | D | Those APIs |
| [Q-DATA-10](#q-data-10) | Open-Meteo rain chance depends on the model | D | Open-Meteo |

### FW: firmware patterns

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-FW-01](#q-fw-01) | Static-init order: a mutex used before `start()` runs | F | Any FreeRTOS code with early tasks |
| [Q-FW-02](#q-fw-02) | Network tasks at idle priority; the task watchdog watches IDLE0 | W | Arduino-ESP32 3.x |
| [Q-FW-03](#q-fw-03) | No runtime flash writes: RTC memory semantics | D | Any ESP32 |
| [Q-FW-04](#q-fw-04) | `millis()` back-off logic breaks after about 25 days | F | Any 32-bit tick counter |
| [Q-FW-05](#q-fw-05) | Padding in checksummed snapshots | F | Any raw-struct CRC |
| [Q-FW-06](#q-fw-06) | LEDC: `ledcReadFreq()` is 0 at duty 0; auto clock tops out below 80 MHz | F | Arduino-ESP32 3.2.x on the S3 |

### TOOL: toolchain and workflow

| ID | Title | Status | Applies to |
|---|---|---|---|
| [Q-TOOL-01](#q-tool-01) | Git Bash/MSYS breaks ESP's `idf_tools.py` | F | pioarduino/ESP-IDF on Windows |
| [Q-TOOL-02](#q-tool-02) | Scripted edits through the Bash tool turn `\n` in C strings into real newlines | W | Claude Code Bash tool on Windows; Python strings |
| [Q-TOOL-03](#q-tool-03) | Platform and PlatformIO Core pinning | W | pioarduino |
| [Q-TOOL-04](#q-tool-04) | `custom_sdkconfig` rewrites the shared framework package | D | pioarduino 54.03.21 |
| [Q-TOOL-05](#q-tool-05) | Opening the serial port resets the board | W | CH340 auto-reset boards |
| [Q-TOOL-06](#q-tool-06) | Concurrent `pio` runs collide | W | PlatformIO |
| [Q-TOOL-07](#q-tool-07) | Linting needs a translated compile database; "clean" can be an illusion | F | PlatformIO + clang-tidy |
| [Q-TOOL-08](#q-tool-08) | Windows shell and runtime quirks | W | Windows PowerShell 5.1/7 |
| [Q-TOOL-09](#q-tool-09) | Disk and partition changes need elevation | W | Windows |
| [Q-TOOL-10](#q-tool-10) | esptool v5 syntax and finding the port | D | PlatformIO's bundled esptool |
| [Q-TOOL-11](#q-tool-11) | Public-repo hygiene: secrets and identifiers in history | W | Any repo going public |
| [Q-TOOL-12](#q-tool-12) | Decoding a crash backtrace | D | Any Arduino-ESP32 firmware |

---

## HW: board and hardware

<a id="q-hw-01"></a>
### Q-HW-01 Sticker and listings name a different model
- **Symptom:** The sticker may read `ESP32_3838S040C_I`. Searches for that name find nothing. Some marketplace listings mix specs (one showed "480x800" beside a 480x480 title).
- **Root cause:** `4848` is 480x480, `S` is ESP32-S3, `040` is 4.0". The PCB silkscreen, the vendor schematic and every reference repo say 4848S040. `C` is capacitive touch, `_I` is undefined (most likely IPS), `_Y_1`/`_Y_3` are the 1- and 3-relay variants.
- **Fix:** Search for ESP32-4848S040. Pick the no-relay `C_I` variant unless you want mains relays. Check the photos show the square board.
- **Evidence:** [docs/01](../01-hardware-overview.md) naming section; first session, 2026-10-08.
- **Status:** documented. **Applies to:** this board family.

<a id="q-hw-02"></a>
### Q-HW-02 Octal PSRAM needs `qio_opi`
- **Symptom:** Boot loop or `PSRAM init failed`. `initDisplay()` returns `nullptr`.
- **Root cause:** The N16R8 module has 8 MB of octal PSRAM, not quad. A quad PSRAM setting cannot talk to it. GPIO35-37 are taken by the octal bus.
- **Fix:** `board_build.arduino.memory_type = qio_opi` in `common/platformio-base.ini` (Arduino IDE: "OPI PSRAM"). Keep it.
- **Evidence:** Unit 1 reports 8,384,052 bytes of PSRAM free at boot with `qio_opi` ([hardware-log](../hardware-log.md)).
- **Status:** fixed. **Applies to:** any ESP32-S3 module with octal PSRAM (N16R8, N8R8).

<a id="q-hw-03"></a>
### Q-HW-03 Native USB is unavailable; USB-CDC-on-boot must be 0
- **Symptom:** Nothing appears on `Serial` when `ARDUINO_USB_CDC_ON_BOOT=1`. No USB-OTG, HID or USB-JTAG.
- **Root cause:** GPIO19/20 (the native USB D-/D+) are used for touch SDA and LCD green bit 1. The USB-C data lines go only to the CH340C, which feeds UART0. So `Serial` must be UART0.
- **Fix:** `-DARDUINO_USB_CDC_ON_BOOT=0` in `common/platformio-base.ini`. Never remove it.
- **Evidence:** [docs/02](../02-pinout.md), [docs/03](../03-programming-and-flashing.md), `Board4848S040_pins.h`. Serial at 115200 works on unit 1 in every project.
- **Status:** fixed. **Applies to:** boards that route USB-C to a UART bridge and reuse GPIO19/20.

<a id="q-hw-04"></a>
### Q-HW-04 Strapping pins carry the LCD and touch bus
- **Symptom:** Strapping warnings in ESPHome (`ignore_strapping_warning: true` is needed on GPIO45). Boot oddities if you load these pins.
- **Root cause:** GPIO0 (LCD R4, BOOT button), GPIO3 (G2), GPIO46 (G3) and GPIO45 (touch SCL) are strapping pins. GPIO45 high at reset would select 1.8 V VDD_SPI. The board works because the module's eFuse forces 3.3 V (inferred from the datasheet and the schematic; not measured).
- **Fix:** Don't add loads to the touch bus that change its level at reset. Be careful if you swap the module. BOOT held at reset enters download mode.
- **Evidence:** [docs/02](../02-pinout.md) strapping section; the board boots reliably on unit 1.
- **Status:** documented. **Applies to:** this board; any design that reuses strapping pins for peripherals.

<a id="q-hw-05"></a>
### Q-HW-05 IO1, IO2 and IO40 are shared (relays, I2S, a second I2C bus)
- **Symptom:** Relays click when audio plays, or there is no sound.
- **Root cause:** Each of the three pins goes to the relay header (0 ohm links R25-27, fitted by default) or to the NS4168 amp (R21-23). The amp's CTRL pin is pulled high through R24, so it is always enabled and plays the right channel only. Relay pins float until firmware drives them. Relay polarity is unverified (community configs drive them inverted).
- **Fix:** Move the links to use audio. Send mono to both I2S slots. Drive relay pins to a safe level early in `setup()`. A second I2C bus (`Wire1.begin(1, 40)`) needs both links removed.
- **Evidence:** [docs/02](../02-pinout.md). The hardware log lists audio and relays as "not yet tested".
- **Status:** documented, untested. **Applies to:** this board.

<a id="q-hw-06"></a>
### Q-HW-06 `ESP.restart()` once came back as an RTC watchdog reset
- **Symptom:** Restarting from the `03-lvgl-demos` launcher once booted with `rst:0x10 (RTCWDT_RTC_RST)` instead of a software reset. It recovered by itself.
- **Root cause:** Not found. Suspected: restarting while the RGB panel DMA is running (inferred).
- **Fix:** None. Project 05's serial `r` command also calls `ESP.restart()`; no repeat was recorded.
- **Evidence:** Serial capture on 2026-10-10 shows `rst:0xc (RTC_SW_CPU_RST)` followed by `rst:0x10 (RTCWDT_RTC_RST)`; [hardware-log](../hardware-log.md), [projects/03 README](../../projects/03-lvgl-demos/README.md). Investigation: [inv-rtc-wdt-restart](03-investigations.md#inv-restart-rtcwdt).
- **Status:** open. **Applies to:** this board; possibly any RGB panel (unverified).

<a id="q-hw-07"></a>
### Q-HW-07 Wrong or misleading reference data
- **Symptom:** Code copied from a reference repo or vendor sheet behaves wrongly.
- **Root cause and fix, one per item:**
  - The vendor IO sheet labels the SD MISO pin "D1". It is DAT0 (GPIO41).
  - The rzeldent board JSONs define `LED_GPIO=16`. GPIO16 is HSYNC. Don't use that define.
  - esp32-smartdisplay's GT911 code and a joevial comment state the INT-to-address mapping backwards. The GT911 datasheet is the authority ([Q-TOUCH-04](#q-touch-04)).
  - aquaElectronics passes `12000` in the `pclk_active_neg` slot ([Q-GFX-07](#q-gfx-07)).
  - Arduino_GFX reports a regression for this panel between 1.2.9 and 1.4.6 (Arduino_GFX issue 465). We pin 1.6.9 in `common/lib/Board4848S040/library.json`. Re-test after any bump.
  - Rule: the vendor schematic is the pin authority; pins live only in `Board4848S040_pins.h`.
- **Evidence:** [docs/02](../02-pinout.md), [docs/04](../04-display-st7701.md), [docs/05](../05-touch-gt911.md), [docs/12](../12-esp32-performance-and-framework.md) section 3. First-session cold review, 2026-10-08.
- **Status:** documented. **Applies to:** the reference repos named.

<a id="q-hw-08"></a>
### Q-HW-08 Power, battery and RF notes (schematic only)
- **Symptom:** None observed. These are traps for later work.
- **Root cause and notes:** All from the schematic; nothing was measured ([docs/08](../08-power-battery-sleep.md)).
  - The module is a WROOM-1U with a U.FL socket. Don't run Wi-Fi without the antenna.
  - No GPIO senses the battery, so there is no fuel gauge without a hardware change.
  - The IP5306-class charger may switch off its boost output at very light load (unverified).
  - The amp cannot be muted.
  - Deep-sleep wake by touch is impossible (INT is not wired). Timer and the BOOT button work.
  - Relay variants carry mains on the daughterboard.
- **Fix:** Use the screen-off pattern (backlight off, UI paused) instead of deep sleep on a powered wall panel.
- **Evidence:** [docs/01](../01-hardware-overview.md), [docs/08](../08-power-battery-sleep.md).
- **Status:** documented, unverified on hardware. **Applies to:** this board.

---

## GFX: display and backlight

<a id="q-gfx-01"></a>
### Q-GFX-01 Flicker while the CPU draws
- **Symptom:** The whole screen flickers while finger-painting (project 01).
- **Root cause:** The LCD peripheral reads the PSRAM framebuffer continuously. CPU writes into the same PSRAM compete for bandwidth and the DMA runs short.
- **Fix:** Bounce buffers: a small ring in internal RAM that an ISR refills from PSRAM. `initDisplay(rotation, bouncePx = B4848_LCD_BOUNCE_PX)` enables 480x10 pixels by default (about 19 KB internal). The value must divide 480x480.
- **Evidence:** Without them the whole screen flickered; with them it was stable, confirmed by the user on 2026-10-10 (commit `fa441f3`, [hardware-log](../hardware-log.md)). Investigation: [inv-bounce-buffer-flicker](03-investigations.md#inv-flicker-bounce).
- **Status:** fixed. **Applies to:** any ESP32-S3 RGB panel with a PSRAM framebuffer.

<a id="q-gfx-02"></a>
### Q-GFX-02 Constant crackle under combined PSRAM load
- **Symptom:** User report, 2026-10-11: after the display had run a while it crackled (sparkled), and "once it starts it becomes constant". Project 05 with Wi-Fi, TLS and a JPEG decode.
- **Root cause (inferred):** More PSRAM traffic than the bounce buffers could hide. The suspect was the mbedTLS handshake arithmetic: after the first RAM fix every mbedTLS allocation of 1 KB or more went to PSRAM, and the gateway handshake runs every 2 s. The hourly sun-image decode (about 0.8 s) was a smaller suspect.
- **Fix:** Three changes in commit `300e7b5`: mbedTLS blocks go to PSRAM only at 4 KB and up ([Q-MEM-02](#q-mem-02)); bounce buffers doubled to 480x20 in `projects/05-powerwall-display/src/main.cpp` (about 38 KB internal); the El Nino ticker was slowed.
- **Evidence:** User report; the changes. Confirmed visually by the owner, 2026-10-11 ("screen looks good now") with all the mitigations in place (2x bounce buffers, 4 KB threshold, rotation-3 flush path, gateway keep-alive). Later 10-minute soaks also show no unusual frame gaps ([Q-GFX-03](#q-gfx-03)). Investigation: [inv-crackle-psram-contention](03-investigations.md#inv-crackle).
- **Status:** worked-around (visually confirmed by the owner, 2026-10-11). **Applies to:** any RGB panel on PSRAM with other PSRAM-heavy work.

<a id="q-gfx-03"></a>
### Q-GFX-03 Rotated flush about 2x slow in Arduino_GFX
- **Symptom:** Visible stutter on project 05 at `SCREEN_ROTATION 3`. Many-area redraws took about 300 ms; a full-screen redraw took 254 ms.
- **Root cause:** With software rotation, `draw16bitRGBBitmap` writes an LVGL strip column by column. Each pixel lands on a different 960-byte framebuffer row, so the PSRAM cache thrashes. It then writes back every full framebuffer row it touched: about 460 KB of cache write-back for a 480x48 strip that changed 46 KB.
- **Fix:** `flushRot3()` in `common/lib/Board4848S040/src/Board4848S040_lvgl.cpp` writes each framebuffer row as one contiguous segment and writes back only that segment (`Cache_WriteBack_Addr`). Other rotations still use the Arduino_GFX path.
- **Evidence:** Full-screen redraw 254 ms to 142 ms; the ~300 ms many-area redraws to 145-185 ms ([hardware-log](../hardware-log.md), 2026-10-11, commit `bd3f28d`). The post-fix 10-minute soaks end with max frame gaps of 54-95 ms on the main screen (one earlier window showed 307 ms). 03 benchmark A/B: full-screen flush 110-152 ms with Arduino_GFX's rotated draw vs 27 ms with `flushRot3()` (20 ms at rotation 0); average 29 vs 33 FPS (35 at rotation 0).
- **Status:** fixed for rotation 3. Owner, 2026-10-11: stutter "a lot less noticeable"; ~135-190 ms frames remain when all power-flow labels and the top-bar Wi-Fi label redraw together (the Wi-Fi label throttle is now in; the rest is power-flow labels + chevrons). Investigation: [inv-rotated-flush-stutter](03-investigations.md#inv-stutter-rotated-flush). **Applies to:** any Arduino_GFX RGB display with LVGL partial rendering and software rotation.

<a id="q-gfx-04"></a>
### Q-GFX-04 Backlight PWM frequency silently ignored
- **Symptom:** Coil whine from the backlight. The code said 100 kHz.
- **Root cause:** `analogWriteFrequency()` was called before the pin was attached. Arduino-ESP32 3.x ignores that silently and the LEDC channel stayed at 1 kHz.
- **Fix:** `ledcAttach(B4848_LCD_BL, 100000, 8)` first, then `ledcWrite`, in `setBacklight()`.
- **Evidence:** Found independently by three of four cold reviewers (commit `dca099e`, 2026-10-08). On unit 1, 255 down to 100 dims smoothly with no audible whine (2026-10-10).
- **Status:** fixed. **Applies to:** any Arduino-ESP32 3.x code that sets a PWM frequency.

<a id="q-gfx-05"></a>
### Q-GFX-05 Backlight cut-out and a nearly invisible slider range
- **Symptom:** The driver turns off below about 100/255 at 100 kHz. In project 05 the brightness slider (duty 100-255) barely changes anything (user report, 2026-10-11).
- **Root cause (inferred):** The range is only 2.5x in duty, and perceived brightness is roughly logarithmic. The boost driver (U5, dimmed by gating its EN pin) also needs start-up time on each pulse. At 100 kHz a period is 10 us, so low and mid duties are compressed. That would explain a cut-out near 100 at 100 kHz but about 60 at a lower frequency.
- **Fix:** Code is in (builds, not yet tried on hardware): configurable PWM frequency and resolution in the board library, a gamma slider in 05, serial `bf`/`bd`/`br`/`bs` for the sweep ([docs/02](../02-pinout.md) "Backlight dimming range" has the procedure). Still to do on the board: sweep 100/50/25/20/5 kHz with someone watching and listening, pick the lowest silent frequency, re-measure the minimum, then change `B4848_LCD_BL_FREQ_HZ` and `B4848_LCD_BL_MIN`. A hardware mod (R12, FB wire) is the fallback.
- **Evidence:** Unit 1 is still lit at 100; the true cut-out is not measured ([hardware-log](../hardware-log.md)). Investigation: [inv-backlight-pwm](03-investigations.md#inv-backlight-pwm).
- **Status:** open (tooling and gamma slider implemented, hardware sweep pending). **Applies to:** this board.

<a id="q-gfx-06"></a>
### Q-GFX-06 `initDisplay()` aborts instead of failing when PSRAM is short
- **Symptom:** A reboot loop instead of a clean error. The `if (!gfx)` check never ran.
- **Root cause:** Arduino_GFX allocates the 460 KB framebuffer with `ESP_ERROR_CHECK`, which aborts rather than returning false.
- **Fix:** `initDisplay()` in `common/lib/Board4848S040/src/Board4848S040.cpp` checks `psramFound()` and free PSRAM first and returns `nullptr` (commit `dca099e`).
- **Evidence:** Code review finding, 2026-10-08.
- **Status:** fixed. **Applies to:** Arduino_GFX RGB panels.

<a id="q-gfx-07"></a>
### Q-GFX-07 RGB timing variants, drift reports and a PCLK typo
- **Symptom (reports from other projects):** Image shifted, rolling or drifting after a while.
- **Root cause and notes:**
  - aquaElectronics passes `12000` in the `pclk_active_neg` argument. That field is a 1-bit bitfield and 12000 is even, so it truncates to 0. Their effective timing equals ours: PCLK 12 MHz from Arduino_GFX's default. We had first documented it as an edge inversion and fixed the doc and the constructor arguments (commits `27bf530`, `dca099e`).
  - The ESP-IDF First-Start project says any non-zero porch drifts after about 100 cycles and uses zero porches with a different init sequence. We did not reproduce this: ours is 10/8/50 and 10/8/20 with the aquaElectronics-identical init table.
  - The core ships `CONFIG_LCD_RGB_RESTART_IN_VSYNC=1`, which recovers from drift.
- **Fix:** None needed on unit 1. The variants table is in [docs/04](../04-display-st7701.md). Fallbacks if drift appears: larger bounce buffer, lower PCLK, zero porches.
- **Evidence:** Unit 1: colours correct and image stable at 12 MHz, 10/8/50 and 10/8/20 ([hardware-log](../hardware-log.md)).
- **Status:** documented. **Applies to:** ST7701 RGB panels on ESP32-S3.

<a id="q-gfx-08"></a>
### Q-GFX-08 PSRAM is unreachable during flash writes (not seen here)
- **Symptom (from a forum report):** The image shifts sideways when Wi-Fi is on or when flash is written (NVS, OTA).
- **Root cause:** Code and read-only data run from flash; while flash is written the cache is off and PSRAM cannot be read, so the RGB DMA underruns.
- **Fix:** Bounce buffers (we have them). Further options need a custom sdkconfig: `SPIRAM_XIP_FROM_PSRAM`, a 64-byte data cache line. We avoid runtime flash writes altogether ([Q-FW-03](#q-fw-03), [Q-NET-10](#q-net-10)).
- **Evidence:** [docs/12](../12-esp32-performance-and-framework.md) section 3 (LVGL forum thread, Espressif LCD FAQ). Not observed on unit 1. A crash report exists for `FETCH_INSTRUCTIONS` on QIO-PSRAM parts (arduino-esp32 issue 9348); we have octal.
- **Status:** documented. **Applies to:** any ESP32-S3 RGB panel with a PSRAM framebuffer.

---

## TOUCH: GT911

<a id="q-touch-01"></a>
### Q-TOUCH-01 INT and RST are not wired
- **Symptom:** No interrupt line. The I2C address cannot be forced.
- **Root cause:** RST shares the LCD's RC power-on reset (R16/C22). INT runs only between the two FPCs and floats. The GT911 latches its address from INT at reset, so the address depends on the flex and power-up, not firmware.
- **Fix:** Poll. `board4848::Touch::begin()` probes 0x5D then 0x14.
- **Evidence:** GT911 at 0x5D, product ID "911" on unit 1 ([hardware-log](../hardware-log.md)). Pins: [docs/02](../02-pinout.md), [docs/05](../05-touch-gt911.md).
- **Status:** documented. **Applies to:** this board.

<a id="q-touch-02"></a>
### Q-TOUCH-02 "No new data" is not "released"
- **Symptom:** Every finger lift was delayed by about 60 ms (laggy taps).
- **Root cause:** The first driver could not tell "status bit 7 clear" (no new report) from a genuine release, which the GT911 reports as ready with zero points. It held the last press for a timeout.
- **Fix:** `Touch::read()` returns -1 for no new data, 0 for released, n for n points. Callers must test `> 0` for a press. Always write 0 to register 0x814E after reading. The LVGL indev in `Board4848S040_lvgl.cpp` keeps the last state between reports and adds a 100 ms fallback.
- **Evidence:** Code review, commit `dca099e`. Finger drawing tracks correctly and taps count once each on unit 1 (2026-10-10).
- **Status:** fixed. **Applies to:** any GT911 polling driver.

<a id="q-touch-03"></a>
### Q-TOUCH-03 `begin()` accepted any device at the address
- **Symptom:** Touch "found" when some other I2C device answered.
- **Root cause:** The first version checked only for an ACK.
- **Fix:** Read the product ID (0x8140) and require ASCII "911".
- **Evidence:** Code review, commit `dca099e`.
- **Status:** fixed. **Applies to:** any I2C device probe.

<a id="q-touch-04"></a>
### Q-TOUCH-04 Touch must be rotated with the display; reference INT mapping is backwards
- **Symptom:** Touch axes are wrong after rotating the display.
- **Root cause:** Touch coordinates are in panel space. Arduino_GFX rotates drawing in software.
- **Fix:** Call `touch.setRotation(r)` with the same value as `initDisplay(r)`. The transform is the inverse of Arduino_GFX's mapping. Some reference code (esp32-smartdisplay, a joevial comment) states the INT-to-address mapping backwards; INT low during reset gives 0x5D, INT high gives 0x14.
- **Evidence:** Project 05 runs rotation 3 and its taps and swipes work. Corner accuracy is untested ([hardware-log](../hardware-log.md)).
- **Status:** documented. **Applies to:** any GT911 on a rotated RGB panel.

---

## SD: microSD

<a id="q-sd-01"></a>
### Q-SD-01 Core 3.2.1 SD driver fails multi-block writes
- **Symptom:** Any write of 1 KB or more failed intermittently, at every SPI clock from 1 to 25 MHz and on two different cards. Small writes (the 500 log-line append test, about 17 ms each) always passed. The driver log showed a CRC error or `no token received` just before the failing write.
- **Root cause:** After the Stop Tran token (0xFD) of a multi-block write, the card answers with one 0xFF byte and only then goes busy programming the last blocks. `sdStop()` in the core's `sd_diskio.cpp` deselected the card straight away. The next command's wait saw that 0xFF as "ready", sent CMD13 into a busy card, and the whole write failed. The result depends on timing, hence random and clock-independent.
- **Fix:** Patched copy of the SD library at `common/lib/SD`: after the Stop Tran token read one 0xFF byte, then wait for the busy period (`sdWait(pdrv, 1000)`). Projects list `symlink://../../common/lib/SD` before the core's. Re-apply when the core is upgraded ([README-mini-screen.md](../../common/lib/SD/README-mini-screen.md)).
- **Evidence:** 16 GB card, 2026-10-11: 7 of 10 runs failed at least one test before the fix; 4 of 4 full runs passed 19/19 after, at every clock 1-25 MHz. A 1 MB write went from about 300 KB/s to about 670 KB/s; read about 1.47 MB/s. A backlight-off run ruled out a supply dip (3 of 4 runs still failed). Commit `8892191`. Not reported upstream by us; whether newer cores fixed it is unknown (unverified).
- **Status:** fixed (patched copy). Investigation: [inv-sd-multiblock-write](03-investigations.md#inv-sd-multiblock-write). **Applies to:** Arduino-ESP32 3.2.1 `SD` over SPI; likely other 3.x releases (unverified).

<a id="q-sd-02"></a>
### Q-SD-02 Old 2 GB card fails above 4 MHz; the rated clock is meaningless
- **Symptom:** A 2011 SanDisk 2 GB SDSC card returned corrupt responses at 10 MHz and above, and was flaky at 4 MHz even with the write fix.
- **Root cause:** The card's CSD says 25 MHz, but nearly every card does, so the register says nothing about what the link can really manage. An old card probably has less timing margin on this wiring (inferred). Running too fast can corrupt data without any card error.
- **Fix:** Card retired. `common/lib/SdStore` chooses the clock by testing: a 32 KB write and read-back link test at mount, a ladder 25/20/10/4/1 MHz, retry then step down on I/O errors, and the proven clock remembered per card in `/.sdstore.cfg` keyed by the CID. Nothing goes to internal flash.
- **Evidence:** 16 GB SDHC (class 10 / U1): 19/19 at 20-25 MHz. 2 GB card: reliable only to 4 MHz at best ([hardware-log](../hardware-log.md)). Self-tuning passed 2 of 2 full runs with no step-downs. Investigation: [inv-sd-clock-per-card](03-investigations.md#inv-sd-card-clock).
- **Status:** worked-around. **Applies to:** any SD over SPI.

<a id="q-sd-03"></a>
### Q-SD-03 SD shares the LCD init bus; default SPI pins are LCD pins
- **Symptom:** (Avoided.) Garbled LCD, or SD that never mounts.
- **Root cause:** SD SCK/MOSI (GPIO48/47) are the ST7701's 3-wire init bus. `SD.begin()` with no SPI object calls `spi.begin()` on the default FSPI pins, 10-13, which are LCD red bits. A floating MISO reads random data when no card drives it.
- **Fix:** Initialise the display first; its CS (GPIO39) then stays high and ignores SD traffic. `SdStore` owns its own `SPIClass sdSpi(FSPI)` bound to the SD pins and enables the internal pull-up on MISO. Call `sdcard::begin()` after `initDisplay()`.
- **Evidence:** [sd_store.h](../../common/lib/SdStore/src/sd_store.h) header; SD works on unit 1 beside a running panel.
- **Status:** worked-around. **Applies to:** this board; any board where the SD shares pins with a display init bus.

<a id="q-sd-04"></a>
### Q-SD-04 No card-detect pin
- **Symptom:** Firmware cannot tell if a card is inserted. A failed mount with no card takes seconds.
- **Root cause:** The slot has no CD switch wired.
- **Fix:** `sdcard::poll()` every 1-2 s from one task reads sector 0 to confirm presence, unmounts when it fails and remounts when the card returns. A cold `begin()` steps down the clock ladder; `poll()` retries at one clock only. `SD.end()` can take about 0.5 s when the card is gone.
- **Evidence:** Hot-swap works on unit 1: pull and re-insert recovers without a reset; counters show the card lost once and remounted (2026-10-11).
- **Status:** worked-around. **Applies to:** this board.

<a id="q-sd-05"></a>
### Q-SD-05 Harmless "no token received" warning on every mount
- **Symptom:** `[W][sd_diskio.cpp:175] sdCommand(): no token received` in the log at each mount.
- **Root cause:** The driver retries its first command after card power-up.
- **Fix:** None needed; ignore a single occurrence per mount.
- **Evidence:** Seen in every 06 run on both cards.
- **Status:** documented. **Applies to:** Arduino-ESP32 `SD`.

<a id="q-sd-06"></a>
### Q-SD-06 Filesystem constraints and power-cut-safe writes
- **Symptom:** (Design constraints.) State must survive power cuts; FatFs cannot replace a file in place.
- **Root cause and notes:**
  - Use FAT32. exFAT support in the Arduino core was judged not dependable, and Windows' own formatter will not make FAT32 above 32 GB (both from session advice; unverified).
  - FatFs cannot rename over an existing file.
- **Fix:** Every `SdStore` helper opens, writes and closes per call. `writeAtomic()` writes a `.tmp` with a header (`SDA1`, sequence, length, CRC32), reads it back, then removes the old file and renames; `readAtomic()` picks the newest valid copy. A cut between remove and rename leaves only the valid `.tmp`, which is promoted. Payload limit 4096 bytes. The library never formats.
- **Evidence:** 06 tests cover a torn copy, an orphaned copy, newest-wins and a wrong length (19/19). Today's energy baseline restores from SD after a reset (2026-10-11).
- **Status:** worked-around. **Applies to:** any FatFs on SD.

<a id="q-sd-07"></a>
### Q-SD-07 Inserting a card can drop the PC's USB-serial link
- **Symptom:** The serial capture on the PC dropped when a card was inserted or pulled during the hot-swap tests. The ESP32 did not reset.
- **Root cause:** Not found (a brief supply dip on the shared 3.3 V or USB is a guess; unverified).
- **Fix:** Re-open the port. `sd_serial.py` and the capture scripts tolerate a dropped port.
- **Evidence:** [hardware-log](../hardware-log.md) hot-swap row; soak interrupted 2026-10-11.
- **Status:** open. **Applies to:** this board with a CH340 and a PC.

---

## MEM: memory

<a id="q-mem-01"></a>
### Q-MEM-01 mbedTLS allocates only from internal RAM
- **Symptom:** `SSL - Memory allocation failed` (-32512) once LVGL had used internal RAM. Internal free drops 40-50 KB while a TLS session is open.
- **Root cause:** The prebuilt core 3.2.1 `qio_opi` libraries are built with `MBEDTLS_MEM_ALLOC_MODE = INTERNAL`, two 16 KB record buffers per session (`ASYMMETRIC_CONTENT_LEN` is off) and the peer certificate kept. The IDF default session cost is about 42 KB.
- **Fix:** Keep internal RAM free for TLS (no `LV_ATTRIBUTE_FAST_MEM`, 48-line draw buffer in project 05, LVGL heap in PSRAM) and redirect large blocks to PSRAM at runtime ([Q-MEM-02](#q-mem-02)). A custom sdkconfig would fix it properly ([Q-TOOL-04](#q-tool-04)).
- **Evidence:** The low point during the Open-Meteo handshake was 92 KB with SD busy; one session used about 69 KB at peak (boot trace, 2026-10-11). Sdkconfig values read from the installed package ([docs/12](../12-esp32-performance-and-framework.md) section 1). Investigation: [inv-internal-ram-budget](03-investigations.md#inv-internal-ram-budget).
- **Status:** worked-around. **Applies to:** Arduino-ESP32 core 3.2.1 prebuilt libs.

<a id="q-mem-02"></a>
### Q-MEM-02 Moving mbedTLS to PSRAM: the threshold matters
- **Symptom:** Lowering the internal-RAM peak caused display crackle when every mbedTLS block went to PSRAM ([Q-GFX-02](#q-gfx-02)).
- **Root cause:** The handshake allocates many small big-number and certificate blocks, so a low threshold sends constant arithmetic through PSRAM and competes with the panel's DMA.
- **Fix:** `mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree)` before any TLS in `projects/05-powerwall-display/src/powerwall.cpp`: blocks of 4 KB and up (mainly the two 16 KB record buffers) go to PSRAM, the rest stay internal, each falling back to the other. The symbol is exported by the prebuilt `libmbedcrypto.a`.
- **Evidence:** Internal low-water mark: 92 KB before; 137-155 KB with a 1 KB threshold (commit `cca53c5`); about 105-118 KB in the 10-15 minute soaks with the 4 KB threshold (commit `300e7b5`, `soak-*.log`). Longest recorded run is 891 s. The ">= 24 h" soak that docs/12 asked for is not recorded. One external report of hangs with hardware AES on PSRAM buffers ([docs/12](../12-esp32-performance-and-framework.md)); none seen here.
- **Status:** worked-around. Investigation: [inv-mbedtls-psram-hook](03-investigations.md#inv-tls-oom). **Applies to:** Arduino-ESP32 3.2.1 on ESP32-S3 with PSRAM.

<a id="q-mem-03"></a>
### Q-MEM-03 Allocations under 4 KB always go to internal RAM
- **Symptom:** Internal RAM drains as you add screens, Arduino `String` bodies, JSON documents or FATFS objects.
- **Root cause:** The core calls `heap_caps_malloc_extmem_enable(4096)` (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`). Anything smaller than 4 KB is taken from internal RAM first.
- **Fix:** Use explicit PSRAM: `HeapBuf`/`psramAlloc` in `projects/05-powerwall-display/src/mem_util.h` for bodies and images; `JsonDocument doc(psramJson())` (ArduinoJson allocator) for JSON; LVGL's own PSRAM pool ([Q-LVGL-01](#q-lvgl-01)). Lowering the threshold globally was proposed in docs/12 and not done.
- **Evidence:** LVGL pool change took free internal RAM from 38 KB to 140 KB with four screens (2026-10-10). Boot trace: ten screens cost 0 bytes of internal RAM (2026-10-11).
- **Status:** worked-around. **Applies to:** any Arduino-ESP32 3.x with PSRAM.

<a id="q-mem-04"></a>
### Q-MEM-04 Total free hides the largest block and the low-water mark
- **Symptom:** TLS fails although "free heap" looks fine (PSRAM is included in some totals). Free internal RAM reads about 121 KB mid-poll but about 160 KB at idle.
- **Root cause:** TLS needs one big internal block. Fragmentation shrinks the largest block. An open gateway TLS session holds about 40 KB, so 121 KB mid-poll is normal.
- **Fix:** Log `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` and `heap_caps_get_minimum_free_size(...)` (the `mem:` heartbeat line and the serial `m` command), a boot trace per step (`[mem]`), and the low-water mark per TLS job (`[tls]`). Guideline: keep 100 KB or more free.
- **Evidence:** Scare of 161 to 121 KB (2026-10-11) turned out to be the open gateway session; new screens cost 0 bytes; the SD task costs about 8 KB, about 18 KB at the low point. Soak logs: largest block 60-96 KB, min ever 105-118 KB.
- **Status:** documented. **Applies to:** any ESP32 with TLS.

<a id="q-mem-05"></a>
### Q-MEM-05 Speed options spend internal RAM that TLS needs
- **Symptom:** Free internal RAM at boot falls from 267 KB to 87 KB when the LVGL speed defaults are applied.
- **Root cause:** `LV_ATTRIBUTE_FAST_MEM` puts LVGL functions in IRAM (about 55 KB) and a 120-line draw buffer takes 115 KB.
- **Fix:** Libraries and `03-lvgl-demos` use the fast defaults. Project 05 does not: no `LV_ATTRIBUTE_FAST_MEM`, `-DB4848_LVGL_BUF_LINES=48`, in `projects/05-powerwall-display/platformio.ini` and `include/lv_conf.h`.
- **Evidence:** Boot heap by variant: 267, 212, 143, 87 KB ([hardware-log](../hardware-log.md)).
- **Status:** documented. **Applies to:** LVGL projects that also use Wi-Fi/TLS.

<a id="q-mem-06"></a>
### Q-MEM-06 `loopTask` 8 KB stack overflows under LVGL
- **Symptom:** `Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception)` with a backtrace through LVGL's draw code into the image decoder ([Q-LVGL-02](#q-lvgl-02)).
- **Root cause:** LVGL renders in `loopTask`, whose default stack is 8 KB. The heartbeat showed under 1 KB free. A stack watchpoint is how such an overflow shows up (inferred).
- **Fix:** `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` in `projects/05-powerwall-display/src/main.cpp`. 9.6 KB stayed spare. The heartbeat prints "stack free ... lvgl".
- **Evidence:** Crash during a 2-minute capture, 2026-10-11, decoded with `addr2line` ([Q-TOOL-12](#q-tool-12)); 4-minute run clean after the fix (commit `cca53c5`). Investigation: [inv-loop-task-stack](03-investigations.md#inv-loop-stack-overflow).
- **Status:** fixed. **Applies to:** any Arduino-ESP32 sketch that runs LVGL in `loop()`.

---

## LVGL

<a id="q-lvgl-01"></a>
### Q-LVGL-01 `LV_STDLIB_CLIB` puts every widget in internal RAM
- **Symptom:** Internal RAM drains as screens are added; TLS then fails.
- **Root cause:** With the C allocator, every LVGL allocation under 4 KB (the `ALWAYSINTERNAL` threshold) goes to internal RAM, and nearly every widget is smaller than that.
- **Fix:** LVGL's built-in heap with `LV_MEM_SIZE (1024*1024U)` and `#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` in `projects/05-powerwall-display/include/lv_conf.h`.
- **Evidence:** Free internal RAM 38 KB to 140 KB with four screens (2026-10-10); the pool is 13-17 % used with ten screens.
- **Status:** fixed. **Applies to:** LVGL 9 on ESP32 with PSRAM.

<a id="q-lvgl-02"></a>
### Q-LVGL-02 The built-in TJPGD decoder inspects every image
- **Symptom:** Every `lv_image` draw calls into the JPEG decoder's `decoder_info` first (`is_jpg`), adding stack depth and time. The overflow in [Q-MEM-06](#q-mem-06) was in that path.
- **Root cause:** `LV_USE_TJPGD 1` is needed so project 05 can use TJpgDec's raw API for the sun picture, but it also registers an LVGL image decoder in `lv_init()` that sniffs the header of every image source, including raw RGB565 descriptors.
- **Fix:** `lv_tjpgd_deinit();` after `lvglBegin()` in `main.cpp`. `space.cpp` still calls TJpgDec directly.
- **Evidence:** Backtrace `is_jpg` to `lv_draw_label` (2026-10-11); no recurrence in later 4-15 minute runs (commit `cca53c5`).
- **Status:** fixed. **Applies to:** LVGL 9.x with `LV_USE_TJPGD`.

<a id="q-lvgl-03"></a>
### Q-LVGL-03 Montserrat has ASCII and the degree sign only
- **Symptom:** Missing glyphs for the middle dot, "n" with tilde and similar.
- **Root cause:** LVGL's built-in Montserrat covers ASCII plus the degree sign.
- **Fix:** Write "/" instead of a middle dot; write "El Nino". The ENSO page sends a Latin-1 n-tilde raw, so `enso.cpp` folds it to "n". A custom font could come from the SD card later.
- **Evidence:** Decided 2026-10-10 and 2026-10-11 ([CLAUDE.md](../../CLAUDE.md)).
- **Status:** documented. **Applies to:** LVGL built-in fonts.

<a id="q-lvgl-04"></a>
### Q-LVGL-04 `lv_obj_get_height()` before the first layout is not the real height
- **Symptom:** The aurora and sun bar strips were empty on first show.
- **Root cause:** `BarChart::SetNewest()` read the chart's height with `lv_obj_get_height()` before LVGL's first layout pass, when object coordinates are not valid.
- **Fix:** `Chart` stores the height it was created with (`ui_chart.cpp`), and no code reads size or position from an object before layout.
- **Evidence:** Found during the refactor integration, in the same commit that added the serial screenshot command (`496462e`, 2026-10-11).
- **Status:** fixed. **Applies to:** LVGL 9.

<a id="q-lvgl-05"></a>
### Q-LVGL-05 Objects are clickable by default and swallow taps and swipes
- **Symptom:** Tapping an icon (the sun's disc, a battery outline) did nothing. Dragging the settings slider switched pages.
- **Root cause:** `lv_obj_create` objects default to `CLICKABLE` in LVGL 9.5 and `lv_obj_remove_style_all` does not clear flags, so they absorb the click. A horizontal drag on a slider bubbles up as a gesture to the screen's handler.
- **Fix:** Helpers clear `CLICKABLE | SCROLLABLE` on decorative parts (`icons.cpp`, `ui::box`). The slider removes `GESTURE_BUBBLE` and `EVENT_BUBBLE` (`screen_settings.cpp`).
- **Evidence:** Cold-review finding, fixed in commit `ca8bc70` (2026-10-10).
- **Status:** fixed. **Applies to:** LVGL 9.

<a id="q-lvgl-06"></a>
### Q-LVGL-06 Hidden screens keep animating
- **Symptom:** Animations on screens that are not shown still cost redraws.
- **Root cause:** LVGL animations run until finished. There is no public enumeration, and pausing the screen object's own animation would freeze the slide transition.
- **Fix:** `ui::pauseScreenAnimations()` in `ui_common.cpp` walks LVGL's animation list, pauses animations whose object belongs to the hidden screen, and skips animations on the screen object itself.
- **Evidence:** Commit `ca8bc70` (2026-10-10).
- **Status:** fixed. **Applies to:** LVGL 9.

<a id="q-lvgl-07"></a>
### Q-LVGL-07 Image cache is keyed by the descriptor
- **Symptom:** (Prevented.) A new sun picture written into the same buffer would not appear.
- **Root cause:** LVGL caches the decoded image by source; rewriting pixels behind the same `lv_image_dsc_t` is not noticed (inferred from the code; the failure was not captured).
- **Fix:** `lv_image_cache_drop(&s_dsc); lv_obj_invalidate(s_img);` when the picture changes (`screen_sun.cpp`).
- **Evidence:** Code only (unverified as a failure).
- **Status:** documented. **Applies to:** LVGL 9 with in-RAM images that change.

---

## PERF: performance

<a id="q-perf-01"></a>
### Q-PERF-01 `-O2` in `build_flags` has no effect
- **Symptom:** The firmware built with `-O2` is byte-identical to the original.
- **Root cause:** The Arduino core adds `-Os` after `build_flags`, and the last `-O` on the command line wins.
- **Fix:** `build_unflags = -Os` as well, in `common/platformio-base.ini`.
- **Evidence:** Variant v1 (flag only) produced identical firmware; v5 (unflag plus `-O2`) averaged 35 FPS against 30 ([hardware-log](../hardware-log.md), commit `d17673a`, 2026-10-10).
- **Status:** fixed. **Applies to:** PlatformIO + Arduino-ESP32.

<a id="q-perf-02"></a>
### Q-PERF-02 A bigger draw buffer was the biggest win
- **Symptom:** (Tuning result.) Render time was the bottleneck, flush was about 20 ms per full screen.
- **Root cause:** Rendering into a small buffer means more strips and passes; the full-screen flush is a CPU copy of 460 KB into PSRAM.
- **Fix:** Shared defaults: `-O2`, `LV_ATTRIBUTE_FAST_MEM`, 120 lines in internal RAM. Average 30 to 35 FPS, render 35 to 22 ms; heavy scenes up 50-110 %. Biggest single step was the buffer (render -31 %), then `-O2`, then IRAM. Cost: internal RAM ([Q-MEM-05](#q-mem-05)).
- **Evidence:** 03 benchmark, seven variants, unit 1 ([hardware-log](../hardware-log.md)). Untried: async DMA copy, esp_lvgl_port SIMD blends. Investigation: [inv-lvgl-performance](03-investigations.md#inv-perf-defaults).
- **Status:** documented. **Applies to:** LVGL partial rendering on this board.

<a id="q-perf-03"></a>
### Q-PERF-03 Double framebuffers and two draw threads are slower
- **Symptom:** Variants that should be faster measured slower.
- **Root cause:** Rendering straight into PSRAM is much slower than into internal RAM, and each frame waits up to one refresh (about 24 ms at 42 Hz). Two LVGL draw threads cost more in overhead than the second core returns, and CPU reads 100 %.
- **Fix:** Don't use them by default. Double buffering stays as an opt-in path (`lvglBeginDoubleBuffered()`, `04-lvgl-fb2-bench`).
- **Evidence:** DIRECT 20 FPS, FULL 14 FPS, two threads 31 FPS, against 35 FPS for the v5 defaults. The swap itself works: 0 timeouts, presents synchronised.
- **Status:** documented. **Applies to:** this board with LVGL 9.5.

<a id="q-perf-04"></a>
### Q-PERF-04 Idle animation redraws are the standing load
- **Symptom:** The main screen animation felt "jerky" (user, 2026-10-10). Standing redraw load of 200,000-300,000 px/s and 190-250 flushes/s on the power-flow screen.
- **Root cause (partly inferred):** Flow dots moved on a 33 ms timer, so slow dots stepped unevenly. Labels were re-set every 500 ms with unchanged text, and LVGL redraws on every set. Entry fades re-render whole cards while the slide already redraws the full screen. A scrolling ticker redraws continuously.
- **Fix:** Per-frame dot updates; set text only when it changes; start entry animations after the 300 ms slide (`UI_FADE_AFTER_SLIDE`, default 1); pause hidden-screen animations ([Q-LVGL-06](#q-lvgl-06)); slow the ticker. The heartbeat prints `redraw: <n> px/s, <n> flushes/s` as the number to compare.
- **Evidence:** About 28 FPS, "much smoother" after the first fix (user); later soaks show 40-59 FPS. No before/after number is recorded for the fade timing or the ticker (unverified).
- **Status:** worked-around. **Applies to:** LVGL on a PSRAM RGB panel.

---

## NET: network stack

<a id="q-net-01"></a>
### Q-NET-01 HTTPClient's chunked reader stalls on a kept-alive TLS socket
- **Symptom:** `task_wdt: IDLE0 (CPU 0)` reset about 30 seconds after the gateway connection was reused. Backtrace: `HTTPClient::getString` to `writeToStreamDataBlock` to `readBytes`.
- **Root cause:** On a kept-alive TLS connection the chunked reader sat in `Stream::readBytes`, which busy-waits without yielding until the read timeout. It starved the idle task and tripped the 5 s watchdog. The first theory (a response with no length header) was refuted: the meters data is chunked and every other response has a Content-Length.
- **Fix:** Fetch chunked bodies as HTTP/1.0 (`useHTTP10(true)`: a plain body, then the connection closes); network tasks at idle priority ([Q-FW-02](#q-fw-02)); short read timeout (3 s). Our own de-chunker and read loop with a deadline and yields (`net_body.h`, `net_http.cpp`) now replace HTTPClient's body readers for all fetches.
- **Evidence:** Failed within 30 s before; a 4-minute run clean after (2026-10-10, commits `ca8bc70`, `d3a860f`). 217 host tests cover the de-chunker at every split (commit `ba7b097`). Investigation: [inv-gateway-chunked-stall-wdt](03-investigations.md#inv-chunked-stall).
- **Status:** worked-around. **Applies to:** Arduino-ESP32 3.2.1 `HTTPClient` with TLS keep-alive.

<a id="q-net-02"></a>
### Q-NET-02 Chunked JSON parses as a number
- **Symptom:** A JSON API parses to a number, or every field is 0.
- **Root cause:** `deserializeJson(doc, http.getStream())` reads the hex chunk-size line as the start of the body.
- **Fix:** Read the whole body de-chunked (`getString()`, `useHTTP10(true)`, or `net::readBody`) and parse from memory.
- **Evidence:** First found building project 05 (2026-10-10); [docs/07](../07-gotchas.md).
- **Status:** worked-around. **Applies to:** any Arduino-ESP32 HTTPClient + stream parser.

<a id="q-net-03"></a>
### Q-NET-03 Default TLS handshake timeout is 120 s
- **Symptom:** The dashboard froze for 2 minutes when a server stopped answering; Wi-Fi up but internet down flipped it to "--" every minute.
- **Root cause:** `WiFiClientSecure`'s handshake timeout defaults to 120 s, and weather shared the polling task.
- **Fix:** `setHandshakeTimeout(8)`; connect and read timeouts 3 s; per-job time budgets ([Q-NET-05](#q-net-05)).
- **Evidence:** Cold review finding, fixed in commit `ca8bc70`.
- **Status:** fixed. **Applies to:** Arduino-ESP32 3.x.

<a id="q-net-04"></a>
### Q-NET-04 The gateway refuses some handshakes at about 30 per minute
- **Symptom:** Intermittent `ssl_starttls_handshake(): (-30592) SSL - A fatal alert message was received from our peer` on the gateway, and once `aggregates HTTP 400`. The fetch jobs to public servers had 0 failures.
- **Root cause (not proven):** The gateway answers a TLS handshake roughly every 2 s (one per poll in HTTP/1.0 mode, about 28 per minute) and sometimes sends a fatal alert. Suggestive, not conclusive: with keep-alive the same run had 22 connects and 1 refusal.
- **Fix:** Optional keep-alive for `/aggregates` behind `-DPW_GW_KEEPALIVE=1`, with our de-chunker. Default stays 0 until soak-tested. The UI tolerates a failed poll (values go stale after 20 s).
- **Evidence:** Soak logs, 2026-10-11. Four HTTP/1.0 runs logged 3, 1, 12 and 19 refusals (the last: 275 TLS connects in 591 s, about 28 per minute). The keep-alive run logged 1 refusal with 22 connects, 857 requests and 0 failures. The error code is mbedTLS `FATAL_ALERT_MESSAGE`. Why the count varies run to run is unknown.
- **Status:** open. Investigation: [inv-gateway-handshake-refusals](03-investigations.md#inv-tls-gateway-refusals). **Applies to:** Powerwall 3 gateway, firmware 26.26.4.

<a id="q-net-05"></a>
### Q-NET-05 One TLS session at a time; job time budget vs stale data
- **Symptom:** Weather or sun-image jobs made the live values go to "--" and the energy totals pause.
- **Root cause:** One network task serves the gateway poll and the background jobs. A job (25 s sun image, five ENSO requests) could exceed the 20 s stale limit.
- **Fix:** Single network task on core 0 at idle priority: poll the gateway, then run at most one due job per pass, with the gateway connection closed first. Every job runs under `PW_JOB_BUDGET_MS` (12 s), and a `static_assert` ties it to `PW_STALE_MS` (20 s). The job table logs `[tls] <name>: job free before..., min ever...`.
- **Evidence:** Plan finding B1; commits `cca53c5`, `ba7b097`. Merging the weather task into the gateway task also saved a 10 KB stack.
- **Status:** fixed. **Applies to:** any RAM-tight ESP32 doing TLS.

<a id="q-net-06"></a>
### Q-NET-06 TLS needs a valid clock first
- **Symptom:** Certificate validity checks fail before NTP has set the time.
- **Root cause:** mbedTLS checks the certificate dates against the system clock. Two files also used different "clock valid" tests (`> 1700000000` and `tm_year < 124`).
- **Fix:** Background jobs run only when `tu::clockValid()` is true (one threshold, 2024-01-01, in `time_util.h`). NTP is started with `configTzTime`.
- **Evidence:** `powerwall.cpp` comment; commit `ba7b097`.
- **Status:** fixed. **Applies to:** any ESP32 with certificate checks.

<a id="q-net-07"></a>
### Q-NET-07 Which root CA to pin for which host
- **Symptom:** TLS fails with a pinned root, or a certificate block in source is corrupt.
- **Root cause and notes:** Pinned roots are in `projects/05-powerwall-display/src/ca_certs.h` (all fetched 2026-10-10/11; chains checked with openssl).

| Host | Pin | Notes |
|---|---|---|
| api.open-meteo.com, soho.nascom.nasa.gov | ISRG Root X1 and Root YR (both pinned in `LETS_ENCRYPT_CA`) | Chain ends at "Root YR", cross-signed by X1. Both fetches succeed on unit 1. docs/11 lists sdo.gsfc.nasa.gov, api.helioviewer.org and psl.noaa.gov on the same chain; not fetched on the device |
| services.swpc.noaa.gov | Amazon Root CA 1 | Plain HTTP redirects (301) to HTTPS |
| www.cpc.ncep.noaa.gov | Go Daddy Root CA G2 | Server sends leaf plus intermediate only |
| api.met.no | HARICA TLS RSA Root CA 2021 | The PEM URL returned an HTML page; the DER `.cer` file was converted instead |
| www.bom.gov.au | DigiCert Global Root G2 | 403 to the default curl User-Agent; not used |

- **Fix:** One `inline constexpr` copy of each in `ca_certs.h` (they used to be compiled into two translation units, about 5 KB wasted). Generate the `"...\n"` lines with a script written by the Write tool, not a heredoc ([Q-TOOL-02](#q-tool-02)).
- **Evidence:** [docs/11](../11-space-weather-and-climate-data.md) section 1; the Open-Meteo request was first sent without verification and then pinned (commit `ca8bc70`).
- **Status:** documented. **Applies to:** the hosts listed.

<a id="q-net-08"></a>
### Q-NET-08 A truncated URL gives HTTP 400
- **Symptom:** HTTP 400 from a URL built with `snprintf`.
- **Root cause:** The buffer was too small, so the query string was cut off silently.
- **Fix:** Size the buffer generously and check the `snprintf` return value.
- **Evidence:** Weather request, 2026-10-10 ([docs/07](../07-gotchas.md)).
- **Status:** fixed. **Applies to:** any code building URLs in fixed buffers.

<a id="q-net-09"></a>
### Q-NET-09 2.4 GHz only
- **Symptom:** `WiFiMulti: no matching wifi found` although the SSID exists.
- **Root cause:** The ESP32-S3 has no 5 GHz radio. A 5 GHz-only network is invisible.
- **Fix:** Join a 2.4 GHz SSID. `WiFiMulti` joins the strongest listed network. Project 05 prints a scan of what it can see after a failed join.
- **Evidence:** Unit 1 joined a 2.4 GHz network at about -60 dBm ([hardware-log](../hardware-log.md)). Two of the three home SSIDs were not visible to it at all (session, 2026-10-10).
- **Status:** documented. **Applies to:** any ESP32.

<a id="q-net-10"></a>
### Q-NET-10 Wi-Fi settings are saved to flash by default
- **Symptom:** Runtime flash (NVS) writes you did not ask for; a known trigger of RGB drift.
- **Root cause:** Arduino-ESP32 `WiFi` has `_persistent = true`, which sets `WIFI_STORAGE_FLASH`.
- **Fix:** `WiFi.persistent(false)` before `WiFi.mode()` (`powerwall.cpp`). Credentials come from `secrets.h`.
- **Evidence:** Found in the framework source by the performance research and fixed on 2026-10-11 (commit `cca53c5`). That IDF writes only when the config changes is unverified.
- **Status:** fixed. **Applies to:** any Arduino-ESP32 3.x.

---

## PW: Powerwall LAN API

All entries are for a Powerwall 3 on firmware 26.26.4 with the basic customer login, read-only, on the home LAN. See [projects/05 README](../../projects/05-powerwall-display/README.md).

<a id="q-pw-01"></a>
### Q-PW-01 Only three endpoints answer on basic access
- **Symptom:** Most documented Powerwall endpoints return 404.
- **Root cause:** Tesla limits the customer login on this firmware. Probing read-only GETs with a valid token gave answers only from `/api/meters/aggregates`, `/api/system_status/soe` and `/api/system_status/grid_status`.
- **Fix:** Build on those three. `aggregates` carries power, voltage, frequency, reactive and apparent power and lifetime Wh counters; `grid_status` carries `grid_services_active`. Backup reserve, operation mode, per-string solar, alerts and temperatures are not available; they need TEDAPI (gateway Wi-Fi plus the full gateway password; reportedly the only route since firmware 25.10, unverified).
- **Evidence:** [hardware-log](../hardware-log.md) (2026-10-10).
- **Status:** documented. **Applies to:** Powerwall 3, firmware 26.26.4.

<a id="q-pw-02"></a>
### Q-PW-02 Login, token expiry and lockout risk
- **Symptom:** 403 with no token, 401 with an expired or bad one. A wrong password retried every 2 s would be about 43,000 attempts a day.
- **Root cause:** `POST /api/login/Basic` with user `customer` and the last 5 characters of the gateway password returns a token, sent back as `Authorization: Bearer`. The reviewer's concern that gateways can lock out repeated bad logins is unverified.
- **Fix:** Log in again on 401/403. Failed logins back off 30 s, 60 s, 120 s up to 5 min and the UI says "login rejected - check password". Keep the password only in the git-ignored `secrets.h`.
- **Evidence:** Cold review finding 5, fixed in commit `ca8bc70`.
- **Status:** fixed. **Applies to:** same.

<a id="q-pw-03"></a>
### Q-PW-03 `/aggregates` is chunked; a missing meter must not become 0
- **Symptom:** (Prevented.) A partial or chunk-split body producing zero power or a wrong "today" total.
- **Root cause:** `aggregates` is chunked; the other endpoints have Content-Length ([Q-NET-01](#q-net-01), [Q-NET-02](#q-net-02)). Zero lifetime counters are how a gateway counter reset looks.
- **Fix:** Parse everything into locals first and touch shared state under the lock only once the aggregates are complete; required keys are checked, optional gauges show "--" when 0 (`powerwall.cpp`).
- **Evidence:** Commit `ca8bc70`. The 10-minute soaks show 0 request failures at the HTTP level and one `aggregates HTTP 400` in the worst run ([Q-NET-04](#q-net-04)); the incomplete-data path was not exercised in a captured log.
- **Status:** fixed. **Applies to:** same.

<a id="q-pw-04"></a>
### Q-PW-04 Raw state of charge includes a hidden 5 % reserve
- **Symptom:** The raw percentage reads up to 5 % higher near empty than the Tesla app.
- **Root cause:** The local value includes a reserve the app hides.
- **Fix:** `(raw - 5) / 0.95` (the pypowerwall scaling). `-DPW_SOC_APP_SCALE=0` shows the raw value.
- **Evidence:** Reviewer finding 10 (2026-10-10). The scaled value was never compared with the app by us (unverified).
- **Status:** worked-around, check against the app. **Applies to:** Powerwall 3 local API.

<a id="q-pw-05"></a>
### Q-PW-05 Lifetime counters: resets, gaps and persistence
- **Symptom:** (Prevented.) "Today" totals include yesterday's last hours after an outage, or one hourly bar absorbs a long gap.
- **Root cause:** Today = lifetime counter minus a midnight snapshot. Stale data, counter resets and power cuts all corrupt that.
- **Fix:** `energy.cpp`: ignore stale readings; clean midnight rollover only with a reading at most 10 min before midnight; re-baseline on a counter reset (more than 1 kWh drop, or zero); do not put gaps over 30 min in one bin. The baseline lives in RTC memory ([Q-FW-03](#q-fw-03)) with a CRC-checked backup on SD, restored after a power cut. No internal-flash writes.
- **Evidence:** Commit `ca8bc70`; "Energy: baseline restored from SD card" in the 2026-10-11 boot logs.
- **Status:** fixed. **Applies to:** same.

<a id="q-pw-06"></a>
### Q-PW-06 The gateway may be on another network segment
- **Symptom:** The gateway does not appear in the PC's neighbour table or a ping sweep.
- **Root cause:** It can sit on an IoT VLAN or a separate Wi-Fi network from the PC.
- **Fix:** Find it from the router's DHCP leases, or sweep the neighbour table and probe HTTPS on responders. The ESP32's `WiFiMulti` list needs a network that can reach it. Give the gateway a DHCP reservation.
- **Evidence:** Session 2026-10-10 (addresses omitted).
- **Status:** documented. **Applies to:** home networks with VLANs.

<a id="q-pw-07"></a>
### Q-PW-07 The grid-services (VPP) flag is untested
- **Symptom:** The "VPP event active" state (pulsing amber) has never been seen.
- **Root cause:** `grid_services_active` only goes true during a real grid-services event.
- **Fix:** None. A test toggle is a deferred follow-up. A guessed inference from other fields was started and reverted at the user's request.
- **Evidence:** [projects/README](../../projects/README.md) follow-ups.
- **Status:** open. **Applies to:** same.

---

## DATA: public data sources

Checked with `curl` on 2026-10-11 around 12:00 UTC; full tables in [docs/11](../11-space-weather-and-climate-data.md).

<a id="q-data-01"></a>
### Q-DATA-01 Many tutorial SWPC endpoints are gone
- **Symptom:** 404 from endpoints in older tutorials.
- **Root cause:** SWPC removed or renamed them: `products/solar-wind/plasma-2-hour.json`, `mag-2-hour.json`, `xrays-1-minute.json`, `products/solar-flux.json`, `cme_analysis_latest.json`, `text/sunspot-report.txt`. The real-time solar wind feed now comes from IMAP (`"source":"IMAP"`).
- **Fix:** Use the verified list in docs/11, for example `propagated-solar-wind-1-hour.json`, `xray-flares-latest.json`, `noaa-scales.json`.
- **Evidence:** 404s on 2026-10-11.
- **Status:** documented. **Applies to:** NOAA SWPC; re-check before relying on any URL.

<a id="q-data-02"></a>
### Q-DATA-02 SWPC JSON shapes vary file to file
- **Symptom:** Parsers read zeros or fail on valid-looking data.
- **Root cause:** `noaa-scales.json` has numbers as strings and nulls in forecast rows; `predicted-solar-cycle.json` keys contain hyphens and dots (`"time-tag"`, `"predicted_f10.7"`); `propagated-solar-wind-1-hour.json` is an array of arrays with a header row; the summary files round `bt`/`bz` to integers; the `"active"` flag in rtsw feeds is false and should be ignored; SESC daily indices use -1/-999 for unavailable.
- **Fix:** Look columns up by header name (`columnOf`), accept string or int scale values (`scaleOf`), treat null/string as no value (`numAt`), use ArduinoJson filters.
- **Evidence:** `space.cpp`. Live on unit 1, 2026-10-11: flare B7.8 (max C1.1), R0 S0 G0, wind 572-581 km/s, SSN 99. (The host tests in `hosttest/` cover the de-chunker, date and ENSO parsers, not the SWPC parsers.)
- **Status:** fixed. **Applies to:** NOAA SWPC.

<a id="q-data-03"></a>
### Q-DATA-03 "A0.0" flare class during a GOES data gap
- **Symptom:** The current flare class jumps to A0.0.
- **Root cause:** SWPC reports zero flux as `A0.0` while the GOES data has a gap.
- **Fix:** Treat `A0.0` as "no reading" and keep the last class (`space.cpp`).
- **Evidence:** Commit `496462e`.
- **Status:** fixed. **Applies to:** NOAA SWPC flare feed.

<a id="q-data-04"></a>
### Q-DATA-04 History files are too big for the chip
- **Symptom:** (Planning.) A live X-ray graph or a long sunspot history does not fit.
- **Root cause:** X-ray flux files are 160 KB (6 h), 653 KB (1 day) and 1.96 MB (3 day), with no smaller variant (the 1-minute/5-minute names are 404). Observed solar-cycle indices are 512 KB; the ONI file is 23 KB and the weekly SST file 148 KB.
- **Fix:** Use latest/7-day products (`xray-flares-latest.json` 454 B, 7-day flares 24 KB), or an HTTP `Range` tail on CPC files. SWPC Range support is untested.
- **Evidence:** Byte counts from the 2026-10-11 survey.
- **Status:** documented. **Applies to:** NOAA SWPC and CPC.

<a id="q-data-05"></a>
### Q-DATA-05 NASA DONKI returned a web page, not JSON
- **Symptom:** Every DONKI URL returned an HTML "CCMC Announcements" page (58,490 B) instead of JSON.
- **Root cause:** Unknown; the service looked migrated or unavailable that day.
- **Fix:** Don't use it. CME news comes from SWPC `discussion.txt`, which is prose.
- **Evidence:** 2026-10-11, including the kauai.ccmc host.
- **Status:** documented, unverified beyond that day. **Applies to:** NASA DONKI.

<a id="q-data-06"></a>
### Q-DATA-06 SDO "latest" images were three weeks stale
- **Symptom:** The "latest" SDO JPEGs had `Last-Modified` of 21 Sep 2026 for every file.
- **Root cause:** The upstream feed had stopped updating.
- **Fix:** Use SOHO (`hmi_igr/512/latest.jpg`, updated that day) and check `Last-Modified` before trusting any "latest" image.
- **Evidence:** 2026-10-11 survey.
- **Status:** documented. **Applies to:** NASA SDO.

<a id="q-data-07"></a>
### Q-DATA-07 Only baseline JPEG decodes; the decoder is on your side
- **Symptom:** (Constraint.) A progressive JPEG would not decode. A server-side change could break the sun picture.
- **Root cause:** TJpgDec handles baseline (SOF0) only. All SOHO files checked were baseline.
- **Fix:** `space.cpp` decodes MCU rows with TJpgDec, area-averages to a small image, tints it and paints the corners with a background marker so no `clip_corner` layer is needed. Re-check the JPEG type after any URL change.
- **Evidence:** A 72,908 B image decoded in 850 ms on unit 1 (2026-10-11). Decoding uses an 8 KB workspace and an internal-RAM band buffer ([Q-LVGL-02](#q-lvgl-02)).
- **Status:** documented. **Applies to:** TJpgDec on ESP32.

<a id="q-data-08"></a>
### Q-DATA-08 CPC ENSO: Range works, caching headers do not, the page is HTML
- **Symptom:** Reading only the start of `ensodisc.shtml` misses the odds and the next-update date.
- **Root cause and notes:**
  - CPC text files honour `Range` (tested 206); the server sends `Cache-Control: no-store`, so no conditional GET.
  - The odds sentence and next-discussion date sit beyond byte 8192 of the 13.5 KB page, although docs/11 first estimated the first 6 KB.
  - There is no JSON for the alert status. The page is scraped.
  - The page sends a raw Latin-1 n-tilde ([Q-LVGL-03](#q-lvgl-03)).
  - Several different Nino3.4 numbers appear (weekly, monthly, ONI, RONI); label each with its index and period.
  - A server that ignores Range sends the whole 148 KB file.
- **Fix:** `enso.cpp` streams the whole page through a tag stripper (the 13 KB is never held), and keeps only the last N bytes of the data files, aborting past 256 KB. Parsers are host-tested on real payloads.
- **Evidence:** Live run on 2026-10-11 gave El Nino Advisory, weekly +3.2, odds 54/83/70 %, next update 12 Nov.
- **Status:** worked-around; HTML scraping stays fragile. **Applies to:** NOAA CPC.

<a id="q-data-09"></a>
### Q-DATA-09 AuroraWatch UK and MET Norway ask for a User-Agent and credit
- **Symptom:** (Etiquette and terms.)
- **Root cause and notes:**
  - Both ask for an identifying User-Agent (`net::USER_AGENT`).
  - AuroraWatch UK (free for non-commercial use) wants credit and asks that thresholds and colours are not altered or called "official". Its API is plain HTTP, which also avoids a TLS session ([Q-NET-05](#q-net-05)).
  - MET Norway wants at most 4 decimals of position and credit.
  - The NOAA global Kp and the AuroraWatch UK level differ: Kp 5.67 (a G2 storm) with the UK level green, 2026-10-11. Show each as its own source.
  - Credits live on the About page only (user decision).
- **Fix:** Fetch every 30 min (moon times once per local day). Terms were summarised from a research agent, not re-read for this catalogue.
- **Evidence:** `sky.cpp`; live data on 2026-10-11.
- **Status:** documented. **Applies to:** those services.

<a id="q-data-10"></a>
### Q-DATA-10 Open-Meteo rain chance depends on the model
- **Symptom:** The default request gave a 0 % chance of rain where the Met Office model gave 33 %.
- **Root cause:** The default blends ensembles; `models=ukmo_seamless` uses UK-specific data. Temperatures, high/low and conditions were identical.
- **Fix:** None; the default is kept. Switching is one request parameter.
- **Evidence:** Side-by-side requests, 2026-10-10.
- **Status:** documented. **Applies to:** Open-Meteo.

---

## FW: firmware patterns

<a id="q-fw-01"></a>
### Q-FW-01 Static-init order: a mutex used before `start()` runs
- **Symptom:** Boot crash: `assert failed` in `xQueueSemaphoreTake`, backtrace `powerwallData()` from `sampleMinute()` from `sdTask`.
- **Root cause:** The SD task started in `setup()` before `powerwallStart()` had created the data mutex, so it took a null semaphore.
- **Fix:** Create the mutex at static initialisation (`Shared<T>`/`Mutex` in `shared_state.h`; `history.cpp` uses a static `Mutex` for the same reason). Getters work with defaults before `start()`.
- **Evidence:** Second of the two crashes decoded with `addr2line` during the 2026-10-11 bring-up of the sun and ENSO screens, the other being [Q-MEM-06](#q-mem-06) (commit `cca53c5`). Investigation: [inv-boot-race-sd-task](03-investigations.md#inv-boot-race-lock).
- **Status:** fixed. **Applies to:** any FreeRTOS code where one task reads another module's state early.

<a id="q-fw-02"></a>
### Q-FW-02 Network tasks at idle priority; the task watchdog watches IDLE0
- **Symptom:** `task_wdt` reset when a library busy-waits.
- **Root cause:** The core's task watchdog (5 s) checks the idle task on core 0. A higher-priority task that never yields starves it.
- **Fix:** Pin the network task to core 0 at `tskIDLE_PRIORITY`. It then time-slices with the idle task, so a stall can't fire the watchdog. LVGL runs on core 1 only (`LV_USE_OS LV_OS_NONE`; the network task never calls LVGL).
- **Evidence:** Part of the fix for [Q-NET-01](#q-net-01); `powerwall.cpp`, `lv_conf.h`.
- **Status:** worked-around. **Applies to:** Arduino-ESP32 3.x.

<a id="q-fw-03"></a>
### Q-FW-03 No runtime flash writes: RTC memory semantics
- **Symptom:** (Rule.) Wear on the internal flash from NVS, LittleFS or SPIFFS.
- **Root cause:** User requirement. Wi-Fi persistence is one hidden writer ([Q-NET-10](#q-net-10)). `03-lvgl-demos` still stores its one-shot demo choice in NVS (a demo only).
- **Fix:** Volatile state in RTC memory (`RTC_NOINIT_ATTR`): it survives software resets and crashes but not a power cut, the reset button (`EN` reports as power-on) or a reflash. Durable data goes to the SD card ([Q-SD-06](#q-sd-06)).
- **Evidence:** [README of 05](../../projects/05-powerwall-display/README.md); memory rule dated 2026-10-10.
- **Status:** documented. **Applies to:** any ESP32.

<a id="q-fw-04"></a>
### Q-FW-04 `millis()` back-off logic breaks after about 25 days
- **Symptom:** (Latent.) After roughly 25 days of uptime a login retry could be blocked for up to another 25 days, with no error shown.
- **Root cause:** A comparison such as `millis() < next` stops working once the value passes 2^31, because `next` is computed past the wrap.
- **Fix:** Keep an explicit `s_backingOff` flag and compare with `(int32_t)(millis() - next) < 0`.
- **Evidence:** Found by a lint agent's code read on 2026-10-11; `powerwall.cpp` comment. `millis()` itself wraps after about 49 days.
- **Status:** fixed. **Applies to:** any 32-bit tick comparison.

<a id="q-fw-05"></a>
### Q-FW-05 Padding in checksummed snapshots
- **Symptom:** (Latent.) A restored baseline that later failed its own checksum.
- **Root cause:** Struct assignment need not copy padding bytes; a CRC over the raw struct sees different padding.
- **Fix:** `memcpy` the whole struct on restore, zero it first (`startDay()` memsets), assign members only, and `static_assert` that it is standard-layout and trivially copyable (`energy.cpp`).
- **Evidence:** Found before it reached the device (2026-10-11).
- **Status:** fixed. **Applies to:** any raw-struct CRC.

<a id="q-fw-06"></a>
### Q-FW-06 LEDC: `ledcReadFreq()` is 0 at duty 0, and the auto clock tops out below 80 MHz
- **Symptom:** After the backlight-range change, `br` reported `0 Hz, 0 bits` and the boot log had a cascade of `ledcAttach(): No free timers available` and `Pin 38 is already attached to LEDC (channel 0, resolution 8)`. Host tests passed; only the hardware showed it.
- **Root cause:** Two things. (1) IDF 5.4's `LEDC_AUTO_CLK` refused 100 kHz x 9 bits (51.2 MHz): `requested frequency 100000 and duty resolution 9 can not be achieved ... div_param=0`, although the S3's APB clock is 80 MHz. (2) The fallback to 8 bits then attached fine, but the code checked success with `ledcReadFreq()`, which returns 0 whenever the channel's duty is 0 (`if (!ledcRead(pin)) return 0;` in `esp32-hal-ledc.c`). That looked like a failure, so the next attempt found the pin already attached.
- **Fix:** plan resolutions against 40 MHz (`freq x 2^bits <= 40 MHz`: 8 bits at 100 kHz, 10 bits at 20-25 kHz) and treat a `true` from `ledcAttach()` as success (`Board4848S040.cpp`). `ledcChangeFrequency()` returns the real frequency and is fine to check.
- **Evidence:** Unit 1 boot logs before and after, 2026-10-11; after the fix `br` gives `backlight 100000 Hz, 8 bits, duty 255 of 255, assumed minimum 100` and `bf 25000` gives 10 bits.
- **Status:** fixed. **Applies to:** Arduino-ESP32 3.2.x LEDC on the S3.

---

## TOOL: toolchain and workflow

<a id="q-tool-01"></a>
### Q-TOOL-01 Git Bash/MSYS breaks ESP's `idf_tools.py`
- **Symptom:** `ERROR: MSys/Mingw is not supported`, `idf_tools.py installation failed`, then `xtensa-esp32s3-elf-g++ is not recognized`.
- **Root cause:** The first build ran from Git Bash. ESP-IDF's tool installer refuses MSYS shells.
- **Fix:** Run `pio` and `esptool` from PowerShell (`tools/pio.ps1 <project> [build|upload|monitor|all]`), cmd, or VS Code's PlatformIO buttons. After the toolchain is installed, builds work, but keep the rule.
- **Evidence:** First session, 2026-10-08; every script header repeats it.
- **Status:** fixed. **Applies to:** pioarduino/ESP-IDF on Windows.

<a id="q-tool-02"></a>
### Q-TOOL-02 Scripted edits through the Bash tool turn `\n` in C strings into real newlines
- **Symptom:** A C string like `"#done %c\n"` is split over two lines; the build fails with an unterminated string. Seen at least five times across the sessions, including after trying to double the backslash.
- **Root cause (two separate causes):**
  1. A Python heredoc with an ordinary string literal turns the two characters `\n` into a newline before writing the file. A quoted heredoc (`<<'EOF'`) stops Bash expanding it, not Python.
  2. The usual cure, doubling the backslash to `\\n`, did not work in the Bash tool: it collapses a double backslash to a single one even inside a quoted heredoc and inside single quotes. Re-tested during this write-up: a heredoc line holding two backslashes and an n arrived with one; a line with four arrived with two; `echo 'x:' + two backslashes + 'n'` printed one. The PowerShell tool leaves backslashes untouched. Whether the collapse is the tool wrapper or Git Bash was not isolated.
  `sed` with `\n` in a replacement is the same class of hazard (no failing instance captured; unverified).
- **Fix:** For C/C++ string edits use the Edit tool. For generated text use the Write tool, or PowerShell (`[IO.File]::WriteAllText` with a here-string). Don't route backslash-heavy text through a Bash heredoc. The damage is only caught after the fact, by the lint pass and the build ([Q-TOOL-07](#q-tool-07)). A hook that blocks heredoc edits to `.c/.cpp/.h/.ino` was suggested; it is not set up.
- **Evidence:** 06 `main.cpp` "#done", the 05 heartbeat and weather `printf`, and a certificate block; first noted 2026-10-10, again 2026-10-11 ([CLAUDE.md](../../CLAUDE.md) "Editing tip"). Investigation: [inv-heredoc-newline](03-investigations.md#inv-heredoc-newlines).
- **Status:** worked-around. **Applies to:** the Claude Code Bash tool on Windows (Git Bash); Python string literals anywhere.

<a id="q-tool-03"></a>
### Q-TOOL-03 Platform and PlatformIO Core pinning
- **Symptom:** `IncompatiblePlatform ... depends on PlatformIO Core >=6.2.0`; or Arduino_GFX 1.6 fails on the official platform.
- **Root cause:** The official `espressif32` platform is stuck on Arduino core 2.x, which Arduino_GFX 1.6.x does not support. Newer pioarduino releases need PlatformIO Core 6.2 or later; 6.1.18 was installed.
- **Fix:** `common/platformio-base.ini` pins pioarduino `54.03.21-2` (Arduino 3.2.1, IDF 5.4), works with Core 6.1.x. Arduino_GFX is pinned to 1.6.9 in `library.json`; LVGL to 9.5.0 and ArduinoJson to 7.4.3 per project. To go newer, run `pio upgrade` first.
- **Evidence:** [docs/07](../07-gotchas.md), 2026-10-08.
- **Status:** worked-around. **Applies to:** pioarduino.

<a id="q-tool-04"></a>
### Q-TOOL-04 `custom_sdkconfig` rewrites the shared framework package
- **Symptom:** (Predicted from the source, not run.) Building one project with `custom_sdkconfig` forces a full rebuild and re-download for every other project.
- **Root cause:** pioarduino 54.03.21's "hybrid compile" builds the Arduino libs from a full IDF build and copies the result into the global `~/.platformio/packages/framework-arduinoespressif32-libs`. `check_reinstall_frwrk()` reinstalls stock libs whenever an env without `custom_sdkconfig` builds afterwards. `tools/build-all.ps1` over six projects would thrash.
- **Fix:** Not adopted. Preferred middle path if needed: `framework = arduino, espidf` for one project (builds inside its `.pio`), or an isolated `core_dir`. Link failure with `custom_sdkconfig` on 55.03.34 is reported upstream.
- **Evidence:** Read from `~/.platformio/platforms/espressif32/builder/frameworks/{arduino,espidf}.py` ([docs/12](../12-esp32-performance-and-framework.md) section 4c). Build times and isolation are unverified.
- **Status:** documented. **Applies to:** pioarduino 54.03.21.

<a id="q-tool-05"></a>
### Q-TOOL-05 Opening the serial port resets the board
- **Symptom:** A capture script resets the board every time it opens the port, losing state under test.
- **Root cause:** pyserial asserts DTR/RTS on open. The CH340C auto-reset circuit (T1/T2) turns that into an EN or BOOT pulse.
- **Fix:** Set `ser.dtr = False` (IO0 high, never enter the bootloader) and `ser.rts = False` (EN high) before opening. `tools-dev/screendump.py` and `sd_serial.py` do; `sd_serial.py --reset` pulses EN on purpose.
- **Evidence:** 2026-10-11 (screenshot tool); commit `496462e`. Investigation: [inv-serial-reset-on-open](03-investigations.md#inv-serial-dtr-reset).
- **Status:** worked-around. **Applies to:** any board with a CH340/CP210x DTR/RTS auto-reset.

<a id="q-tool-06"></a>
### Q-TOOL-06 Concurrent `pio` runs collide
- **Symptom:** `FileExistsError: [WinError 183] Cannot create a file when that file already exists: '...\.platformio\packages\tool-cppcheck'` during an upload; a `pio check` hung for hours; one agent killed every `pio` and `python` process.
- **Root cause:** Two PlatformIO runs installing or using the same global package at once (an upload against an agent's first `pio check`, which installs cppcheck). Shared build directories collide in the same way.
- **Fix:** Agents build with their own `PLATFORMIO_BUILD_DIR`; only the main session builds, flashes and uses the board; `lint.ps1` uses an isolated build dir; agents kill only processes they started. A collision invalidated one experiment (the backlight-off SD run), which was re-flashed and repeated.
- **Evidence:** Sessions 2026-10-11; the hung `pio check` (started 10:27) was stopped by hand.
- **Status:** worked-around. **Applies to:** PlatformIO with parallel agents or terminals.

<a id="q-tool-07"></a>
### Q-TOOL-07 Linting needs a translated compile database; "clean" can be an illusion
- **Symptom:** clang-tidy via `pio check` stops at `Arduino.h not found`, or takes 12 minutes and analyses the framework. An agent made the noise vanish by hiding the error lines, so "clean" meant cppcheck only.
- **Root cause:** `pio check --skip-packages` gives no framework include paths; without it the framework is analysed. GCC's command lines contain Xtensa flags clang rejects.
- **Fix:** `tools/lint.ps1 <project>`: cppcheck through `pio check` plus clang-tidy 18 run on a compile database from `pio run -t compiledb`, cut down to our files and translated for clang (`--target=i686-none-elf`, Xtensa defines, toolchain headers as `-isystem`). The database is cached and rebuilt when inputs change. It exits 1 on medium/high findings, never prints `secrets.h`, and skips the patched `common/lib/SD`. Silence a deliberate finding with `// NOLINT(<check>)` or `// cppcheck-suppress <id>`.
- **Evidence:** Raw findings on project 05 went from 357 to 3 (all in `secrets.h`); the first real run flagged a `memcmp` on a struct, and the lint agent's code read (not a tool) found the millis bug ([Q-FW-04](#q-fw-04)). Commit `2c4025e`, 2026-10-11. Investigation: [inv-lint-compiledb](03-investigations.md#inv-lint-setup).
- **Status:** fixed. **Applies to:** PlatformIO + clang-tidy on Xtensa targets.

<a id="q-tool-08"></a>
### Q-TOOL-08 Windows shell and runtime quirks
- **Symptom and fix, one per item:**
  - `$ErrorActionPreference = 'Stop'` breaks on native-command stderr in Windows PowerShell 5.1; the scripts use `'Continue'` and test exit codes.
  - .NET calls such as `[IO.File]::ReadAllText("main.cpp")` resolve a relative path against the process directory, not the PowerShell location. Use full paths.
  - `Select-String -First N` on a build log cut the build short; pipe without the limit.
  - A Python edit stopped on a text-encoding error; use `encoding='utf-8'` (or `PYTHONUTF8=1`).
  - `argparse` rejected intermixed arguments in `sd_serial.py`; use `parse_intermixed_args`.
  - A Windows-side `cat` waiting for input hung a command; give commands closed stdin.
  - Line endings: `.gitattributes` normalises; Git warns "CRLF will be replaced by LF" on some files.
- **Root cause:** Each is a platform difference, as listed.
- **Evidence:** Sessions 2026-10-08 to 2026-10-11; `tools/*.ps1` comments.
- **Status:** worked-around. **Applies to:** Windows PowerShell and Python.

<a id="q-tool-09"></a>
### Q-TOOL-09 Disk and partition changes need elevation
- **Symptom:** `Clear-Disk` refused: "Access to a CIM resource was not available". A card partitioned on a Raspberry Pi showed several partitions.
- **Root cause:** The session had no administrator rights.
- **Fix:** Run the same commands in an elevated PowerShell via `Start-Process -Verb RunAs`, with the user approving the UAC prompt; confirm the target disk number with the user before wiping.
- **Evidence:** 2026-10-11 (SD preparation).
- **Status:** worked-around. **Applies to:** Windows.

<a id="q-tool-10"></a>
### Q-TOOL-10 esptool v5 syntax and finding the port
- **Symptom:** `esptool` is not on PATH; `read_flash` style commands fail.
- **Root cause:** esptool v5 ships inside PlatformIO and uses hyphenated commands (`read-flash`, `write-flash`, `chip-id`).
- **Fix:** Call it as `penv\Scripts\python.exe packages\tool-esptoolpy\esptool.py ...`; list ports with `pio device list` (the board shows as CH340, `1A86:7523`). Back up the factory flash first with `tools/backup-flash.ps1`.
- **Evidence:** [docs/03](../03-programming-and-flashing.md), cold review 2026-10-08.
- **Status:** documented. **Applies to:** PlatformIO's bundled esptool.

<a id="q-tool-11"></a>
### Q-TOOL-11 Public-repo hygiene: secrets and identifiers in history
- **Symptom:** A real Wi-Fi name and a gateway address had slipped into a code comment, the hardware log and a `secrets.example.h` placeholder.
- **Root cause:** Real values were pasted into docs while debugging. A reviewer flagged them.
- **Fix:** Credentials only in git-ignored `secrets.h`; placeholders in docs, logs and the shared design canvas; history rewritten (24 commits) before any remote existed, with a backup bundle kept in a temp folder. Before going public, re-scan for: the board's MAC address (it is in `docs/hardware-log.md` and in the factory backup file name), commit author metadata, card serials in logs, and git-ignored logs on disk.
- **Evidence:** Reviews 2026-10-10 (commit `ca8bc70` replaced the values); this catalogue's own scrub rules.
- **Status:** worked-around; the MAC address needs a decision before publishing. **Applies to:** any repo going public.

<a id="q-tool-12"></a>
### Q-TOOL-12 Decoding a crash backtrace
- **Symptom:** A Guru Meditation or `task_wdt` backtrace of raw addresses.
- **Root cause:** n/a. Method.
- **Fix:** `monitor_filters = esp32_exception_decoder` decodes live. Offline: `xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf <addresses>` (from `~/.platformio/packages/toolchain-xtensa-esp-elf/bin`). A backtrace through `HTTPClient::getString` names [Q-NET-01](#q-net-01); one through `is_jpg` names [Q-LVGL-02](#q-lvgl-02); one ending in `xQueueSemaphoreTake` from an early task names [Q-FW-01](#q-fw-01).
- **Evidence:** Used for all three crashes above.
- **Status:** documented. **Applies to:** any Arduino-ESP32 firmware.

---

## Sources

External references cited above. URLs are those used in the repo's docs and session notes; they were not re-fetched when this catalogue was written.

Espressif and IDF:
- Espressif LCD FAQ (RGB drift, bounce buffer, cache line, PCLK limits): https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html
- ESP-IDF 5.4 RGB LCD (bounce buffer, XIP, cache must stay on): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- ESP-IDF 5.4 external RAM (`ALWAYSINTERNAL`, PSRAM stacks): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/external-ram.html
- ESP-IDF 5.4 mbedTLS (heap per session, dynamic buffers): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/mbedtls.html
- Espressif mbedTLS FAQ: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/mbedtls.html
- ESP-IDF SD SPI host (20 MHz default for standard cards): https://docs.espressif.com/projects/esp-idf/en/release-v4.0/api-reference/peripherals/sdspi_host.html
- ESP-IDF Windows setup (MSYS is not supported): https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/get-started/windows-setup.html

Libraries and projects:
- Arduino-ESP32 (core 3.2.1, `SD`, `HTTPClient`, `WiFi`): https://github.com/espressif/arduino-esp32
- arduino-esp32 issue 9348 (XIP/RODATA defaults declined; QIO crash note): https://github.com/espressif/arduino-esp32/issues/9348
- arduino-esp32 issue 9636 (WiFiClientSecure memory): https://github.com/espressif/arduino-esp32/issues/9636
- Arduino_GFX: https://github.com/moononournation/Arduino_GFX and issue 465 (ST7701 regression report): https://github.com/moononournation/Arduino_GFX/issues/465
- LVGL: https://github.com/lvgl/lvgl
- LVGL forum, Wi-Fi horizontal offset fixed by a bounce buffer: https://forum.lvgl.io/t/lvgl-horizontal-offset-when-esp32-s3-wifi-enabled-esp-idf/21888
- pioarduino: https://github.com/pioarduino/platform-espressif32 (issue 335: https://github.com/pioarduino/platform-espressif32/issues/335)
- ArduinoJson, external RAM: https://arduinojson.org/v7/how-to/use-external-ram-on-esp32/
- aquaElectronics ST7701 init and timings: https://github.com/aquaElectronics/esp32-4848s040-st7701
- rzeldent Sunton board JSONs: https://github.com/rzeldent/platformio-espressif32-sunton and esp32-smartdisplay: https://github.com/rzeldent/esp32-smartdisplay
- Vendor data pack mirror: https://github.com/NorthernMan54/ESP32-4848S040
- pypowerwall (local Powerwall API notes, SOC scaling): https://github.com/jasonacox/pypowerwall
- Parallax forum thread on old SD cards failing below their rated clock: https://forums.parallax.com/discussion/comment/1388211

Data services:
- NOAA SWPC products: https://services.swpc.noaa.gov/
- NOAA CPC ENSO advisory: https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso_advisory/
- SOHO real-time images: https://soho.nascom.nasa.gov/data/realtime/
- Open-Meteo forecast API: https://open-meteo.com/en/docs
- MET Norway API terms of service: https://api.met.no/doc/TermsOfService
- AuroraWatch UK: https://aurorawatch.lancs.ac.uk/

Local documents: [docs/07-gotchas.md](../07-gotchas.md), [docs/hardware-log.md](../hardware-log.md), [docs/11](../11-space-weather-and-climate-data.md), [docs/12](../12-esp32-performance-and-framework.md), [common/lib/SD/README-mini-screen.md](../../common/lib/SD/README-mini-screen.md).
