# Pinout

The same pins are defined in code in [`common/lib/Board4848S040/src/Board4848S040_pins.h`](../common/lib/Board4848S040/src/Board4848S040_pins.h).

Confidence: ✅ = matches the **vendor IO sheet** ([`vendor/io-pin-distribution.xlsx`](vendor/io-pin-distribution.xlsx)) and the community sources (ESPHome, rzeldent, Arduino_GFX).

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

## microSD ✅ (vendor sheet)
| Function | GPIO | Vendor label |
|---|---|---|
| CS | 42 | TF(D3) (D3 = CS in SPI mode) |
| MISO | 41 | TF(D1) |
| SCK | 48 | TF(SCK). Shared with LCD SPI. |
| MOSI | 47 | TF(SDA). Shared with LCD SPI. |

LCD SPI is only used during the init sequence, so the SD card can use the bus afterwards (`SPI.begin(48, 41, 47, 42)`). The vendor's "D1" label is odd; this still needs a hardware test.

## I2S audio → NS4168 ✅ (vendor sheet)
| Function | GPIO |
|---|---|
| BCLK | 1 |
| LRCLK / WS | 2 |
| DATA (amp DIN) | 40 |

These are the same pins as relays L3/L2/L1. They are routed to the relays or the amp depending on the 0 Ω links (R25–27 → relays, R21–23 → amp).

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
