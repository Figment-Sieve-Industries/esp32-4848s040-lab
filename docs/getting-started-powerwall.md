# Getting started: Powerwall 3 display

Run a live Tesla Powerwall 3 dashboard on a cheap 4" touch screen (Guition ESP32-4848S040), read straight from your own gateway over your home network. This page walks you from "I own a Powerwall" to "it is on my wall".

You build and flash the firmware with PlatformIO, then enter your settings from your phone: on first boot the display opens its own Wi-Fi hotspot and a setup page ([section 5](#5-configure)). Typing the settings into a `secrets.h` file before building still works and is meant for developers. The setup portal builds and is host-tested but has not yet been tested on hardware.

Contents: [1 What you get](#1-what-you-get) | [2 Shopping list](#2-shopping-list) | [3 Before you start: about your Powerwall](#3-before-you-start-about-your-powerwall) | [4 Install the tools](#4-install-the-tools) | [5 Configure](#5-configure) | [6 Back up the factory firmware](#6-back-up-the-factory-firmware-first) | [7 Build, flash, first boot](#7-build-and-flash-first-boot) | [8 Troubleshooting](#8-troubleshooting) | [Flash from the browser](#flash-from-the-browser-prebuilt) | [9 Coming soon](#9-coming-soon) | [10 Help](#10-getting-help-and-contributing)

## 1. What you get

A small screen that shows your solar, house, battery and grid power in real time, today's energy totals and a history, plus some extras (weather, moon, aurora, space weather). It talks only to your Powerwall gateway on your home network. There is no Tesla cloud account, no server and no subscription. Swipe left and right to change screens.

Screens:

1. Power flow (solar, battery, grid, house)
2. Today (solar, home, grid totals so far today)
3. History (day / week / month tabs, solar and import bar charts, battery line)
4. Battery and grid (battery and grid health)
5. Weather with sunrise and sunset (Open-Meteo, no key)
6. Moon (phase and rise/set times)
7. Aurora (AuroraWatch UK plus NOAA Kp index)
8. Sun and space weather (NOAA, SOHO sun image)
9. El Nino (NOAA ENSO outlook)
10. Settings (backlight slider, on/off switches for the optional screens, SD card status)
11. About (credits)

What it does NOT do:

- It is **read-only**. It cannot change your Powerwall's mode, backup reserve or anything else.
- It uses the gateway's **basic local login** only. On the tested firmware (26.26.4) that exposes just three things: power and lifetime energy counters (`/api/meters/aggregates`), battery state of charge, and grid status. Backup reserve, operation mode, per-string solar, alerts and temperatures are **not** available this way ([Q-PW-01](guide/01-quirks-and-gotchas.md#q-pw-01)).
- The battery percentage is rescaled to hide the 5 % reserve the local API includes, so it should match the Tesla app, but that has not been compared side by side ([Q-PW-04](guide/01-quirks-and-gotchas.md#q-pw-04)).
- The "grid services / VPP event" indicator has never been seen to trigger ([Q-PW-07](guide/01-quirks-and-gotchas.md#q-pw-07)).
- It is tested only on a **Powerwall 3**. Powerwall 2 and Powerwall+ are untested.
- The weather, sky and aurora screens are tuned for the maintainer's location (UK). Weather, moon and sun times work anywhere; the AuroraWatch part is UK-only data, the Kp index is global.
- Units are fixed (degrees C, km/h, kWh).

Technical details: [project 05 README](../projects/05-powerwall-display/README.md).

## 2. Shopping list

| Item | Notes |
|---|---|
| The board: **Guition ESP32-4848S040C_I** (ESP32-S3 **N16R8**: 16 MB flash, 8 MB octal PSRAM; 4" 480x480 capacitive touch) | Sold under Guition, Sunton and JCZN names; the silkscreen reads `ESP32-4848S040`. Listings are often titled "4 inch 480x480 smart display capacitive touch" or "86 box". Buy the **no-relay** version: names ending `_Y_1` / `_Y_3` carry mains relays and a mains supply board you do not need. It must be **capacitive** (C), not resistive or no-touch. Prices and checked listings (UK eBay, AliExpress) are in [guide 06](guide/06-hardware-and-sourcing.md#where-to-buy). Prices were about GBP 19-28 in 2026-10. |
| USB-C **data** cable | A charge-only cable will not show a serial port. If no port appears, try another cable. |
| 5 V supply | A phone charger is enough. The backlight is the main load. |
| microSD card (optional but recommended) | Holds your history, settings and cached data so they survive a power cut. A plain **SDHC card of 32 GB or less, formatted FAT32**. A 2014 16 GB class 10 card worked; a 2011 2 GB card failed. A recent name-brand SDHC card is the safe choice but has not been tested. Never use an old small card. See [Q-SD-02](guide/01-quirks-and-gotchas.md#q-sd-02) and [guide 06](guide/06-hardware-and-sourcing.md#microsd-cards). The firmware never formats your card. |
| Wall mounting (optional) | The module is about 86 x 86 mm, the size of a standard wall box, so it fits an 86 mm box or a case sold as "86 box". Use only the no-relay board with a low-voltage 5 V supply; do not connect mains to anything. See [guide 06](guide/06-hardware-and-sourcing.md). |
| A computer | Windows, macOS or Linux, to build and flash. |

## 3. Before you start: about your Powerwall

You need three things from the Powerwall side.

**a) The gateway's IP address on your home network.**
The reliable way: open your router's admin page and look in the DHCP client list (or "connected devices") for the Tesla gateway. The Tesla app may also show network details, but menu paths vary, so check your router. Then give the gateway a **DHCP reservation** (a fixed address) so the address never changes. If the gateway is on a different network segment (IoT VLAN, separate Wi-Fi), the display's Wi-Fi network must be able to reach it ([Q-PW-06](guide/01-quirks-and-gotchas.md#q-pw-06)).

**b) The local "customer" password.**
The firmware logs in with `POST /api/login/Basic` using the user name `customer` and an empty email. The password is the **last 5 characters of the gateway password printed on the QR sticker** (on the gateway or the paperwork). This is the same login the gateway's own local web page uses. The firmware then sends the returned token on each request and logs in again if it expires.

Lockout caution: a wrong password makes the firmware retry with back-off (30 s, 60 s, 120 s, up to 5 minutes) and the screen says "login rejected - check password". Whether a gateway can lock you out after repeated bad logins is not verified, so **double-check the 5 characters before flashing** and do not leave a wrong password running for hours ([Q-PW-02](guide/01-quirks-and-gotchas.md#q-pw-02)). Never share this password in issues or screenshots.

**c) A 2.4 GHz Wi-Fi network** the display can join. The ESP32-S3 has no 5 GHz radio; a 5 GHz-only network is invisible to it. If your router combines bands under one name, that is fine. If the gateway lives on another VLAN, make sure the display's network can reach it ([Q-NET-09](guide/01-quirks-and-gotchas.md#q-net-09)).

## 4. Install the tools

You need PlatformIO (to build the firmware) and, on some PCs, a USB driver.

1. Install [VS Code](https://code.visualstudio.com/) and its **PlatformIO IDE** extension, or install PlatformIO Core on its own. Easiest on Windows is the VS Code extension. Install Git as well, to clone the repo.
   - **macOS / Linux, PlatformIO Core only:** `python3 -m pip install --user "platformio>=6.1,<6.2"` (or your package manager's `platformio`), then check with `pio --version`. The repo's pinned platform needs Core 6.1.x; newer Core versions are not tested here, and this exact command line has not been tested on macOS or Linux.
2. Clone this repository (the `reference/` submodules are not needed to build this project):
   ```
   git clone https://github.com/Figment-Sieve-Industries/esp32-4848s040-lab.git
   ```
3. **Windows:** run PlatformIO commands from **PowerShell** (or use the VS Code PlatformIO buttons), **not Git Bash**. Git Bash breaks the ESP toolchain installer ([Q-TOOL-01](guide/01-quirks-and-gotchas.md#q-tool-01)). If scripts are blocked: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
4. **USB driver:** the board uses a CH340C USB-serial chip. Windows 10/11 usually installs it by itself; if no COM port appears with the board plugged in (using a data cable), install the driver from the chip maker, WCH: <https://www.wch-ic.com/downloads/CH341SER_EXE.html>. macOS and Linux generally need nothing.
5. **Find the port:** on Windows open Device Manager > Ports (COM & LPT) and look for "USB-SERIAL CH340 (COMx)". On Linux it is usually `/dev/ttyUSB0`, on macOS `/dev/cu.wchusbserial*`.

The first build downloads the toolchain and takes a while. Use the project's pinned versions; do not upgrade PlatformIO platforms, as the repo pins a specific one ([Q-TOOL-03](guide/01-quirks-and-gotchas.md#q-tool-03)).

## 5. Configure

### Recommended: the setup page on your phone

Nothing to edit before building. After flashing ([section 7](#7-build-and-flash-first-boot)), a display with no stored settings starts in setup mode:

1. The screen shows a QR code and a hotspot name `PW-Display-XXXX`. Scan the code with your phone camera, or join that Wi-Fi (it has no password).
2. The setup page should open by itself. If it does not, open `http://192.168.4.1` in the phone's browser.
3. **Home Wi-Fi:** choose your 2.4 GHz network, type its password and press Connect. While the display joins, the hotspot can change channel and your phone may drop off it for a few seconds; it normally rejoins by itself. If the page says it lost contact, join `PW-Display-XXXX` again.
4. **Powerwall gateway:** press Find it, or type the gateway's address (see [3a](#3-before-you-start-about-your-powerwall)). Find it searches only the display's own network; a gateway on another network segment or VLAN must be typed in ([Q-PW-06](guide/01-quirks-and-gotchas.md#q-pw-06)). Then type the customer password ([3b](#3-before-you-start-about-your-powerwall)).
5. **Location:** type your town or postcode and press Look up (Open-Meteo, over your internet connection), or enter latitude, longitude and time zone by hand. Tick "Show UK aurora alerts" if you live in the UK.
6. Press **Test and save**. The display checks each step on its screen and on the page: it joins your Wi-Fi, checks that a gateway answers at that address, signs in **once** with your customer password, and checks the location and time zone. Only when everything passes are the settings saved (on the display's chip), and it restarts into the dashboard. If a step fails you see which one and why, and nothing is saved.

To change the settings later: Settings screen > Setup > **Re-run setup** (tap it twice within 3 seconds), or hold your finger on the screen for 5 seconds while the display powers on.

The settings are stored in the chip's NVS memory, written only when you save setup. Another way in, for many displays or no phone: a `config.json` file on the SD card ([project README](../projects/05-powerwall-display/README.md#configjson)).

### For developers: `secrets.h`

If a `secrets.h` file is present when you build, its values are used whenever nothing is stored on the display (they are not saved to NVS). Your settings then live in one file that is never committed to Git:

```
copy projects\05-powerwall-display\src\secrets.example.h projects\05-powerwall-display\src\secrets.h      (Windows)
cp projects/05-powerwall-display/src/secrets.example.h projects/05-powerwall-display/src/secrets.h        (macOS / Linux)
```

Open `secrets.h` in a text editor and fill in every field (placeholders shown):

| Define | What to put | Example |
|---|---|---|
| `WIFI_NETWORKS[]` | One `{"ssid", "password"}` pair per Wi-Fi network, 2.4 GHz. Add as many as you like; the firmware joins the strongest one in range and switches if it drops. Delete the sample rows you do not use. | `{"MyWiFi", "my-wifi-password"}` |
| `PW_HOST` | LAN IP address of your Powerwall gateway (with its DHCP reservation) | `"192.168.1.50"` |
| `PW_CUSTOMER_PASSWORD` | Last 5 characters of the gateway password on the QR sticker | `"AB12C"` |
| `TZ_INFO` | Your time zone as a POSIX TZ string (see below) | `"GMT0BST,M3.5.0/1,M10.5.0"` |
| `WEATHER_LAT` | Latitude of your home in decimal degrees (positive north). Used for weather, sunrise/sunset, moon and aurora darkness. | `51.5072` |
| `WEATHER_LON` | Longitude in decimal degrees (positive east). Keep the brackets for negative values. | `(-0.1276)` |
| `WEATHER_NAME` | Place name shown on the weather screen, in capitals. | `"MYTOWN"` |

Keep `secrets.h` private. It holds your Wi-Fi password and gateway password and is git-ignored. Do not paste it into issues. The latitude and longitude are your home location; round them (for example to one decimal) if you want less precision, the screens still work.

**Time zone and units.** The clock is set from the internet (NTP: `pool.ntp.org`, `time.google.com`) and converted to local time with the `TZ_INFO` string, which uses the standard POSIX format `STD offset DST,start,end`. The offset sign is inverted: west of Greenwich is positive. Examples:

| Where | `TZ_INFO` |
|---|---|
| UK / Ireland (default) | `"GMT0BST,M3.5.0/1,M10.5.0"` |
| Central Europe | `"CET-1CEST,M3.5.0,M10.5.0/3"` |
| US Eastern | `"EST5EDT,M3.2.0,M11.1.0"` |
| US Pacific | `"PST8PDT,M3.2.0,M11.1.0"` |
| Australia (Sydney) | `"AEST-10AEDT,M10.1.0,M4.1.0/3"` |

These strings come from the standard POSIX/tz-database conventions, not from this repo; check yours against a list such as the "POSIX TZ" column of the tz database. Without a valid clock the internet-data screens will not load, because certificate checks need the correct date ([Q-NET-06](guide/01-quirks-and-gotchas.md#q-net-06)). Temperatures are in degrees C and wind in km/h; there is no setting to change that.

## 6. Back up the factory firmware first

Back up the board's original 16 MB flash before the first upload. It is the only way back to the vendor firmware.

Windows (PowerShell, board plugged in; COM9 is only an example, replace it with your port):

```
.\tools\backup-flash.ps1 -Port COM9
```

It takes a few minutes and writes a file under `firmware\factory\` (git-ignored; never commit it, it can hold personal data). To restore: `.\tools\restore-flash.ps1 -File firmware\factory\<file>.bin -Port COM9` ([D-18](guide/02-debugging-playbook.md#d-18-flash-backup-and-restore)). On macOS/Linux, use `esptool` directly: `esptool.py --port <port> --baud 921600 read_flash 0 0x1000000 backup.bin` (this exact command line is not tested here).

## 7. Build and flash, first boot

Plug the board in with the USB-C data cable. Uploading needs no button presses (auto-reset works).

Windows (PowerShell, from the repo root; replace COM9 with your port):

```
.\tools\pio.ps1 05-powerwall-display build
.\tools\pio.ps1 05-powerwall-display all -Port COM9
```

`all` builds, uploads and opens the serial monitor. Other actions: `build`, `upload`, `monitor`.

macOS / Linux, or any shell where PlatformIO is on the PATH:

```
pio run -d projects/05-powerwall-display -t upload --upload-port /dev/ttyUSB0
pio device monitor -d projects/05-powerwall-display
```

**What you should see after flashing:** the upload ends with `SUCCESS`, the board restarts, the backlight comes on and the first screen appears within a few seconds; the serial monitor then prints boot lines and the heartbeat every 10 seconds.

**First boot:** without stored settings (and without a `secrets.h` in the build) the display starts the setup portal; follow [section 5](#5-configure). Once set up, the screen lights up, joins Wi-Fi, sets the clock and starts polling the gateway. Expect the power-flow screen with live numbers within a few seconds to a minute. Swipe left/right to move between screens. Weather, moon, aurora, sun and El Nino screens fill in as each background fetch completes (some refresh only every 30 minutes to 12 hours). Rotation is set so the USB-C port is at the bottom.

**Serial monitor** (115200 baud). Every 10 seconds a heartbeat prints live values, memory and network counters ([D-01](guide/02-debugging-playbook.md#d-01-serial-heartbeat)). The first line looks like this (Wi-Fi name and numbers are examples):

```
[MyWiFi -58 dBm] solar 2500 home 3400 grid 900 batt 0 soc 100.0% on-grid | today solar 0.00 home 0.00 kWh | screen 0 | 46.4 fps
```

A subset of the serial commands (type one and press Enter; the full list is at the top of `projects/05-powerwall-display/src/main.cpp`, and [D-09](guide/02-debugging-playbook.md#d-09-serial-command-interface) explains the interface):

| Command | What it does |
|---|---|
| `s <n>` | Go to screen n (`#err screen disabled` if it is switched off in Settings) |
| `n` / `p` | Next / previous enabled screen |
| `l` | List the screens (switched-off ones marked `-`) |
| `e [<n> <0\|1>]` | List or switch screens on/off (same as the Settings switches) |
| `b <0-255>` | Set backlight now (not saved; the panel cuts out below about 100) |
| `m` | Print memory / heap line |
| `x` | Screenshot (used by `tools-dev/screendump.py`, [D-10](guide/02-debugging-playbook.md#d-10-framebuffer-screenshots)) |
| `v <0\|1>` | Privacy mode for screenshots: hides your Wi-Fi name and town |
| `c` | List the cached records on the SD card |
| `y [d\|w\|m\|-\|+]` | History screen: day/week/month tab, previous/next period |
| `k` | Diagnostic: force a display scan-out restart (see the guide if the picture drifts) |
| `r` | Reboot |
| `h` | Help |

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Never joins Wi-Fi; log says `no matching wifi found`; the firmware prints a scan of visible networks | The network is 5 GHz only, or out of range, or the name/password in `secrets.h` is wrong | Use a 2.4 GHz network (the ESP32-S3 cannot see 5 GHz). Check spelling and case. See [Q-NET-09](guide/01-quirks-and-gotchas.md#q-net-09) |
| Screen says "login rejected - check password" (HTTP 401/403) | Wrong customer password (must be the last 5 characters of the QR-sticker gateway password) | Run setup again (Settings > Re-run setup) and enter it; with a developer build, fix `PW_CUSTOMER_PASSWORD` in `secrets.h` and reflash. The firmware backs off 30 s up to 5 min between tries. Do not leave a wrong password running. See [Q-PW-02](guide/01-quirks-and-gotchas.md#q-pw-02) |
| Cannot reach the gateway at all (timeouts) | Wrong `PW_HOST`, the address changed, or the display's network cannot reach the gateway's VLAN | Check the IP in your router, add a DHCP reservation, allow routing between the networks. See [Q-PW-06](guide/01-quirks-and-gotchas.md#q-pw-06) |
| Values show "--" or the screen says data is stale | The gateway did not answer in time, or a meter is missing; the firmware shows "--" rather than a false 0 | Wait a minute and watch the serial heartbeat. If persistent, check login and Wi-Fi signal. See [Q-PW-03](guide/01-quirks-and-gotchas.md#q-pw-03), [Q-NET-05](guide/01-quirks-and-gotchas.md#q-net-05) |
| Powerwall works but weather / moon / aurora / sun / El Nino are empty | Clock not yet set, or a TLS failure | Wait a few minutes after boot (NTP first). Check Wi-Fi and internet access, and that `TZ_INFO` is valid. See [Q-NET-06](guide/01-quirks-and-gotchas.md#q-net-06) |
| Log shows TLS errors such as `SSL - Memory allocation failed` or a rare `HTTP 400` | Low internal RAM during a handshake, or the gateway refusing handshakes at about 30 a minute | Do not change the memory settings; reboot (`r`) and report it with the `m` line. See [Q-NET-04](guide/01-quirks-and-gotchas.md#q-net-04), [D-03](guide/02-debugging-playbook.md#d-03-tls-per-job-low-water-lines) |
| SD card not detected, or "SD none" in the heartbeat | Old or tiny (SDSC) card, not FAT32, card above 32 GB, or not seated | Use a plain SDHC card up to 32 GB formatted FAT32. The firmware never formats it. It works without a card, but history will not survive a power cut. See [Q-SD-02](guide/01-quirks-and-gotchas.md#q-sd-02), [Q-SD-04](guide/01-quirks-and-gotchas.md#q-sd-04), [D-16](guide/02-debugging-playbook.md#d-16-sd-test-bench) |
| Serial link drops when you insert the SD card | Known side effect on some PCs; the board itself does not reset | Re-open the serial monitor. See [Q-SD-07](guide/01-quirks-and-gotchas.md#q-sd-07) |
| Whole screen flickers | Display DMA starved (the repo already enables bounce buffers) | If you changed the code or build flags, undo them. Stock firmware should not flicker. See [Q-GFX-01](guide/01-quirks-and-gotchas.md#q-gfx-01) |
| Crackle or noise on the panel after running a while | PSRAM bandwidth contention | Stock firmware is tuned for this; if you modified memory settings, revert. See [Q-GFX-02](guide/01-quirks-and-gotchas.md#q-gfx-02), [symptom table](guide/02-debugging-playbook.md#symptom-to-first-method) |
| Touch does not respond | Touch chip not detected at boot (not verified as a common failure) | Reboot (`r`) or power-cycle, and check the boot log for the touch probe line. The touch chip (GT911) has no interrupt or reset line wired, so its address depends on power-up. See [Q-TOUCH-01](guide/01-quirks-and-gotchas.md#q-touch-01) |
| Upload fails ("port busy", "No serial data", no COM port) | Charge-only cable, missing CH340 driver, another program (serial monitor) holding the port | Use a data cable, install the WCH driver, close other monitors, try another USB port. See [section 4](#4-install-the-tools) and [guide 06](guide/06-hardware-and-sourcing.md#what-else-you-need) |
| `ERROR: MSys/Mingw is not supported` on Windows | Building from Git Bash | Use PowerShell or the VS Code buttons. See [Q-TOOL-01](guide/01-quirks-and-gotchas.md#q-tool-01) |
| Opening a serial monitor or script restarts the board | The CH340C auto-reset circuit reacts to the port opening | Normal for a plain monitor. Scripts must set DTR/RTS low before opening, as `tools-dev/screendump.py` does. See [Q-TOOL-05](guide/01-quirks-and-gotchas.md#q-tool-05) |
| Two builds at once fail with file-exists errors | Concurrent `pio` runs share packages | Run one `pio` at a time. See [Q-TOOL-06](guide/01-quirks-and-gotchas.md#q-tool-06) |
| Board will not boot or behaves oddly after flashing | Bad flash or wrong settings | Restore your backup ([D-18](guide/02-debugging-playbook.md#d-18-flash-backup-and-restore)) and try again |

More: the full symptom table is in the [debugging playbook](guide/02-debugging-playbook.md#symptom-to-first-method), and every known quirk is indexed in [guide 01](guide/01-quirks-and-gotchas.md).

## Flash from the browser (prebuilt)

A prebuilt image can be installed from Chrome or Edge on a desktop computer, with no PlatformIO: open <https://figment-sieve-industries.github.io/esp32-4848s040-lab/>, plug the board in with a USB-C data cable and press the install button. Back up the factory firmware first ([section 6](#6-back-up-the-factory-firmware-first)).

**Until on-device setup ships, prebuilt firmware can't join your Wi-Fi.** The prebuilt Powerwall image is built with the placeholder `secrets.example.h`, so it boots and shows the screens but stays offline ("--"). To get live data, build from source with your own `secrets.h` ([section 5](#5-configure) and [section 7](#7-build-and-flash-first-boot)). The simple demo projects (01, 02, 03) need no settings and work from the browser as they are. The page, images and release process are described in [ADR-36](guide/04-patterns-and-decisions.md#adr-36-release-pipeline-and-browser-flashing).

## 9. Coming soon

On-device setup (the phone setup page, [section 5](#5-configure)) and gateway discovery (Find it) are in the firmware and waiting for a hardware test. Planned next, with no dates promised: a prebuilt browser-flash image that uses the setup page, so no build is needed at all.

## 10. Getting help and contributing

- Questions, bugs and "it worked for me" reports: open an issue at <https://github.com/Figment-Sieve-Industries/esp32-4848s040-lab/issues>. Redact your Wi-Fi name, gateway IP, password, location and any serial log before pasting; logs contain the Wi-Fi name.
- Please share results from other Powerwall models (Powerwall 2, Powerwall+) and other gateway firmware versions. They are untested, and a note on what the basic local login returns would help a lot.
- Found a new quirk? The [development guide](guide/README.md) records them; pull requests are welcome.
