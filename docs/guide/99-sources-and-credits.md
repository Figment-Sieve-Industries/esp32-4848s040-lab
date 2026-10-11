# 99. Sources and credits

[Guide index](README.md) | [01 Quirks](01-quirks-and-gotchas.md) | [02 Debugging](02-debugging-playbook.md) | [03 Investigations](03-investigations.md) | [04 Patterns](04-patterns-and-decisions.md) | [05 New project](05-new-project-checklist.md) | [06 Hardware](06-hardware-and-sourcing.md) | [Credits](99-sources-and-credits.md)

Everything this repo uses or draws on, with the licence or terms as far as they could be established. One line per item. Licences were read from the upstream repository or site on **2026-10-11**; where that could not be done the entry says "not stated" or "unverified". This is a good-faith list, not legal advice. If you find an error, open an issue.

This repo is a personal, non-commercial hobby project. Several data sources below allow non-commercial use only. If you fork this for a product, an advert-supported app or anything paid, check each source's terms again.

The About screen of project 05 ("Data & credits" card) shows the short form of the data credits on the device.

## 1. Hardware documents and vendor material

| Item | Used for | Link | Licence / terms |
|---|---|---|---|
| Guition / Shenzhen Jingcai Intelligent (JCZN) data pack for the 4.0 inch ESP32-4848S040 | `docs/vendor/`: schematic pages (`schematic-1.png`, `schematic-2.png`), IO pin sheet (`io-pin-distribution.xlsx`), specification and getting-started PDFs, dimension drawings, module pin definition. The **schematic is the pin authority** for the repo | Official: <http://pan.jczn1688.com/directlink/1/ESP32%20module/4.0inch_ESP32-4848S040.zip> (slow server). Mirror: <https://github.com/NorthernMan54/ESP32-4848S040> | Vendor copyright. **No licence stated** by the vendor, and the mirror repository shows none. Included for reference |
| Vendor factory demo images (`86switch_onoff.bin`, `switch86_lvgl_music.bin`, `4.0_LvglWidgets.bin`) | Restoring factory state. **Linked only, not stored** in the repo | <https://github.com/NorthernMan54/ESP32-4848S040/tree/main/ESP32-4848S040/8-Burn%20operation/Burn%20files> | Vendor firmware, no licence stated |
| Datasheets: ESP32-S3, ESP32-S3-WROOM-1/1U, ST7701S, GT911, NS4168 | `docs/datasheets/` | Espressif: <https://www.espressif.com/en/support/documents/technical-documents>. Sitronix (ST7701S), Goodix (GT911) and the NS4168 maker host their own | Manufacturers' copyright; free to download, redistribution **not licensed**. Copied from rzeldent's repository (next row) |
| rzeldent / platformio-espressif32-sunton | Source of the datasheet PDFs and the two board photos (`docs/images/esp32-4848S040C-front.png`, `...-back.jpg`) and of the board JSONs (section 2). No longer a submodule | <https://github.com/rzeldent/platformio-espressif32-sunton> | Repository licence **GPL-3.0**. The page gives no separate terms for the `assets` folder |
| CH340 driver | USB-UART driver for the on-board CH340C. Link only | <https://www.wch-ic.com/downloads/CH341SER_EXE.html> | WCH, proprietary, free download |
| Espressif ESP-IDF, Arduino-ESP32 and mbedTLS documentation | Reasoning in the docs about memory, PSRAM, RGB LCD, SD host, TLS. Links only, no text copied. See section 7 | <https://docs.espressif.com/> | Espressif documentation, linked not copied |

## 2. Reference code (git submodules in `reference/`)

Read-only prior art. Nothing is built from these, but ideas, timing values and pin knowledge were taken from them and are noted in the docs. Each is a shallow submodule pinned by commit.

| Repository | Author | What it taught us | Link | Licence |
|---|---|---|---|---|
| `esp32-4848s040-st7701` | aquaElectronics | ST7701 init table verified on hardware. The table in `common/lib/Board4848S040` is byte-identical to this one and to Arduino_GFX's `st7701_type9` | <https://github.com/aquaElectronics/esp32-4848s040-st7701> | MIT |
| `esp32-smartdisplay` | rzeldent | One LVGL abstraction for all Sunton boards. Architecture reference, board variants, GT911 address behaviour | <https://github.com/rzeldent/esp32-smartdisplay> | **GPL-3.0** |
| `ESP32-4848S040-LVGL-LGFX` | joevial | LovyanGFX and EEZ Studio workflow, backlight coil-whine fix and 100 kHz PWM, backlight-dimming mod, second I2C bus on GPIO1/40 | <https://github.com/joevial/ESP32-4848S040-LVGL-LGFX> | **No licence file** in the repository |
| `Guition-ESP32-S3-4848S040` | Alexey Titov (alaltitov) | ESPHome + LVGL 8.4 Home Assistant wall panel; screen-off / LVGL-pause pattern. Docs: <https://github.com/alaltitov/Guition-ESP32-S3-4848S040-DOCS> | <https://github.com/alaltitov/Guition-ESP32-S3-4848S040> | MIT |
| `ESP32-4848S040-First-Start` | abcbbck-png | ESP-IDF (CMake) starter: `sdkconfig.defaults`, partitions, `esp_lcd` set-up | <https://github.com/abcbbck-png/ESP32-4848S040-First-Start> | MIT |

Snippet kept in `reference/snippets/`:

| Item | Used for | Link | Licence |
|---|---|---|---|
| openHASP build config for this board (`openhasp-esp32-s3-4848S040.ini`) | Reference for a ready-made MQTT/Home Assistant touch-panel firmware | <https://github.com/HASwitchPlate/openHASP> | MIT |

Linked but not pulled in (no code taken):

| Project | Link |
|---|---|
| LovyanGFX | <https://github.com/lovyan03/LovyanGFX> |
| Espressif esp-bsp (`esp_lcd_st7701`, `esp_lcd_touch_gt911`, `esp_lvgl_port`) | <https://github.com/espressif/esp-bsp> |
| PrayerfulDrop / ESPHome-Guition-ESP32-S3-4848S040 | <https://github.com/PrayerfulDrop/ESPHome-Guition-ESP32-S3-4848S040> |
| kroon040 / HA-Panel-GUITION | <https://github.com/kroon040/HA-Panel-GUITION> |
| Witaliy76 / Yoradio_lvgl (audio over the NS4168) | <https://github.com/Witaliy76/Yoradio_lvgl> |
| APIUM / guition-4848-micropython-lvgl | <https://github.com/APIUM/guition-4848-micropython-lvgl> |
| ESPHome device page for this board | <https://devices.esphome.io/devices/guition-esp32-s3-4848s040> |
| ESPHome ST7701S display component | <https://esphome.io/components/display/st7701s/> |
| Espressif `esp_lcd_st7701` component | <https://components.espressif.com/components/espressif/esp_lcd_st7701> |

## 3. Board definitions

| Item | Used for | Link | Licence |
|---|---|---|---|
| `esp32-4848S040CIY1.json`, `esp32-4848S040CIY3.json` (rzeldent) | PlatformIO board JSONs in `common/boards/`. Copied from rzeldent's repository; flags and memory settings are the origin, the repo's own `[base_4848s040]` config sits on top | <https://github.com/rzeldent/platformio-espressif32-sunton> | **GPL-3.0** (repository licence) |
| ST7701 init table | `common/lib/Board4848S040` | aquaElectronics (section 2) and Arduino_GFX (section 4) | MIT / BSD-2-Clause |

## 4. Libraries and frameworks

| Name | Used for | Link | Licence |
|---|---|---|---|
| LVGL 9.5.0 | UI library, all screens | <https://lvgl.io/> / <https://github.com/lvgl/lvgl> | MIT |
| GFX Library for Arduino (Arduino_GFX) 1.6.9, by Moon On Our Nation | ST7701 / RGB panel driver, framebuffer in PSRAM | <https://github.com/moononournation/Arduino_GFX> | BSD 2-clause (licence file credits Adafruit Industries as original copyright holder) |
| ArduinoJson 7.4.3, by Benoit Blanchon | Parsing the Powerwall, Open-Meteo, MET Norway and NOAA JSON with filters | <https://arduinojson.org/> / <https://github.com/bblanchon/ArduinoJson> | MIT |
| Arduino-ESP32 core 3.2.1 (Espressif) | Arduino framework, Wi-Fi, SPI, LEDC, HTTP client | <https://github.com/espressif/arduino-esp32> | LGPL-2.1 (core). Some libraries carry Apache-2.0 headers |
| Patched `SD` library (`common/lib/SD`) | Arduino-ESP32 3.2.1's `SD` with a busy-wait fix and register readers. Derived from Espressif's `SD` and Neil Thiessen's SD/MMC File System Library | <https://github.com/espressif/arduino-esp32/tree/master/libraries/SD> | Apache-2.0 (file headers). Changes listed in `common/lib/SD/README-mini-screen.md` |
| ESP-IDF 5.4 (Espressif) | FreeRTOS, esp_lcd RGB panel, TLS stack, FATFS | <https://github.com/espressif/esp-idf> | Apache-2.0 |
| mbedTLS (bundled with ESP-IDF) | TLS for the HTTPS fetches | <https://github.com/Mbed-TLS/mbedtls> | Apache-2.0 or GPL-2.0-or-later (dual) |
| TJpgDec R0.03, by ChaN (bundled in LVGL, `LV_USE_TJPGD`) | Decoding the SOHO sun JPEG | <https://elm-chan.org/fsw/tjpgd/00index_e.html> | ChaN's permissive licence: free for personal and commercial use, source copyright notice must be kept |
| pioarduino `platform-espressif32` 54.03.21-2 | PlatformIO platform (Arduino core 3.2.1 on ESP-IDF 5.4) | <https://github.com/pioarduino/platform-espressif32> | Apache-2.0 |
| Board4848S040 and SdStore (`common/lib/`) | This repo's own libraries | this repository | Same licence as the rest of the repo |

## 5. Fonts and icons

| Name | Used for | Link | Licence |
|---|---|---|---|
| Montserrat (built into LVGL as `lv_font_montserrat_*`, by Julieta Ulanovsky and contributors) | All text on screen. ASCII plus the degree sign, hence "/" instead of a middle dot | <https://github.com/JulietaUla/Montserrat> | SIL Open Font Licence 1.1 |
| Font Awesome 5 glyphs (merged into LVGL's built-in Montserrat sizes, `LV_SYMBOL_*`) | Symbols in the UI | <https://fontawesome.com/v5/> | Fonts: SIL OFL 1.1. Icons: CC BY 4.0 |
| Weather and UI icons in `icons.cpp` | Drawn in code | this repository | Same licence as the rest of the repo |

## 6. Data sources (project 05 fetches these on the device)

All requests send an identifying User-Agent (`powerwall-display/1.0 ...`) and are cached or rate-limited. Every call is made over HTTPS unless noted.

| Name | Used for | Link | Licence / terms |
|---|---|---|---|
| Tesla Powerwall gateway local API (unofficial) | Site, battery and load power, state of charge, grid status, from the owner's own gateway on the LAN | No official documentation. Endpoints were found on the owner's own gateway | Not a published API. Tesla gives no licence or support for it. Not affiliated with or endorsed by Tesla. "Tesla" and "Powerwall" are Tesla's trademarks. Credentials stay on the device (`secrets.h`, not in the repo) |
| Open-Meteo | Weather forecast for the weather screen, every 15 minutes | <https://open-meteo.com/> (terms: <https://open-meteo.com/en/terms>, licence: <https://open-meteo.com/en/license>) | Data **CC BY 4.0**. Free API for non-commercial use only (under 10,000 calls a day). Attribution required |
| AuroraWatch UK, Space and Plasma Physics group, Department of Physics, Lancaster University | Aurora alert level and 24 hour activity for the aurora screen (plain HTTP API) | <https://aurorawatch.lancs.ac.uk/> (API terms: <http://aurorawatch.lancs.ac.uk/api-info/>) | Data **CC BY-NC-SA 3.0**. API **non-commercial only**. Must use the official alert names and colours, must not change thresholds, must not imply endorsement or call the app "official", must identify the client and wait at least 3 minutes between requests, and must show that the status is generated by AuroraWatch UK. The credit shown on the device is: "Aurora alert status generated by AuroraWatch UK, Space and Plasma Physics group, Department of Physics, Lancaster University, UK (CC BY-NC-SA 3.0)" |
| NOAA Space Weather Prediction Center (SWPC) | Planetary Kp (observed and 3-day forecast), flare class, solar wind, scales, flare and sunspot numbers (`services.swpc.noaa.gov`) | <https://www.swpc.noaa.gov/> (JSON services: <https://services.swpc.noaa.gov/>) | US government work, public domain. Credit as "NOAA SWPC". Do not imply endorsement or present altered data as official (<https://www.weather.gov/disclaimer>) |
| NOAA Climate Prediction Center (CPC) | El Nino / ENSO status, outlook odds and SST indices (ONI, RONI, weekly Nino regions), from `www.cpc.ncep.noaa.gov` | <https://www.cpc.ncep.noaa.gov/products/analysis_monitoring/enso_advisory/> | US government work, public domain. Credit as "NOAA CPC". Same disclaimer as above |
| SOHO (ESA and NASA), HMI intensitygram | Live sun photo on the sun screen (`soho.nascom.nasa.gov`, 512 px JPEG, decoded on the device) | <https://soho.nascom.nasa.gov/> | SOHO is a joint ESA and NASA mission. No licence or data policy is stated on the site's front page. Shown with the credit "SOHO (ESA & NASA)" |
| MET Norway (Norwegian Meteorological Institute), Sunrise API 3.0 | Moonrise and moonset times for the configured location (`api.met.no`) | <https://api.met.no/> (terms: <https://api.met.no/doc/TermsOfService>, licence: <https://docs.api.met.no/doc/License>) | **CC BY 4.0**. Requires credit, a licence link, an indication of changes and an identifying User-Agent |
| Jean Meeus, *Astronomical Algorithms*, 2nd edition (Willmann-Bell, 1998) | Moon phase, illuminated fraction, next full and new moon, and sun altitude, computed on the device (`moon_calc.cpp`) | ISBN 0-943396-61-1; publisher: <https://shopatsky.com/collections/willmann-bell> (Willmann-Bell's old site redirects there) | A published book. The formulae are re-implemented in original code; no text or tables copied. Cite when reusing |
| NASA Scientific Visualization Studio, "Moon Phase and Libration, 2020", visualization **#4768**, frame `phase_full.2347` | The full-moon photograph on the moon screen, shaded to the current phase on the device (a 180 x 180 RGB565 image in `moon_photo.c`) | Page: <https://svs.gsfc.nasa.gov/4768>. File: <https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004768/phase_full.2347.tif>. Policy: <https://svs.gsfc.nasa.gov/help/> | NASA SVS states that its content is public domain unless noted; the animation's music is not, and is not used. Credit: "NASA's Scientific Visualization Studio" (visualizer Ernie Wright, USRA; producer David Ladd, USRA; scientists Noah Petro and John Keller, NASA/GSFC; built from Lunar Reconnaissance Orbiter LROC WAC and LOLA data). The original image is not stored in the repo; `tools-moon/make_moon_photo.py` rebuilds the data |

Researched in [11 Space weather and climate data](../11-space-weather-and-climate-data.md) but **not used by the firmware**: NASA SDO, Helioviewer, NOAA PSL MEI, NCEI, NASA DONKI, Bureau of Meteorology ENSO. They are listed there with their findings.

## 7. Root certificates for TLS pinning

`src/ca_certs.h` pins root certificates, fetched from each organisation's own repository. Root certificates are public by design.

| Root | Used for | Link |
|---|---|---|
| ISRG Root X1 and ISRG Root YR (Internet Security Research Group, Let's Encrypt) | `api.open-meteo.com` and `soho.nascom.nasa.gov` | <https://letsencrypt.org/certificates/> |
| Amazon Root CA 1 (Amazon Trust Services) | `services.swpc.noaa.gov` | <https://www.amazontrust.com/repository/> |
| HARICA TLS RSA Root CA 2021 (Hellenic Academic and Research Institutions CA) | `api.met.no` | <https://repo.harica.gr/> (deep link to the certificate unverified) |
| Go Daddy Root Certificate Authority - G2 (GoDaddy) | `www.cpc.ncep.noaa.gov` | <https://certs.godaddy.com/repository> (the page lists "GoDaddy Class 2 Certification Authority Root Certificate - G2"; the exact name used in the pin was not shown there) |

## 8. Espressif documentation consulted

Links only, to the versions the repo targets (ESP-IDF 5.4 and the Arduino-ESP32 3.2.1 line).

| Topic | Link |
|---|---|
| RGB LCD (esp_lcd) | <https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html> |
| External RAM (PSRAM) | <https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/external-ram.html> |
| RAM usage and performance | <https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/performance/ram-usage.html> |
| mbedTLS in ESP-IDF | <https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/mbedtls.html> |
| SD SPI host and SDMMC host | <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/sdspi_host.html>, <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/sdmmc_host.html> |
| Arduino-ESP32 as an IDF component and lib-builder | <https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html>, <https://docs.espressif.com/projects/arduino-esp32/en/latest/lib_builder.html> |
| ESP FAQ: LCD and mbedTLS | <https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html>, <https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/mbedtls.html> |
| Memory analysis case study | <https://developer.espressif.com/blog/esp32-memory-analysis-case-study/> |

Forum threads and issue reports that informed specific findings are linked where they are used in the other documents in `docs/`. They stay the property of their authors.

## 9. Build and analysis tools

| Name | Used for | Link | Licence |
|---|---|---|---|
| PlatformIO Core 6.1.x | Build, upload, monitor, library management | <https://platformio.org/> / <https://github.com/platformio/platformio-core> | Apache-2.0 |
| cppcheck | Static analysis through `pio check` | <https://cppcheck.sourceforge.io/> | GPL-3.0 (used as a tool; no code included) |
| clang-tidy (LLVM) | Static analysis via `tools/lint.ps1` | <https://clang.llvm.org/extra/clang-tidy/> | Apache-2.0 with LLVM exceptions |
| esptool (Espressif) | Flash backup, restore and upload | <https://github.com/espressif/esptool> | GPL-2.0-or-later (used as a tool) |
| Python 3, Pillow | Host helper scripts (`make_moon_photo.py`, `screendump.py`, `sd_serial.py`) | <https://www.python.org/>, <https://python-pillow.org/> | PSF licence; HPND (Pillow) |
