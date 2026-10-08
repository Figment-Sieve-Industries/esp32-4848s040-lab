# Docs index

| File | Topic |
|---|---|
| [01-hardware-overview.md](01-hardware-overview.md) | What's on the board, variants, connectors, photos |
| [02-pinout.md](02-pinout.md) | GPIO map, strapping pins, free pins |
| [03-programming-and-flashing.md](03-programming-and-flashing.md) | USB-C / CH340C, drivers, auto-reset, manual boot mode, erase/backup |
| [04-display-st7701.md](04-display-st7701.md) | ST7701S bring-up: 3-wire SPI init plus RGB timings |
| [05-touch-gt911.md](05-touch-gt911.md) | GT911 over I2C |
| [06-frameworks-and-libraries.md](06-frameworks-and-libraries.md) | Toolchains and libraries that support this board |
| [07-gotchas.md](07-gotchas.md) | Pitfalls and fixes |

## Datasheets (`datasheets/`)
| File | Part |
|---|---|
| `ST7701S.pdf` | Display driver IC |
| `GT911.pdf` | Touch controller |
| `NS4168.pdf` | I2S mono class-D audio amp |
| `ESP32-S3.pdf` | ESP32-S3 SoC datasheet |
| `esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf` | WROOM-1 module datasheet |

Source: [rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton) `assets/`.

## Images (`images/`)
- `esp32-4848S040C-front.png`
- `esp32-4848S040C-back.jpg`: back of the PCB with labelled connectors (CH340C, TF slot, NS4168, P1 UART, H1 header, BAT, BOOT/RST)

## Vendor material not in this repo
Guition/JCZN distribute a zip "data pack" (schematic, demo code, SquareLine/LVGL examples) through the seller. Ask the AliExpress seller for the **ESP32-4848S040** download link. When you get it, unpack it into `docs/vendor/` and add an entry here. **The schematic is the authority for the SD card pins**, which are still unverified.
