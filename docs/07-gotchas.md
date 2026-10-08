# Gotchas

| Symptom | Cause / fix |
|---|---|
| Nothing on `Serial` | `ARDUINO_USB_CDC_ON_BOOT` must be **0**. Native USB isn't wired; the console is on the CH340. |
| Boot loop or `PSRAM init failed` | PSRAM is **octal**: use `memory_type = qio_opi` (PIO) or "OPI PSRAM" (Arduino IDE). |
| Black screen, backlight on | The init sequence wasn't accepted (wrong table or SPI pins), or `gfx->begin()` failed because the framebuffer couldn't be allocated (no PSRAM). |
| Black screen, backlight off | GPIO38 isn't driven high. Call `setBacklight(255)`. |
| Image shifted, rolling or shimmering | RGB porch or PCLK timing. See [04](04-display-st7701.md). Wi-Fi + PSRAM contention: lower PCLK or use bounce buffers. |
| Colours swapped or wrong | RGB vs BGR order, or the wrong data-pin order. Use the pin map from `Board4848S040_pins.h` exactly. |
| Touch not detected | Try address 0x14. INT/RST aren't wired, so you can't force the address. Check SDA 19 / SCL 45. |
| Touch axes wrong after rotating | Apply the same rotation to touch coordinates (see [05](05-touch-gt911.md)). |
| Upload fails "Failed to connect" | Use a data cable. Do the BOOT+RST manual sequence. Lower `upload_speed` to 460800. |
| Strapping warning on GPIO45 / boot oddities | GPIO0/3/45/46 are strapping pins used by the LCD/touch. Normal on this board; ignore the warning. |
| Relays click when using audio (or vice versa) | IO1/2/40 are shared. Move R25–27 to R21–23 for audio. |
| Official espressif32 PIO platform fails with Arduino_GFX 1.6 | Use the **pioarduino** platform (Arduino core 3.x). |
| `idf_tools.py installation failed`, then `xtensa-esp32s3-elf-g++ is not recognized` | First build was run from **Git Bash/MSYS**, which ESP's tool installer rejects. Run `pio` from PowerShell, cmd or the VS Code PlatformIO buttons. |
| `IncompatiblePlatform ... depends on PlatformIO Core >=6.2.0` | Projects pin pioarduino `54.03.21-2`, which works with Core 6.1.x. To use newer releases, run `pio upgrade` first. |
| Backlight turns off when dimmed low | The AP3031 driver cuts out below ~60/255. Use `B4848_LCD_BL_MIN`. A hardware mod for deeper dimming is described in `reference/ESP32-4848S040-LVGL-LGFX`. |
| Backlight whines | Low PWM frequency. `setBacklight()` uses 100 kHz. |
