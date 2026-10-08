# Pinout

The same pins are defined in code in [`common/lib/Board4848S040/src/Board4848S040_pins.h`](../common/lib/Board4848S040/src/Board4848S040_pins.h).

Confidence: ✅ = consistent across ESPHome, rzeldent board defs and a hardware-verified Arduino project. ⚠️ = commonly reported but not verified here.

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
| Backlight | 38 (active high, PWM) | ✅ |
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

## microSD: ⚠️ unverified
| Function | GPIO |
|---|---|
| CS | 42 |
| SCK | 48 (shared with LCD SPI) |
| MOSI | 47 (shared with LCD SPI) |
| MISO | 41 |

Check these against the vendor schematic before relying on them. LCD SPI is only used during init, so sharing the bus afterwards is fine.

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
