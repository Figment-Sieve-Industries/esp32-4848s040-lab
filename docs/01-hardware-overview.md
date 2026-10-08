# Hardware overview: ESP32-4848S040C_I

Maker: Guition (sold under the Sunton/JCZN name). The silkscreen reads `ESP32-4848S040`, and the PCB shown is V1.0 from late 2023.

## Naming
`ESP32-4848S040C_I`
- `4848`: 480×480 resolution
- `S`: ESP32-**S**3
- `040`: 4.0" diagonal
- `C`: **C**apacitive touch (N = no touch, R = resistive)
- `_I`: not defined in any vendor document. Most likely **IPS** panel: the vendor demo's panel file is `ST7701S_HSD3.95IPS(HSD040BPN1)`, and the sibling `8048S043C_I` is sold as the IPS version. Every SKU in the data pack is `_I`.
- `_Y_1` / `_Y_3`: relay versions with **1 or 3 relays** (from the vendor spec PDF). rzeldent calls them `CIY1` / `CIY3`. No `_Y` suffix = no relay board.

## Main components
| Component | Part | Notes |
|---|---|---|
| Module | ESP32-S3-WROOM-1 **N16R8** | 16 MB QIO flash, 8 MB **octal** PSRAM. Use `memory_type = qio_opi`. |
| LCD | 4.0" IPS 480×480 | ST7701S, RGB565 parallel bus plus 3-wire SPI for config |
| Touch | GT911 (on the FPC) | I2C. RST and INT are not routed to the ESP32. |
| USB-UART | **CH340C** | USB-C to UART0 (GPIO43/44). T1/T2 transistors give auto-reset and boot. |
| Audio | NS4168 | I2S class-D mono amp, `Speak` JST connector (P7). Its pins are shared with the relays. |
| Storage | microSD (TF1) | SPI |
| Battery | U1 Li-ion charger + boost (power-bank style IC) | Charges a 1S Li-ion/LiPo on P6 `BAT` from 5 V, and boosts it to `VOUT-BAT`, which feeds the 3.3 V buck, the backlight driver and the amp. SW1 is its KEY (power button). |
| Backlight driver | U5, AP3031 boost LED driver | EN = IO38 (`BL_CTR`). Dims poorly below ~60/255. |
| Power | 5 V USB-C, `+5V` on P1/H1, or battery | Relay variants also have an onboard 110–240 V AC PSU. |

## Connectors (from the back-side photo)
| Ref | What | Pins |
|---|---|---|
| USB1 | USB-C | Power plus CH340C serial |
| P1 | HC-1.25 4-pin | VIN (5 V in, via Q1 reverse protection), TXD0, RXD0, GND |
| H1 | 2×4 header | GND, relay1/2/3 (IO40/2/1), U0RXD, U0TXD, 5V, to the relay/PSU daughterboard |
| P6 `BAT` | JST 1.25 2-pin | Li-ion cell B+ / B− (charged by U1) |
| P7 `Speak` | JST 1.25 2-pin | Speaker out from NS4168 (bridge-tied, don't ground either side) |
| SW1 | small switch | KEY input of the battery IC U1 (power on/off) |
| FPC1 | 6-pin 1.0 mm FPC | Touch: SDA=IO19, SCL=IO45 (4.7k pull-ups), INT and RST not wired to the ESP32 |
| LCD1 | 40-pin FPC | LCD panel |
| P2 | 6-pin FPC | 3.3V, GND, RST(EN), IO9, U0TXD, U0RXD. Programming/debug port. IO9 is also LCD G4. |
| BOOT / RST | Buttons | BOOT = GPIO0, RST = EN. The CH340C DTR/RTS drive them through T1/T2 for auto-reset. |

The full schematic is in [`vendor/schematic-1.png`](vendor/schematic-1.png) (power, battery, USB-UART, backlight, SD) and [`vendor/schematic-2.png`](vendor/schematic-2.png) (ESP32, LCD, touch, audio, relays).

## Photos
![front](images/esp32-4848S040C-front.png)
![back](images/esp32-4848S040C-back.jpg)

## Mains warning
On relay variants the PSU and relay board carry **mains voltage**. Don't have mains connected while USB is plugged in and the case is open.
