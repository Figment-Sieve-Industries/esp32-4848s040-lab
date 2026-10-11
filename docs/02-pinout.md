# Pinout

The same pins are defined in code in [`common/lib/Board4848S040/src/Board4848S040_pins.h`](../common/lib/Board4848S040/src/Board4848S040_pins.h).

**Sources:** unless a row says otherwise, every pin below matches the **vendor schematic** ([`vendor/schematic-1.png`](vendor/schematic-1.png), [`schematic-2.png`](vendor/schematic-2.png)), the vendor IO sheet, and the community configs (ESPHome, rzeldent, Arduino_GFX). Claims that aren't from the schematic are marked *(community)* or *(unverified)*.

"Wiring confirmed" is not the same as "works": nothing in this repo has been run on hardware yet.

## Display: ST7701S
| Function | GPIO |
|---|---|
| SPI CS | 39 |
| SPI SCK | 48 (shared with SD SCK) |
| SPI SDA (MOSI) | 47 (shared with SD MOSI) |
| DE | 18 |
| VSYNC | 17 |
| HSYNC | 16 |
| PCLK | 21 |
| R0–R4 | 11, 12, 13, 14, 0 (panel DB13–17) |
| G0–G5 | 8, 20, 3, 46, 9, 10 (panel DB6–11) |
| B0–B4 | 4, 5, 6, 7, 15 (panel DB1–5) |
| Backlight | 38, active high → U5 EN via R13 (R14 pull-down). PWM at 100 kHz by default (configurable, see "Backlight dimming range"). |
| LCD reset | Not connected to the ESP32. Shared RC power-on reset (R16 10k / C22 1 µF) with the GT911. |

Backlight minimum: the driver cuts out below roughly **100/255** at 100 kHz PWM *(community: joevial's `BL_EN_MIN 102`)*. On unit 1, **100 is confirmed still lit** and there is no whine at 100 kHz; the exact cut-out isn't measured yet ([hardware log](hardware-log.md)). An earlier figure of ~60 was measured at a lower PWM frequency.

## Touch: GT911
| Function | GPIO |
|---|---|
| SDA | 19 (4.7k pull-up R4) |
| SCL | 45 (4.7k pull-up R3; strapping pin, see below) |
| INT | Not connected to the ESP32. It runs only between the LCD FPC and the touch FPC, so it floats. |
| RST | Not connected to the ESP32. Same RC power-on reset as the LCD (net `LCD_RST`). |
| I2C address | 0x5D expected. See [05](05-touch-gt911.md) for the address rule; 0x14 is possible but unconfirmed on this board. |

## Relays (relay variants only)
| Relay | GPIO | 0 Ω link |
|---|---|---|
| 1 | 40 | R25 |
| 2 | 2 | R26 |
| 3 | 1 | R27 |

- **Default fitting is relays:** R25–R27 are fitted and R21–R23 are empty in the reference photo.
- **Polarity is unverified.** The relay driver sits on the daughterboard, which isn't in the schematic. Community ESPHome configs drive the relays `inverted`. Test on your unit.
- The relay pins **float until firmware drives them**, so set them to a safe level early in `setup()`.

## microSD
| Function | GPIO | Card pin |
|---|---|---|
| CS | 42 | CD/DAT3 (10k pull-up) |
| MOSI | 47 | CMD (shared with LCD SPI SDA) |
| SCK | 48 | CLK (shared with LCD SPI SCK) |
| MISO | 41 | DAT0 (the vendor xlsx says "D1", which is wrong) |

**Wiring confirmed by the schematic; function untested.**
- **SPI mode only.** DAT1 and DAT2 go only to pull-ups (RN1/R15), so 4-bit SD_MMC isn't possible.
- **No card-detect.** Software can't tell whether a card is inserted; try to mount it and handle the failure.
- LCD SPI is only used during the init sequence. **Initialise the display first**, then `SPI.begin(48, 41, 47, 42); SD.begin(42, SPI, 1000000);`.
- One project (Yoradio) reports that SD "did not work properly on this board". Start at a low SPI clock.

## I2S audio → NS4168
| Function | GPIO | 0 Ω link |
|---|---|---|
| BCLK | 1 | R23 |
| LRCLK / WS | 2 | R22 |
| DATA (amp DIN) | 40 | R21 |

- These are the same pins as relays 3/2/1. Each pin goes to either the relay header or the amp, depending on which 0 Ω link is fitted: R25–27 → relays (the default), R21–23 → amp. **To use audio, move the links.**
- The amp's **CTRL** pin (net `SD_MD`) is pulled to VOUT-BAT through R24 (1 MΩ) with no pull-down, so CTRL sits near VDD. Per the NS4168 datasheet (table 1) that means the amp is **always enabled and plays the RIGHT channel**. Send mono audio to both slots. There's no mute pin.

## UART0 / CH340C
TX = 43, RX = 44. These go to the USB-C bridge, the P1 header, the H1 header and P2.

## Strapping and special pins
- **GPIO0** (LCD red bit R4, BOOT button, R2 pull-up). Holding BOOT at reset enters download mode.
- **GPIO3, 46** are strapping pins used for LCD colour bits.
- **GPIO45** (touch SCL) is a strapping pin with an **external 4.7k pull-up**. On the ESP32-S3, GPIO45 = 1 at reset would select **1.8 V** VDD_SPI for the flash and PSRAM. The board only works because the module's eFuse (`VDD_SPI_FORCE`) forces 3.3 V. Don't add extra loads on the touch bus that could change the GPIO45 level at boot, and be careful if you swap the module. ESPHome needs `ignore_strapping_warning: true` on GPIO45.
- **GPIO19/20** are the native USB D−/D+. They are used for touch SDA and LCD G1, and the USB-C data lines go only to the CH340C, so **native USB is not available**.
- **GPIO35–37** are taken by octal PSRAM. Don't use them.

## Free GPIOs
Nearly every GPIO is used by the LCD, touch, SD, UART or PSRAM. The only realistic candidates:
- **1, 2, 40**: free if you don't use the relays or the audio (remove the 0 Ω links or leave the relay board unfitted)
- **41, 42**: free if you don't use the SD card
- **43, 44**: UART0. Usable via the P1 header if you give up the serial console.
- **9**: also exposed on P2, but it's LCD G4. Don't use it.

## Second I2C bus *(community: joevial)*
`Wire` (I2C_NUM_0) is the touch bus on 19/45. For your own sensors, create a second bus on **GPIO1 (SDA) / GPIO40 (SCL)**, using the relay pads (GPIO40 sits next to GND on H1):

```cpp
Wire1.begin(1, 40, 400000);   // I2C_NUM_1; Wire is the GT911 bus
```

This needs the relay/audio links removed, or no relay board. 3.3 V for sensors is available on test pad **S3** (S4 = GND). Alternatively, put extra I2C devices on the touch bus, as long as nothing clashes with 0x5D/0x14 and the GPIO45 warning above is respected.

## Backlight dimming range *(A and B implemented; the hardware sweep is still to do)*
On unit 1 the 05 brightness slider (duty 100-255 at 100 kHz) made very little visible difference (user report, 2026-10-11). Likely causes:
1. **Narrow range.** 100-255 is only 2.5x in duty, and perceived brightness is roughly logarithmic.
2. **PWM too fast for the driver.** U5 is dimmed by gating its EN pin. A boost driver needs some start-up time on each EN pulse, and at 100 kHz each period is only 10 µs, so low and mid duties are compressed. This also explains why the cut-out is ~100 at 100 kHz but ~60 at a lower frequency.

What is in the code now (nothing is flashed or measured yet; the default PWM frequency is still 100 kHz):
- **A. Configurable PWM** (board library). `B4848_LCD_BL_FREQ_HZ` (default 100000) is the start-up frequency; a project can override it with `-DB4848_LCD_BL_FREQ_HZ=<hz>`. `board4848::setBacklightFrequency(hz)` retunes at run time, picks the highest duty resolution that frequency allows (f x 2^bits <= 80 MHz, capped at 12 bits: **9 bits at 100 kHz, 11 bits at 20-25 kHz**) and rescales the current duty, so the brightness stays. `setBacklight(0..255)` keeps working (mapped onto the current resolution), plus `setBacklightLevel(0.0..1.0)`, `setBacklightDuty(raw)` and the getters `backlightFrequency/Resolution/Duty/DutyMax/MinDuty()`. `backlightMinDuty()` is `B4848_LCD_BL_MIN` (100/255) scaled to the current resolution and rounded up (201 at 9 bits, 803 at 11 bits). The same fraction is assumed at every frequency: that is safe if the real limit is a fraction of the period, and conservative if it is a minimum on-time. Re-measure it per frequency (below) and override `B4848_LCD_BL_MIN` once known.
- **B. Gamma slider** (project 05, `src/backlight_curve.h`). The slider is 0-100 % and the saved value is that percentage (not the duty). `duty = minDuty + round((maxDuty - minDuty) * (pct/100)^2.2)`. **0 % is the dimmest level that is still lit** (`minDuty`), never off; `b 0` over serial still switches the backlight off. With 9 bits (about 310 usable steps) the lowest ~5 slider positions round to the same duty; at 11 bits that flat zone shrinks to ~2. Old `settings.bin` files (raw duty, magic `SET1`) are converted on load to the percentage that gives the same duty; the file size is unchanged.

### Hardware sweep (the user does this; nothing here is measured yet)
Needs project 05 flashed and a serial monitor on its port (115200 baud, close the monitor before `pio upload`). All settings below are temporary (lost at reboot); the saved slider is not touched. Commands are in the project 05 README.

For each frequency in the order **100000, 50000, 25000, 20000, 5000**:
1. `bf <hz>`. The reply shows the resolution and the duty maximum (`br` reports it again). 5000 Hz is expected to be audible: it is there to see whether the dimming range gets better and how bad the whine is.
2. **Listen** with the panel at about 20 cm (quiet room, ear close to the top edge and the back where U5 and its inductor sit): `b 255`, then `bd <about 25 % of max>`, then `bd <about 50 %>`. Note whine, hiss or buzz at each. Note flicker too: look at the panel with the phone camera in slow motion or just by eye in the corner of your vision.
3. **Find the cut-out**: `bs 32 700` steps the duty from 0 to the maximum in 32 steps, 0.7 s apart, and prints `bs i/n duty D @ F Hz` for each step. Watch the panel (a dark room helps) and note the step number where it first lights, and the step where it is clearly at full brightness. Stop early with `bs 0`.
4. **Refine**: go to the duty just below the first lit step and walk up with `bd <duty>` (or run `bs 20 1000 <from> <to>` over that narrow window) until you find the lowest duty at which the panel is still lit. Record it, together with the maximum, as both a raw number and a fraction of the maximum.
5. **Judge the response**: with `bs 16 1000` look at how even the steps are. Many steps that look the same at the top, or a jump at the bottom, mean the range is compressed at that frequency.

Record per frequency: resolution, duty maximum, lowest duty still lit (raw and as a fraction), audible whine yes/no (how bad), visible flicker yes/no, evenness of the steps. Put the table in [hardware-log.md](hardware-log.md). Then:
- Pick the **lowest frequency with no audible whine and no visible flicker** and the most even response (probably 20-25 kHz).
- Set `-DB4848_LCD_BL_FREQ_HZ=<hz>` (or change the default in `Board4848S040_pins.h`, so all projects benefit) and `-DB4848_LCD_BL_MIN=<value as /255, rounded up>` from the measured fraction; keep a small safety margin.
- Update [Q-GFX-05](guide/01-quirks-and-gotchas.md#q-gfx-05) and [inv-brightness-range](guide/03-investigations.md#inv-brightness-range) with the numbers.

Only if the lowest usable brightness is still too bright: the hardware mod below.

## Backlight hardware mod *(community: joevial; untested)*
For deeper dimming, joevial changes R12 from 5.1 Ω to 33 Ω, adds about 80 nF from FB to GND, and wires FB to GPIO2 for a second, coarse brightness channel (GPIO38 stays the fine one). See `reference/ESP32-4848S040-LVGL-LGFX/README.md`. This conflicts with relay 2 / audio LRCLK on GPIO2.
