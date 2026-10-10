# Gotchas

| Symptom | Cause / fix |
|---|---|
| `idf_tools.py installation failed`, then `xtensa-esp32s3-elf-g++ is not recognized` | The first build was run from **Git Bash/MSYS**, which ESP's tool installer rejects. Run `pio` from PowerShell (`.\tools\pio.ps1`), cmd, or the VS Code PlatformIO buttons. |
| `IncompatiblePlatform ... depends on PlatformIO Core >=6.2.0` | `common/platformio-base.ini` pins pioarduino `54.03.21-2`, which works with Core 6.1.x. To use newer releases, run `pio upgrade` first. |
| Nothing on `Serial` | `ARDUINO_USB_CDC_ON_BOOT` must be **0**. Native USB isn't wired; the console is on the CH340. |
| Boot loop or `PSRAM init failed` | PSRAM is **octal**: use `memory_type = qio_opi` (PIO) or "OPI PSRAM" (Arduino IDE). `initDisplay()` returns nullptr if PSRAM is missing. |
| Black screen, backlight on | The init sequence wasn't accepted (wrong table or SPI pins), or the RGB timing is off. See [04](04-display-st7701.md). |
| Black screen, backlight off | GPIO38 isn't driven. Call `board4848::setBacklight(255)`. |
| Screen flickers while drawing (e.g. finger-paint) | PSRAM bandwidth: CPU framebuffer writes starve the LCD DMA. **Fixed** by the bounce buffers `initDisplay()` now enables by default; verified on hardware. |
| Image shifted, rolling or shimmering | RGB porch/PCLK timing; try the variants table in [04](04-display-st7701.md). Wi-Fi + PSRAM contention: bounce buffers or a lower PCLK. |
| Colours swapped or wrong | RGB vs BGR order, or the wrong data-pin order. Use the pin map in `Board4848S040_pins.h` exactly. |
| Touch not detected | `begin()` probes 0x5D and 0x14 and checks the "911" ID. Check SDA 19 / SCL 45. INT/RST aren't wired, so the address can't be forced. |
| Touch axes wrong after rotating | Call `touch.setRotation(r)` with the same value as `initDisplay(r)` (see [05](05-touch-gt911.md)). |
| Taps feel laggy on release | Fixed: `Touch::read()` returns 0 for a genuine release and -1 for "no new data". Code that does `if (touch.read(...))` must use `> 0`. |
| Upload fails "Failed to connect" | Use a data cable. Do the BOOT + RST manual sequence. Lower `upload_speed` to 460800. Close any open serial monitor. |
| Strapping warning on GPIO45 / boot oddities | GPIO0/3/45/46 are strapping pins used by the LCD/touch. The warning is expected, but GPIO45's external pull-up only works because the module's eFuse forces VDD_SPI to 3.3 V. Don't add loads to the touch bus that change its boot level. See [02](02-pinout.md). |
| Relays click when using audio (or vice versa) | IO1/2/40 are shared. Move the 0 Ω links R25–27 → R21–23 for audio. |
| No sound | The amp needs the 0 Ω link move above. It plays the **right** I2S channel (CTRL tied high), so send mono to both slots. |
| Backlight turns off when dimmed low | The driver cuts out below ~100/255 at 100 kHz *(community figure, unverified)*. Use `B4848_LCD_BL_MIN`. For deeper dimming, see the hardware mod in [02](02-pinout.md). |
| Backlight whines | Low PWM frequency. `setBacklight()` attaches LEDC at 100 kHz. An earlier version called `analogWriteFrequency()` before the pin was attached; core 3.x silently ignores that, leaving it at 1 kHz. |
| Official espressif32 PIO platform fails with Arduino_GFX 1.6 | Use the **pioarduino** platform (Arduino core 3.x). The base ini already does. |
