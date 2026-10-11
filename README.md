# ESP32-4848S040 lab
<!-- Social preview image: docs/images/social-preview.png (1280x640). GitHub has no API for it: upload it manually in the repo's Settings > General > Social preview. -->

A hobby lab for the **Guition ESP32-4848S040**: an ESP32-S3 board with a 4" 480x480 capacitive touch screen in an 86 mm wall-box format. The repository holds PlatformIO projects, a shared board library, hardware notes and a development guide built on measurements taken on a real unit. The headline project is a **Tesla Powerwall 3 dashboard** that reads the gateway's local LAN API, with no cloud account and no server.

> The sticker may read something like `ESP32_3838S040C_I`. The PCB silkscreen, the vendor schematic and every reference source call this 480x480 board the **ESP32-4848S040**. `4848` = 480x480, `040` = 4.0".

More projects will be added over time. The [projects table](#projects) lists everything that is here now and how far each one has got.

## Tesla Powerwall 3 display

A wall-mounted energy monitor for **Tesla Powerwall 3** owners. The ESP32 talks straight to the **Powerwall gateway's local API** over your home Wi-Fi, so it keeps working when the internet or Tesla's servers do not, and nothing about your home leaves the network. Built with **LVGL 9** on the **ESP32-S3**, it shows live **solar**, **home battery**, grid and house load as an **energy monitor** you can glance at in the hall.

**Own a Powerwall 3 and want one?** Start with **[Getting started: run the Powerwall 3 display on your own board](docs/getting-started-powerwall.md)**: shopping list, finding your gateway, setup, flashing and troubleshooting.

<table>
  <tr>
    <td align="center"><a href="docs/images/screens/05-0-house.png"><img src="docs/images/screens/05-0-house.png" width="240" alt="Power flow screen: solar, grid, home load and Powerwall as pills around a house, with weather and grid status"></a><br>Power flow</td>
    <td align="center"><a href="docs/images/screens/05-1-today.png"><img src="docs/images/screens/05-1-today.png" width="240" alt="Today screen: self-powered ring, energy totals and solar by hour"></a><br>Today</td>
    <td align="center"><a href="docs/images/screens/05-2-history.png"><img src="docs/images/screens/05-2-history.png" width="240" alt="History screen, Day tab: solar by hour, grid import and battery percentage"></a><br>History (day)</td>
  </tr>
  <tr>
    <td align="center"><a href="docs/images/screens/05-3-health.png"><img src="docs/images/screens/05-3-health.png" width="240" alt="Battery and grid screen: state of charge, grid voltage and frequency"></a><br>Battery and grid</td>
    <td align="center"><a href="docs/images/screens/05-4-weather.png"><img src="docs/images/screens/05-4-weather.png" width="240" alt="Weather screen: current conditions, sunrise and sunset, and the next 10 hours"></a><br>Weather</td>
    <td align="center"><a href="docs/images/screens/05-5-moon.png"><img src="docs/images/screens/05-5-moon.png" width="240" alt="Moon screen: phase, illumination, rise and set"></a><br>Moon</td>
  </tr>
  <tr>
    <td align="center"><a href="docs/images/screens/05-6-aurora.png"><img src="docs/images/screens/05-6-aurora.png" width="240" alt="Aurora screen: AuroraWatch UK status, 24 hour activity and Kp forecast"></a><br>Aurora (UK)</td>
    <td align="center"><a href="docs/images/screens/05-7-sun.png"><img src="docs/images/screens/05-7-sun.png" width="240" alt="Sun screen: live SOHO photo, flare class, solar wind and sunspots"></a><br>Sun and space weather</td>
    <td align="center"><a href="docs/images/screens/05-8-enso.png"><img src="docs/images/screens/05-8-enso.png" width="240" alt="El Nino screen: ENSO status, Nino 3.4 and outlook odds"></a><br>El Nino (ENSO)</td>
  </tr>
  <tr>
    <td align="center"><a href="docs/images/screens/05-9-settings.png"><img src="docs/images/screens/05-9-settings.png" width="240" alt="Settings screen: brightness slider, screen on/off switches and SD card status"></a><br>Settings (brightness, screens on/off, SD card)</td>
    <td align="center"><a href="docs/images/screens/05-10-about.png"><img src="docs/images/screens/05-10-about.png" width="240" alt="About screen: data sources and credits"></a><br>About and credits</td>
    <td align="center"><a href="docs/images/screens/05-2-history-week.png"><img src="docs/images/screens/05-2-history-week.png" width="240" alt="History screen, Week tab: solar and grid import per day"></a><br>History (week)</td>
  </tr>
  <tr>
    <td align="center"><a href="docs/images/screens/05-2-history-month.png"><img src="docs/images/screens/05-2-history-month.png" width="240" alt="History screen, Month tab: solar and grid import per day"></a><br>History (month)</td>
    <td></td>
    <td></td>
  </tr>
</table>

Screenshots are real framebuffer captures from the board, taken in privacy mode (placeholder Wi-Fi name and town).

**What it does**

- **Live power flow** from the gateway every 2 seconds: solar, grid, house load and Powerwall power, battery state of charge, grid import or export, voltage, and an off-grid banner during an outage.
- **Eleven swipeable screens:** power flow, today, history, battery and grid, weather, moon, aurora (UK), sun and space weather, El Nino, settings (brightness, turn optional screens on or off, SD card status), and about. Screens you turn off are skipped when swiping and stop their data fetches.
- **History charts:** solar and grid import per hour, per day of the week and per day of the month, with the battery level on the day view, a tap-for-readout on every bar and a stepper back through earlier periods. Needs the SD card for anything before today.
- **Weather** shows today's sunrise and sunset.
- **Local only.** It uses the same customer login as the gateway's own web page. There is no Tesla cloud account, API key or subscription, and your Powerwall readings are never sent anywhere. The only outside requests are to the public weather and space-weather sources listed below (the weather ones carry your configured location).
- **SD card logging and cache (optional).** A one-minute CSV log of the readings, today's energy totals that survive a power cut, a daily energy record that feeds the history charts, and a cache of the fetched weather and space data so the screens fill straight after a reboot. Nothing is written to the board's internal flash while it runs.
- **Extras for the curious:** weather and a 10-hour forecast, the moon computed on the device, the UK aurora alert level, Kp index, a live photo of the Sun, and the El Nino outlook.

**Get started:** [`projects/05-powerwall-display/README.md`](projects/05-powerwall-display/README.md) covers what you need, the gateway login, configuration, flashing and troubleshooting.

It is an unofficial project built on an undocumented gateway interface. Read the [limitations](projects/05-powerwall-display/README.md#limitations-and-notes) first.

## Projects

Each project is a PlatformIO project in its own numbered folder under [`projects/`](projects/). More will be added; [`projects/README.md`](projects/README.md) is the live index with the planned ideas.

Status vocabulary: `idea`, `wip`, `builds`, `works on hw`, `done`. `builds` means it compiles and has not been run on a board; `works on hw` means it has been run on a real board and the result checked.

| # | Project | What it does | Status |
|---|---|---|---|
| 01 | [hello-display-touch](projects/01-hello-display-touch/) | Colour test, finger-paint and touch coordinates on screen and serial. Plain Arduino_GFX, no LVGL. | works on hw |
| 02 | [lvgl-hello](projects/02-lvgl-hello/) | LVGL 9.5 starting point: a button, a backlight slider and an FPS monitor. The template for UI projects. | works on hw |
| 03 | [lvgl-demos](projects/03-lvgl-demos/) | Touch launcher for LVGL's official demos (Widgets, Music, Benchmark, Stress). Source of the benchmark numbers in the hardware log. | works on hw |
| 04 | [lvgl-fb2-bench](projects/04-lvgl-fb2-bench/) | Experiment: double framebuffers with a vsync swap. It works but measured slower (20 vs 35 FPS), so it is not used. | works on hw |
| 05 | [**powerwall-display**](projects/05-powerwall-display/) | **Tesla Powerwall 3 dashboard** from the gateway's local API, plus history charts, weather, moon, aurora, space weather and El Nino screens, SD logging and cache. | works on hw |
| 06 | [sd-card-test](projects/06-sd-card-test/) | microSD bring-up and the shared SD code: 19 functional tests, clock sweep, soak and hot-swap, all drivable over serial. | works on hw |

## Development guide

[`docs/guide/`](docs/guide/README.md) is a growing guide for this board and similar ESP32-S3 RGB-panel boards. It records what was measured, what failed and why, so the next project does not repeat it.

| Chapter | What it covers |
|---|---|
| [Guide index](docs/guide/README.md) | How to use the guide and how to add to it |
| [01 Quirks and gotchas](docs/guide/01-quirks-and-gotchas.md) | Catalogue of hardware, toolchain, library, network-API and tooling quirks, by ID, with fixes |
| [02 Debugging playbook](docs/guide/02-debugging-playbook.md) | Methods that worked (heartbeat, A/B builds, screenshots, soaks, host tests), with a symptom-to-method table |
| [03 Investigations](docs/guide/03-investigations.md) | Write-ups of each real debugging case, with before and after numbers, and ideas tried and rejected |
| [04 Patterns and decisions](docs/guide/04-patterns-and-decisions.md) | The architecture decisions that survived, with alternatives and consequences |
| [05 New project checklist](docs/guide/05-new-project-checklist.md) | Ordered steps for starting a project on this board or porting to another |
| [06 Hardware and sourcing](docs/guide/06-hardware-and-sourcing.md) | Variants, specifications, what else you need and where to buy |
| [99 Sources and credits](docs/guide/99-sources-and-credits.md) | Every library, data source and reference used, with licence terms |

Hardware documents:

| Doc | What's in it |
|---|---|
| [docs/01-hardware-overview.md](docs/01-hardware-overview.md) | Specs, variants, components, connectors, board photos |
| [docs/02-pinout.md](docs/02-pinout.md) | Full GPIO map, strapping pins, free pins, second I2C bus |
| [docs/03-programming-and-flashing.md](docs/03-programming-and-flashing.md) | USB-C and CH340C, drivers, boot mode, backup and restore |
| [docs/04-display-st7701.md](docs/04-display-st7701.md) | RGB timings and variants, init sequence, rotation |
| [docs/05-touch-gt911.md](docs/05-touch-gt911.md) | GT911 registers, address, polling, rotation |
| [docs/06-frameworks-and-libraries.md](docs/06-frameworks-and-libraries.md) | Arduino vs ESP-IDF trade-offs; PlatformIO, LVGL, ESPHome |
| [docs/07-gotchas.md](docs/07-gotchas.md) | The original short table of pitfalls (the guide's chapter 01 supersedes it) |
| [docs/08-power-battery-sleep.md](docs/08-power-battery-sleep.md) | Power tree, battery charger, sleep and wake options |
| [docs/09-alternative-displays.md](docs/09-alternative-displays.md) | Similar boards and prices |
| [docs/10-esp32-p4-displays.md](docs/10-esp32-p4-displays.md) | ESP32-P4 MIPI-DSI display boards |
| [docs/11-space-weather-and-climate-data.md](docs/11-space-weather-and-climate-data.md) | Research on the public space-weather and climate data sources |
| [docs/12-esp32-performance-and-framework.md](docs/12-esp32-performance-and-framework.md) | Memory and framework research |
| [docs/hardware-log.md](docs/hardware-log.md) | What has been verified and measured on a real board |
| [docs/datasheets/](docs/datasheets/) and [docs/vendor/](docs/vendor/) | Part datasheets; the vendor data pack with the schematic (the pin authority) |

## Hardware and where to buy

| | |
|---|---|
| MCU | ESP32-S3-WROOM-1U-N16R8: dual-core 240 MHz, 16 MB flash, 8 MB octal PSRAM, Wi-Fi (2.4 GHz) and BLE 5. U.FL external antenna: do not run Wi-Fi with it disconnected. |
| Display | 4.0" TFT (likely IPS), 480x480, ST7701S driver, 16-bit RGB bus plus 3-wire SPI for set-up |
| Touch | GT911 capacitive, I2C |
| USB | USB-C to a CH340C USB-UART with auto-reset: flash and monitor over one cable, no buttons needed |
| Other | microSD (SPI), NS4168 I2S amplifier and speaker connector, Li-ion charger, BOOT and RST buttons, UART header; some versions add 1 or 3 mains relays |

Buy the capacitive-touch version **without relays** unless you want them. Listings for sale, with notes, are in [docs/guide/06-hardware-and-sourcing.md](docs/guide/06-hardware-and-sourcing.md). The unit used for this repository came from this eBay UK listing, which may have ended: <https://www.ebay.co.uk/itm/147605654410>. Other verified listings (eBay UK and AliExpress) are on the same page. **Prices change**, so check the listing and the model option (`ESP32-4848S040`) before ordering.

## Quick start

Building the Powerwall display for your own home? Follow [docs/getting-started-powerwall.md](docs/getting-started-powerwall.md) instead; it covers the same steps with the Powerwall-specific setup.

Use **PowerShell** (or VS Code's PlatformIO buttons). Do not use Git Bash: ESP's tool installer rejects MSYS shells and the first build fails (see [docs/07](docs/07-gotchas.md)).

1. **Back up the factory firmware before the first upload.** Connect the board by USB-C, then run `.\tools\backup-flash.ps1`. It saves the full 16 MB image to `firmware/factory/` and takes a few minutes. Details: [docs/03](docs/03-programming-and-flashing.md).
2. Install VS Code with the **PlatformIO IDE** extension, or PlatformIO Core. The board should appear as **USB-SERIAL CH340 (COMx)**; if it does not, see [docs/03](docs/03-programming-and-flashing.md).
3. Build, flash and monitor a project:
   ```powershell
   .\tools\pio.ps1 01-hello-display-touch all -Port COM5
   ```
   The first argument is the project folder, the second is `build`, `upload`, `monitor` or `all`. The first build downloads the toolchain and libraries and takes several minutes.
4. **Secrets.** Projects that need credentials (project 05) read them from `src/secrets.h`. Copy `src/secrets.example.h` to `src/secrets.h` and fill it in. `secrets.h` is git-ignored and must never be committed.
5. **Lint** C and C++ changes with `.\tools\lint.ps1 <project>` (cppcheck and clang-tidy), and check that everything still builds with `.\tools\build-all.ps1`.

## Repository layout

```
esp32-4848s040-lab/
├── README.md                this file
├── CLAUDE.md                rules for agents and contributors working in this repo
├── docs/                    hardware notes, pinout, how-tos, datasheets, vendor pack
│   ├── guide/               the development guide (quirks, playbook, investigations, decisions)
│   ├── images/              board photos and screenshots
│   └── README.md            docs index
├── common/
│   ├── platformio-base.ini  shared PlatformIO settings every project extends
│   ├── boards/              PlatformIO board JSONs (from rzeldent)
│   └── lib/                 Board4848S040 (pins, ST7701, GT911, LVGL glue), SdStore, patched SD
├── projects/                one folder per project; README.md is the index
├── tools/                   PowerShell helpers: build, flash, monitor, lint, flash backup and restore
├── firmware/                factory firmware backups and vendor images (see its README)
└── reference/               prior-art repositories as git submodules (read-only)
```

Clone with the submodules, or fetch them afterwards:

```sh
git clone --recurse-submodules https://github.com/Figment-Sieve-Industries/esp32-4848s040-lab.git
# or, after a plain clone:
git submodule update --init
```

## Contributing and issues

Issues are welcome: a quirk on your unit, a Powerwall firmware difference, a documentation error, a better listing to buy from. Say which board variant and (for project 05) which Powerwall firmware you have, and never paste Wi-Fi names, passwords, tokens or your gateway's address. Pull requests are welcome too; read [`CLAUDE.md`](CLAUDE.md) for the repository rules (build and lint before committing, pins only in the one header, credentials only in the git-ignored `secrets.h`) and [`docs/guide/README.md`](docs/guide/README.md) for how to add a quirk or an investigation to the guide. Please do not claim a hardware result you have not seen on a board.

## Licence

The code and documentation written for this repository are released under the **MIT licence**. Third-party material keeps its own terms:

- Vendor documents and datasheets in `docs/vendor/` and `docs/datasheets/` belong to their owners and are included for reference only.
- The PlatformIO board files in `common/boards/` come from [rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton) and are **GPL-3.0**.
- The repositories in `reference/` are git submodules with their own licences, and are not part of this repository's code.
- Libraries and data sources used by the projects have their own licences and terms. Several data sources allow non-commercial use only.

The full list, with licences, is in [docs/guide/99-sources-and-credits.md](docs/guide/99-sources-and-credits.md).

## Sources and credits

The main sources, with licences and terms in [docs/guide/99-sources-and-credits.md](docs/guide/99-sources-and-credits.md):

- **Libraries and tools:** [LVGL](https://github.com/lvgl/lvgl) (MIT), [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) by Moon On Our Nation (BSD 2-clause), [ArduinoJson](https://github.com/bblanchon/ArduinoJson) by Benoit Blanchon (MIT), [arduino-esp32](https://github.com/espressif/arduino-esp32) and [ESP-IDF](https://github.com/espressif/esp-idf) by Espressif, [pioarduino](https://github.com/pioarduino/platform-espressif32) and [PlatformIO](https://platformio.org/).
- **Reference repositories** (submodules in `reference/`): [esp32-4848s040-st7701](https://github.com/aquaElectronics/esp32-4848s040-st7701) by aquaElectronics, [esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay) by rzeldent, [ESP32-4848S040-LVGL-LGFX](https://github.com/joevial/ESP32-4848S040-LVGL-LGFX) by joevial, [Guition-ESP32-S3-4848S040](https://github.com/alaltitov/Guition-ESP32-S3-4848S040) by Alexey Titov, and [ESP32-4848S040-First-Start](https://github.com/abcbbck-png/ESP32-4848S040-First-Start) by abcbbck-png.
- **Board definitions and datasheets:** [rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton).
- **Data used by project 05:** [Open-Meteo](https://open-meteo.com/) (weather, CC BY 4.0), [AuroraWatch UK](https://aurorawatch.lancs.ac.uk/) (Lancaster University, CC BY-NC-SA 3.0), [NOAA Space Weather Prediction Center](https://www.swpc.noaa.gov/), [NOAA Climate Prediction Center](https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso_advisory/), [NASA Scientific Visualization Studio](https://svs.gsfc.nasa.gov/4768) (moon image), [SOHO](https://soho.nascom.nasa.gov/) (ESA and NASA, sun photo) and [MET Norway](https://api.met.no/) (moonrise and moonset, CC BY 4.0).
- **Trademarks:** Tesla and Powerwall are trademarks of Tesla, Inc. This project is unofficial and is not affiliated with, endorsed by or supported by Tesla. The gateway's local API is not a published or supported interface.

---

Made by **Figment Sieve Industries**. The code and documentation were developed with Claude (Anthropic) as a coding assistant.
