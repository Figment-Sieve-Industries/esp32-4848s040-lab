# Docs index

| File | Topic |
|---|---|
| [01-hardware-overview.md](01-hardware-overview.md) | What's on the board, variants, connectors, photos |
| [02-pinout.md](02-pinout.md) | GPIO map, strapping pins, free pins, second I2C bus |
| [03-programming-and-flashing.md](03-programming-and-flashing.md) | USB-C / CH340C, drivers, auto-reset, manual boot mode, backup/restore/erase |
| [04-display-st7701.md](04-display-st7701.md) | ST7701S bring-up: 3-wire SPI init plus RGB timings and variants |
| [05-touch-gt911.md](05-touch-gt911.md) | GT911 over I2C |
| [06-frameworks-and-libraries.md](06-frameworks-and-libraries.md) | Arduino vs ESP-IDF trade-offs; toolchains and libraries |
| [07-gotchas.md](07-gotchas.md) | Pitfalls and fixes |
| [08-power-battery-sleep.md](08-power-battery-sleep.md) | Power tree, Li-ion charger, sleep and wake |
| [09-alternative-displays.md](09-alternative-displays.md) | Boards similar to the 4848S040, with UK buying options |
| [10-esp32-p4-displays.md](10-esp32-p4-displays.md) | ESP32-P4 display boards (UK buying options) |
| [11-space-weather-and-climate-data.md](11-space-weather-and-climate-data.md) | Space weather and ENSO data sources (research) |
| [12-esp32-performance-and-framework.md](12-esp32-performance-and-framework.md) | ESP32-S3 performance, memory and framework choice |
| [getting-started-powerwall.md](getting-started-powerwall.md) | Step-by-step guide to the Powerwall 3 display (project 05) |
| [guide/](guide/README.md) | The development guide: quirks, debugging playbook, investigations, ADRs |
| [flash/](flash/README.md) | The browser flasher page (GitHub Pages) |
| [hardware-log.md](hardware-log.md) | What has been verified on real units (chip, flash, PSRAM, display, touch) |

## Datasheets (`datasheets/`)
| File | Part |
|---|---|
| `ST7701S.pdf` | Display driver IC |
| `GT911.pdf` | Touch controller |
| `NS4168.pdf` | I2S mono class-D audio amp |
| `ESP32-S3.pdf` | ESP32-S3 SoC datasheet |
| `esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf` | WROOM-1 / WROOM-1U module datasheet |

Source: [rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton) `assets/`. These are copied here on purpose, so the docs work without fetching submodules.

## Images (`images/`)
- `esp32-4848S040C-front.png`
- `esp32-4848S040C-back.jpg`: back of the PCB with labelled connectors (CH340C, TF slot, NS4168, P1 UART, H1 header, BAT, BOOT/RST)

Source: rzeldent's `assets/images`. These are photos of *a* 4848S040, which may differ in detail from your unit (for example the module marking or which 0 Ω links are fitted).

## Vendor data pack (`vendor/`)
From the official Guition/Jingcai (深圳市晶彩智能) data pack:
- Official link: http://pan.jczn1688.com/directlink/1/ESP32%20module/4.0inch_ESP32-4848S040.zip (91 MB, slow server; it opens a "GUITION-DISK" page). Last checked 2026-10-08.
- Partial mirror: [NorthernMan54/ESP32-4848S040](https://github.com/NorthernMan54/ESP32-4848S040/tree/main/ESP32-4848S040), with the vendor Arduino demo, factory "burn" images and the flash download tool. The factory images are covered in [`../firmware/README.md`](../firmware/README.md).

| File | What |
|---|---|
| `schematic-1.png` | **Schematic p1**: power input (P1, Q1), 3.3 V buck, USB-C + CH340C + auto-reset (T1/T2), backlight LED driver (U5), TF card |
| `schematic-2.png` | **Schematic p2**: ESP32-S3-WROOM-1U module, BOOT/RST, LCD FPC, touch FPC, Li-ion charger (U1), NS4168 audio (U4), relay header H1, P2 debug connector |
| `io-pin-distribution.xlsx` | Official IO pin map (one label error: IO41 is DAT0/MISO, not D1) |
| `esp32-s3-wroom-1-pin-definition.png` | Module pinout |
| `specifications-EN.pdf` | Product specification (defines the `_Y_1` / `_Y_3` relay SKUs) |
| `getting-started-4.0inch.pdf` | Vendor user manual (Arduino IDE setup, demos) |
| `dimensions.jpg`, `structure.jpg` | Mechanical drawings |

**The schematic is the authority for pins.**
