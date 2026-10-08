# Pinout

The same pins are defined in code in [`common/lib/Board4848S040/src/Board4848S040_pins.h`](../common/lib/Board4848S040/src/Board4848S040_pins.h).

Confidence: ✅ = matches the **vendor schematic** ([`vendor/schematic-1.png`](vendor/schematic-1.png), [`schematic-2.png`](vendor/schematic-2.png)), the vendor IO sheet and the community sources (ESPHome, rzeldent, Arduino_GFX).

## Display: ST7701S
| Function | GPIO | |
|---|---|---|
| SPI CS | 39 | ✅ |
| SPI SCK | 48 | ✅ |
| SPI SDA (MOSI) | 47 | ✅ |
| DE | 18 | ✅ |
| VSYNC | 17 | ✅ |
| HSYNC | 16 | ✅ |
| PCLK | 21 | ✅ |
| R0–R4 | 11, 12, 13, 14, 0 | ✅ |
| G0–G5 | 8, 20, 3, 46, 9, 10 | ✅ |
| B0–B4 | 4, 5, 6, 7, 15 | ✅ |
| Backlight | 38 (active high, PWM; AP3031 driver, usable range ~60–255) | ✅ |
| LCD reset | not connected | ✅ |

## Touch: GT911
| Function | GPIO | |
|---|---|---|
| SDA | 19 | ✅ |
| SCL | 45 | ✅ (strapping pin) |
| INT | not connected | ✅ |
| RST | not connected | ✅ |
| I2C address | 0x5D (some units 0x14) | ✅ |

## Relays (relay variants only)
| Relay | GPIO |
|---|---|
| 1 | 40 |
| 2 | 2 |
| 3 | 1 |

ESPHome drives these **inverted**. IO1, IO2 and IO40 are shared with the NS4168 I2S amp. To use audio instead of relays, move the 0 Ω resistors R25/R26/R27 to R21/R22/R23 (per rzeldent).

## microSD ✅ (vendor schematic)
| Function | GPIO | Card pin |
|---|---|---|
| CS | 42 | CD/DAT3 (10k pull-up) |
| MOSI | 47 | CMD. Shared with LCD SPI SDA. |
| SCK | 48 | CLK. Shared with LCD SPI SCK. |
| MISO | 41 | DAT0 (the xlsx says "D1", which is wrong) |

- **SPI mode only.** DAT1 and DAT2 go only to pull-ups, so 4-bit SD_MMC isn't possible.
- **No card-detect.** Software can't tell whether a card is inserted; try to mount it and handle the failure.
- LCD SPI is only used during init. Initialise the display first, then `SPI.begin(48, 41, 47, 42); SD.begin(42);`.
- One project (Yoradio) reports that SD "did not work properly on this board". This still needs a hardware test.

## I2S audio → NS4168 ✅ (vendor schematic)
| Function | GPIO |
|---|---|
| BCLK | 1 |
| LRCLK / WS | 2 |
| DATA (amp DIN) | 40 |

These are the same pins as relays 3/2/1. Each pin goes through a 0 Ω link to either the relay header or the amp: IO40 via R25 (relay1) or R21 (DIN); IO2 via R26 (relay2) or R22 (LRCLK); IO1 via R27 (relay3) or R23 (BCLK). The amp's SD_MODE is pulled up to VOUT-BAT through 1 MΩ, so it is always enabled with no mute pin.

## UART0 / CH340C
TX = 43, RX = 44. These go to the USB-C bridge and the P1 header.

## Strapping and special pins
- **GPIO0** (R4 colour bit, BOOT button). Holding BOOT at reset enters the download mode.
- **GPIO3, 45, 46** are strapping pins used here for the LCD/touch. ESPHome needs `ignore_strapping_warning: true` on GPIO45.
- **GPIO19/20** are the native USB D−/D+. They are used for touch SDA and LCD G1, so **native USB is not available**. Use the CH340C.
- **GPIO35–37** are taken by octal PSRAM. Don't use them.

## Free GPIOs
Very few. Every pin from 0 to 21 plus 38–40, 45, 46 and 47/48 is used. Candidates:
- **1, 2, 40**: free on non-relay units (or if you remove the relay/audio jumpers)
- **41, 42**: free if you don't use the SD card
- **43, 44**: UART0. Usable via the P1 header if you give up the serial console.

For extra I/O, consider an I2C expander on the touch I2C bus (SDA 19 / SCL 45), but check the GT911 address doesn't clash.
