#pragma once
// Pin map for the Guition / Sunton ESP32-4848S040C_I
// ESP32-S3-WROOM-1U N16R8 (16 MB flash, 8 MB octal PSRAM, U.FL antenna)
// Authority: vendor schematic docs/vendor/schematic-1.png + schematic-2.png.
// Cross-checked with the vendor IO sheet, ESPHome, rzeldent and Arduino_GFX. See docs/02-pinout.md.

// ---- ST7701S panel: 3-wire SPI (init commands only) ----
#define B4848_LCD_CS 39
#define B4848_LCD_SCK 48 // shared with SD card CLK
#define B4848_LCD_SDA 47 // shared with SD card CMD/MOSI

// ---- ST7701S panel: 16-bit RGB565 parallel bus (pixel data) ----
#define B4848_LCD_DE 18
#define B4848_LCD_VSYNC 17
#define B4848_LCD_HSYNC 16
#define B4848_LCD_PCLK 21

#define B4848_LCD_R0 11
#define B4848_LCD_R1 12
#define B4848_LCD_R2 13
#define B4848_LCD_R3 14
#define B4848_LCD_R4 0 // GPIO0 = BOOT strapping pin

#define B4848_LCD_G0 8
#define B4848_LCD_G1 20
#define B4848_LCD_G2 3 // strapping pin
#define B4848_LCD_G3 46 // strapping pin
#define B4848_LCD_G4 9
#define B4848_LCD_G5 10

#define B4848_LCD_B0 4
#define B4848_LCD_B1 5
#define B4848_LCD_B2 6
#define B4848_LCD_B3 7
#define B4848_LCD_B4 15

#define B4848_LCD_BL 38 // EN of U5 (AP3031-compatible boost LED driver), active high
// Lowest duty (of 255) that keeps the driver on at 100 kHz PWM. Community-measured (~102, joevial), not
// verified here. The library treats it as a FRACTION of the period (MIN/255) at any frequency or resolution
// (see board4848::backlightMinDuty()), which is conservative if the real limit is an on-time. Override with
// -DB4848_LCD_BL_MIN=<n> once a sweep has measured it (docs/02-pinout.md, "Backlight dimming range").
#ifndef B4848_LCD_BL_MIN
#define B4848_LCD_BL_MIN 100
#endif
// Backlight PWM frequency (Hz) used until a project calls board4848::setBacklightFrequency(). 100 kHz is
// above hearing (no coil whine, verified on unit 1) but may compress the dimming range; do not change the
// default without a hardware sweep. A project can override it with -DB4848_LCD_BL_FREQ_HZ=<hz>.
#ifndef B4848_LCD_BL_FREQ_HZ
#define B4848_LCD_BL_FREQ_HZ 100000
#endif
// Upper limit for the LEDC duty resolution (bits). The library uses the highest resolution the frequency
// allows (f * 2^bits <= 40 MHz; IDF's auto clock refused 51.2 MHz), capped here: 8 bits at 100 kHz, 10 bits at 20-25 kHz.
#ifndef B4848_LCD_BL_MAX_RES_BITS
#define B4848_LCD_BL_MAX_RES_BITS 12
#endif

#define B4848_LCD_WIDTH 480
#define B4848_LCD_HEIGHT 480
// Bounce buffer (pixels) for the RGB panel: the LCD DMA reads from internal RAM that an ISR refills
// from the PSRAM framebuffer. Stops flicker when the CPU writes PSRAM while the panel scans out.
// Must divide 480*480. 0 disables. Costs 2 * px * 2 bytes of internal RAM.
#define B4848_LCD_BOUNCE_PX (B4848_LCD_WIDTH * 10)

// ---- GT911 capacitive touch (I2C). Not wired to the ESP32: RST (shared RC power-on reset) and INT (floats). ----
#define B4848_TOUCH_SDA 19
#define B4848_TOUCH_SCL 45 // strapping pin
#define B4848_TOUCH_ADDR 0x5D // 0x14 is also probed (datasheet alternate address)

// ---- Relays (relay variants only). Vendor labels L1/L2/L3. Shared with I2S audio, see docs. ----
#define B4848_RELAY1 40
#define B4848_RELAY2 2
#define B4848_RELAY3 1

// ---- I2S to NS4168 amp (only after moving 0R links R25-27 -> R21-23). Amp plays the RIGHT channel (CTRL ~ VDD). ----
#define B4848_I2S_BCLK 1
#define B4848_I2S_LRCLK 2
#define B4848_I2S_DOUT 40 // vendor sheet: "I2S-DIN" (amp input)

// ---- microSD (TF) slot, SPI mode only, no card-detect. Wiring per schematic; not yet tested. Init after the display. ----
#define B4848_SD_CS 42
#define B4848_SD_SCK 48
#define B4848_SD_MOSI 47
#define B4848_SD_MISO 41 // DAT0 (the vendor xlsx wrongly says D1)

// ---- UART0 via onboard CH340C (USB-C) and the P1 header ----
#define B4848_UART0_TX 43
#define B4848_UART0_RX 44
