# Hardware overview: ESP32-4848S040C_I

Maker: Guition / Shenzhen Jingcai Intelligent (sold under the Sunton/JCZN names). The silkscreen reads `ESP32-4848S040`, and the PCB in the reference photo is V1.0 from late 2023.

## Naming
`ESP32-4848S040C_I`
- `4848`: 480×480 resolution
- `S`: ESP32-**S**3
- `040`: 4.0" diagonal
- `C`: **C**apacitive touch (N = no touch, R = resistive)
- `_I`: not defined in any vendor document. Most likely **IPS** panel: the vendor demo's panel file is `ST7701S_HSD3.95IPS(HSD040BPN1)`, and the sibling `8048S043C_I` is sold as the IPS version. Every SKU in the data pack is `_I`. The vendor spec itself only says "TFT".
- `_Y_1` / `_Y_3`: relay versions with **1 or 3 relays** (from the vendor spec PDF). rzeldent calls them `CIY1` / `CIY3`. No `_Y` suffix = no relay board.

## Main components
| Component | Part | Notes |
|---|---|---|
| Module | ESP32-S3-WROOM-**1U**-N16R8 | Per the schematic symbol. 16 MB QIO flash, 8 MB **octal** PSRAM; use `memory_type = qio_opi`. **U.FL (IPX) external antenna** with a pigtail; don't transmit with it disconnected. The can in the reference photo reads "ESP32-S3-WROOM … N16R8" with FCC ID 2BCLR-XN-S3E, which isn't Espressif's grantee code, so it may be a third-party module. rzeldent and the vendor spec just say "WROOM-1". |
| LCD | 4.0" TFT (likely IPS), 480×480 | ST7701S, RGB565 on a 16-bit parallel bus (the panel is 18-bit; DB0/DB12 not connected) plus 3-wire SPI for config |
| Touch | GT911 (on the touch flex) | I2C. RST and INT are not connected to the ESP32 (see [02](02-pinout.md)). |
| USB-UART | **CH340C** | USB-C to UART0 (GPIO43/44). T1/T2 transistors give auto-reset and boot. |
| Audio | NS4168 (U4) | I2S class-D mono amp, `Speak` connector (P7). Its I2S pins are shared with the relays via 0 Ω links. Always enabled. |
| Storage | microSD (TF1) | SPI mode only, no card-detect |
| Battery | U1, **IP5306-class** power-bank IC (photo marking "IP53…") | Charges a 1S Li-ion/LiPo on P6 `BAT` from 5 V and boosts it to `VOUT-BAT`. SW1 is its KEY input. See [08](08-power-battery-sleep.md). |
| Backlight driver | U5, **AP3031-compatible** boost LED driver | Not named in the schematic; ID from the joevial repo, and the marking varies between units. EN = IO38 (`BL_CTR`). |
| Power | 5 V USB-C, VIN on P1, `5V` on H1, or battery | Relay variants also have an onboard 110–240 V AC PSU on the daughterboard. |

## Connectors
| Ref | What | Pins |
|---|---|---|
| USB1 | USB-C | Power plus CH340C serial |
| P1 | HC-1.25 (1.25 mm) 4-pin | 1 VIN, 2 TXD0, 3 RXD0, 4 GND. VIN goes through Q1 (AO3401), which gives reverse-polarity protection only. With USB plugged in, **5 V appears on P1 pin 1** (back-fed). |
| H1 | 2×4 header | 1/2 GND, 3/5/7 relay1/2/3 (IO40/IO2/IO1), 4 U0RXD, 6 U0TXD, 8 5V. Goes to the relay/PSU daughterboard. |
| P6 `BAT` | JST 1.25 2-pin | Li-ion cell B+ / B− (charged by U1) |
| P7 `Speak` | JST 1.25 2-pin | Speaker out from NS4168. Bridge-tied output: don't ground either side. |
| SW1 | small switch | KEY input of the battery IC U1 (via R29) |
| FPC1 | 6-pin 1.0 mm FPC | Touch: SDA=IO19, SCL=IO45 (4.7k pull-ups R3/R4); RST on the shared RC reset; INT not wired to the ESP32 |
| LCD1 | 40-pin FPC | LCD panel |
| P2 | 6-pin connector (small SMD/FPC-style footprint) | 6 3.3V, 5 GND, 4 RST(EN), 3 IO9, 2 U0TXD, 1 U0RXD. Usable for serial flashing, but it has no IO0, so **hold BOOT** while resetting. IO9 is also LCD G4. |
| BOOT / RST | Buttons | BOOT = GPIO0 (R2 10k pull-up), RST = EN. The CH340C DTR/RTS drive both through T1/T2 for auto-reset. |

The full schematic:
- [`vendor/schematic-1.png`](vendor/schematic-1.png): power input, 3.3 V buck, USB-UART + auto-reset, backlight driver, SD card
- [`vendor/schematic-2.png`](vendor/schematic-2.png): ESP32 module, LCD, touch, Li-ion charger, audio, relay header, P2

## Photos
These are rzeldent's photos and may not match your exact unit.

![front](images/esp32-4848S040C-front.png)
![back](images/esp32-4848S040C-back.jpg)

## Mains warning
On relay variants the PSU and relay board carry **mains voltage**. Don't have mains connected while USB is plugged in and the case is open.
