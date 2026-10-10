# mini-screen: ESP32-4848S040C_I workspace

Personal development workspace for the **Guition / Sunton ESP32-4848S040C_I**: an ESP32-S3 board with a 4" 480×480 capacitive touch screen in an 86 mm wall-box housing. Local-only git repo.

> The sticker may read something like `ESP32_3838S040C_I`. The PCB silkscreen, the vendor schematic and every reference source call this 480×480 board the **ESP32-4848S040**. `4848` = 480×480, `040` = 4.0".

## Quick facts

| | |
|---|---|
| MCU | ESP32-S3-WROOM-**1U**-N16R8: dual-core 240 MHz, 16 MB flash, 8 MB octal PSRAM, Wi-Fi + BLE 5. **U.FL external antenna.** Don't run Wi-Fi with the antenna disconnected. |
| Display | 4.0" TFT (likely IPS), 480×480. **ST7701S** driver: 16-bit RGB parallel bus plus 3-wire SPI for init. |
| Touch | **GT911** capacitive, I2C (SDA 19, SCL 45), address 0x5D (or 0x14) |
| USB | USB-C through a **CH340C** USB-UART bridge with auto-reset. Flash and monitor over USB-C; no buttons needed. |
| Other | microSD (SPI), NS4168 I2S amp + speaker connector, Li-ion charger (IP5306-class), BOOT/RST buttons, UART header, optional 1 or 3 relays + mains PSU |

## Repository layout

```
mini-screen/
├── README.md                ← you are here (master index)
├── CLAUDE.md                ← rules for agents/contributors working in this repo
├── docs/                    ← hardware notes, pinout, how-tos, datasheets, vendor pack
│   └── README.md            ← docs index
├── common/
│   ├── platformio-base.ini  ← shared PlatformIO settings every project extends
│   ├── boards/              ← rzeldent board JSONs (esp32-4848S040CIY1/CIY3)
│   └── lib/Board4848S040    ← shared library: pin map, ST7701 init, GT911 touch, LVGL glue
├── projects/                ← one folder per project
│   └── README.md            ← projects index (status of every project)
├── tools/                   ← PowerShell helpers: build/flash/monitor, build-all, flash backup/restore
├── firmware/                ← factory firmware backups + vendor factory images (README)
└── reference/               ← prior-art repos (git submodules, read-only)
    └── README.md
```

## Getting started

Use **PowerShell** or VS Code's PlatformIO buttons. **Not Git Bash**: ESP's tool installer rejects MSYS shells and the first build fails (see [docs/07](docs/07-gotchas.md)).

0. **Back up the factory firmware first.** Plug the board into USB-C, then run `.\tools\backup-flash.ps1`. It saves the full 16 MB image to `firmware/factory/`. This takes a few minutes. Details: [docs/03](docs/03-programming-and-flashing.md).
1. Install VS Code and the **PlatformIO IDE** extension (or just PlatformIO Core).
2. Check that the board shows up as **USB-SERIAL CH340 (COMx)** in Device Manager. If it doesn't, see [docs/03](docs/03-programming-and-flashing.md).
3. Build, flash and monitor project 01:
   ```powershell
   .\tools\pio.ps1 -Project 01-hello-display-touch -Action all
   ```
   Or in VS Code: **File > Open Folder** on `projects\01-hello-display-touch`, then **Upload** and **Monitor**.

The first build downloads the toolchain and libraries and takes several minutes. Later builds take seconds.

## Index

### Docs
| Doc | What's in it |
|---|---|
| [docs/01-hardware-overview.md](docs/01-hardware-overview.md) | Specs, variants, components, connectors, board photos |
| [docs/02-pinout.md](docs/02-pinout.md) | Full GPIO map, strapping pins, free pins, second I2C bus |
| [docs/03-programming-and-flashing.md](docs/03-programming-and-flashing.md) | USB-C/CH340, drivers, boot mode, backup/restore |
| [docs/04-display-st7701.md](docs/04-display-st7701.md) | RGB timings and variants, init sequence, rotation |
| [docs/05-touch-gt911.md](docs/05-touch-gt911.md) | GT911 registers, address, polling, rotation |
| [docs/06-frameworks-and-libraries.md](docs/06-frameworks-and-libraries.md) | Arduino vs ESP-IDF trade-offs; PlatformIO, LVGL, ESPHome |
| [docs/07-gotchas.md](docs/07-gotchas.md) | Known pitfalls and fixes |
| [docs/08-power-battery-sleep.md](docs/08-power-battery-sleep.md) | Power tree, battery charger, sleep and wake options |
| [docs/hardware-log.md](docs/hardware-log.md) | **What has been verified on your board** |
| [docs/datasheets/](docs/datasheets/) | ST7701S, GT911, NS4168, ESP32-S3, WROOM-1 PDFs |
| [docs/vendor/](docs/vendor/) | Guition data pack: **schematic**, IO pin sheet, spec, user manual, dimensions |

### Projects
See **[projects/README.md](projects/README.md)** for the list of projects, their status and how to add a new one.

### Reference
See **[reference/README.md](reference/README.md)** for the prior-art repos (esp32-smartdisplay, LovyanGFX + EEZ Studio, ESPHome HA panel, ESP-IDF starter, aquaElectronics ST7701 init) and what to learn from each.

## Cloning
This repo is local only (no remote). When copying it elsewhere, fetch the submodules too:
```sh
git clone --recurse-submodules <path-or-url>
# or, after a plain clone:
git submodule update --init
```
