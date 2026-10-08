# Programming and flashing

## Short answer: yes, just use the USB-C port
The USB-C connector goes through an onboard **CH340C** USB-to-serial chip to UART0 of the ESP32-S3. Two transistors (T1/T2 on the PCB) form the usual DTR/RTS **auto-reset circuit**. So `pio run -t upload`, Arduino IDE or `esptool` can flash the board with no buttons and no extra adapter. The same port is your serial monitor (`Serial` at 115200).

You don't need a separate USB-TTL adapter. The P1 JST header (GND/RX/TX/+5V) exposes the same UART0 if you ever want one.

> The ESP32-S3's *native* USB (GPIO19/20) is **not** used. Those pins drive the touch I2C and an LCD colour bit. So:
> - set `ARDUINO_USB_CDC_ON_BOOT=0` (Serial → UART0 → CH340)
> - USB-OTG / USB-HID / JTAG-over-USB features are not available on this board

## Windows driver
Windows 10/11 usually installs the CH340 driver automatically. The device shows as **USB-SERIAL CH340 (COMx)**, VID:PID `1A86:7523`.
If it shows up as an unknown device, install `CH341SER.EXE` from WCH: https://www.wch-ic.com/downloads/CH341SER_EXE.html

Use a **data** USB-C cable. Charge-only cables are a common trap.

## If auto-reset fails (manual download mode)
1. Hold **BOOT**
2. Tap **RST**
3. Release **BOOT**
4. Start the upload. Press **RST** afterwards to run the new firmware.

## Useful esptool commands
esptool ships inside PlatformIO (`~/.platformio/packages/tool-esptoolpy`). Alternatively, `pip install esptool`.

```sh
# Identify chip, flash size and PSRAM
esptool --port COMx chip_id
esptool --port COMx flash_id

# BACK UP THE FACTORY FIRMWARE FIRST (16 MB)
esptool --port COMx -b 921600 read_flash 0 0x1000000 factory_backup.bin

# Restore it
esptool --port COMx -b 921600 write_flash 0 factory_backup.bin

# Full erase (fixes most "weird state" problems)
esptool --port COMx erase_flash
```

> Back up the factory image before your first upload. It's the only way back to the vendor demo.

## Upload settings summary
| Setting | Value |
|---|---|
| Board | `esp32-s3-devkitc-1` (PlatformIO) / "ESP32S3 Dev Module" (Arduino IDE) |
| Flash size | 16 MB (QIO, 80 MHz) |
| PSRAM | **OPI PSRAM** (Arduino IDE) / `memory_type = qio_opi` (PIO) |
| Partition | 16 MB default, or "Huge APP" |
| USB CDC on boot | **Disabled** |
| Upload speed | 921600 (drop to 460800 if it's flaky) |

## OTA
Once you have Wi-Fi code running, ArduinoOTA or ESPHome OTA lets you update without the cable. That's handy when the unit is in a wall box.
