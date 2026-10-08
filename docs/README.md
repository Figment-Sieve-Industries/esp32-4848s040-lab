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

## Vendor data pack (`vendor/`)
From the official Guition/Jingcai (深圳市晶彩智能) data pack:
- Official link: http://pan.jczn1688.com/directlink/1/ESP32%20module/4.0inch_ESP32-4848S040.zip (91 MB, slow server; it opens a "GUITION-DISK" page)
- Partial mirror: [NorthernMan54/ESP32-4848S040](https://github.com/NorthernMan54/ESP32-4848S040/tree/main/ESP32-4848S040), with the vendor Arduino demo, factory "burn" images and the flash download tool

| File | What |
|---|---|
| `schematic-1.png` | **Schematic p1**: power, Li-ion charger, 3.3 V buck, USB-C + CH340C + auto-reset, backlight driver, TF card |
| `schematic-2.png` | **Schematic p2**: ESP32-S3 module, LCD FPC, touch FPC, NS4168 audio, relay header H1, P2 debug FPC |
| `io-pin-distribution.xlsx` | Official IO pin map (one label error: IO41 is DAT0/MISO, not D1) |
| `esp32-s3-wroom-1-pin-definition.png` | Module pinout |
| `specifications-EN.pdf` | Product specification |
| `getting-started-4.0inch.pdf` | Vendor user manual (Arduino IDE setup, demos) |
| `dimensions.jpg`, `structure.jpg` | Mechanical drawings |

The schematic is the authority for pins.
