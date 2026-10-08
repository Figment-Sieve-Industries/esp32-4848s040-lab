#pragma once
// Pin map for the Guition / Sunton ESP32-4848S040C_I
// ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM)
// Sources: ESPHome device page, rzeldent/platformio-espressif32-sunton, aquaElectronics/esp32-4848s040-st7701
// See docs/02-pinout.md for details and confidence notes.

// ---- ST7701S panel: 3-wire SPI (init commands only) ----
#define B4848_LCD_CS 39
#define B4848_LCD_SCK 48 // shared with SD card SCK (reported)
#define B4848_LCD_SDA 47 // shared with SD card MOSI (reported)

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

#define B4848_LCD_BL 38 // backlight (AP3031 driver), active high. Dims poorly below ~60/255
#define B4848_LCD_BL_MIN 60

#define B4848_LCD_WIDTH 480
#define B4848_LCD_HEIGHT 480

// ---- GT911 capacitive touch (I2C). RST and INT are not wired to the ESP32. ----
#define B4848_TOUCH_SDA 19
#define B4848_TOUCH_SCL 45 // strapping pin
#define B4848_TOUCH_ADDR 0x5D // some units answer on 0x14 instead

// ---- Relays (relay variants only). Vendor labels L1/L2/L3. Shared with I2S audio, see docs. ----
#define B4848_RELAY1 40
#define B4848_RELAY2 2
#define B4848_RELAY3 1

// ---- I2S to NS4168 amp (only after moving 0R links R25-27 -> R21-23) ----
#define B4848_I2S_BCLK 1
#define B4848_I2S_LRCLK 2
#define B4848_I2S_DOUT 40 // vendor sheet: "I2S-DIN" (amp input)

// ---- microSD (TF) slot, SPI mode. Confirmed by vendor IO sheet (docs/vendor/io-pin-distribution.xlsx) ----
#define B4848_SD_CS 42
#define B4848_SD_SCK 48
#define B4848_SD_MOSI 47
#define B4848_SD_MISO 41

// ---- UART0 via onboard CH340C (USB-C) and the P1 header ----
#define B4848_UART0_TX 43
#define B4848_UART0_RX 44
