# mini-screen — ESP32-4848S040C_I workspace

Development workspace for the **Guition / Sunton ESP32-4848S040C_I**: an ESP32-S3 board with a 4" 480×480 IPS capacitive touch screen in an 86 mm wall-box housing.

> The sticker may read something like `ESP32_3838S040C_I`. The PCB silkscreen and every reference source call this 480×480 board the **ESP32-4848S040**. The `4848` means 480×480 and `040` means 4.0".

## Quick facts

| | |
|---|---|
| MCU | ESP32-S3-WROOM-1 **N16R8**: dual-core 240 MHz, 16 MB flash, 8 MB octal PSRAM, Wi-Fi + BLE 5 |
| Display | 4.0" IPS 480×480, **ST7701S** driver, 16-bit RGB parallel interface plus 3-wire SPI for init |
| Touch | **GT911** capacitive, I2C (SDA 19, SCL 45), address 0x5D |
| USB | USB-C through a **CH340C** USB-UART bridge with auto-reset. Flash and monitor over USB-C. |
| Other | microSD slot, NS4168 I2S amp + speaker connector, BOOT and RST buttons, UART header, optional 1–3 relays and mains PSU |

## Repository layout

```
mini-screen/
├── README.md                ← you are here (master index)
├── docs/                    ← hardware notes, pinout, how-tos, datasheets
│   └── README.md            ← docs index
├── common/lib/Board4848S040 ← shared library: pin map, ST7701 init, GT911 driver
├── projects/                ← one folder per project (PlatformIO)
│   └── README.md            ← projects index
└── reference/               ← third-party repos (git submodules, read-only)
    └── README.md
```

## Index

### Docs
| Doc | What's in it |
|---|---|
| [docs/01-hardware-overview.md](docs/01-hardware-overview.md) | Specs, variants, connectors, board photos |
| [docs/02-pinout.md](docs/02-pinout.md) | Full GPIO map and which pins are free |
| [docs/03-programming-and-flashing.md](docs/03-programming-and-flashing.md) | USB-C/CH340, drivers, boot mode, recovery |
| [docs/04-display-st7701.md](docs/04-display-st7701.md) | RGB timings, init sequence, rotation, colour |
| [docs/05-touch-gt911.md](docs/05-touch-gt911.md) | GT911 registers, address, polling |
| [docs/06-frameworks-and-libraries.md](docs/06-frameworks-and-libraries.md) | PlatformIO/Arduino, ESP-IDF, ESPHome, LVGL, SquareLine |
| [docs/07-gotchas.md](docs/07-gotchas.md) | Known pitfalls |
| [docs/datasheets/](docs/datasheets/) | ST7701S, GT911, NS4168, ESP32-S3, WROOM-1 PDFs |

### Projects
| # | Project | Status | Description |
|---|---|---|---|
| 01 | [hello-display-touch](projects/01-hello-display-touch/) | Builds ✅ (not yet run on hardware) | Colour test plus finger-paint, prints touch coordinates |

See [projects/README.md](projects/README.md) for how to add a new project.

## Getting started (60 seconds)

1. Install VS Code and the **PlatformIO IDE** extension. PlatformIO is already present on this PC at `~/.platformio`.
2. Plug the board into USB-C. It should appear as **USB-SERIAL CH340 (COMx)** in Device Manager. If it doesn't, see [docs/03](docs/03-programming-and-flashing.md).
3. Open `projects/01-hello-display-touch` in VS Code, then click **Upload** and then **Monitor**.

Or from a terminal:
```sh
cd projects/01-hello-display-touch
~/.platformio/penv/Scripts/pio run -t upload
~/.platformio/penv/Scripts/pio device monitor
```

## Cloning on another machine
```sh
git clone --recurse-submodules <url>
# or, after a plain clone:
git submodule update --init
```
