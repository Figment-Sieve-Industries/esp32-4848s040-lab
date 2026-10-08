# Programming and flashing

All commands are for **Windows PowerShell**, run from the repo root. Don't use Git Bash (see [07](07-gotchas.md)).

## Short answer: just use the USB-C port
The USB-C connector goes through an onboard **CH340C** USB-to-serial chip to UART0 of the ESP32-S3. Two transistors (T1/T2) form the usual DTR/RTS **auto-reset circuit**, so PlatformIO, Arduino IDE or `esptool` can flash the board with no buttons and no extra adapter. The same port is your serial monitor (`Serial` at 115200).

You don't need a separate USB-TTL adapter. The P1 HC-1.25 4-pin header (VIN/TX/RX/GND) and the P2 connector expose the same UART0 if you ever want one. P2 has no IO0, so hold **BOOT** while resetting when flashing through it.

> The ESP32-S3's *native* USB (GPIO19/20) is **not** used. Those pins drive the touch I2C and an LCD colour bit, and the USB-C data lines go only to the CH340C. So:
> - set `ARDUINO_USB_CDC_ON_BOOT=0` (Serial → UART0 → CH340); the shared `common/platformio-base.ini` does this
> - USB-OTG / USB-HID / JTAG-over-USB features are not available on this board

## Windows driver and COM port
Windows 10/11 usually installs the CH340 driver automatically. The device shows as **USB-SERIAL CH340 (COMx)**, VID:PID `1A86:7523`.
- If it shows up as an unknown device, install `CH341SER.EXE` from WCH: https://www.wch-ic.com/downloads/CH341SER_EXE.html
- Find the COM number in **Device Manager > Ports (COM & LPT)**, or with:
  ```powershell
  & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
  ```
- Use a **data** USB-C cable. Charge-only cables are a common trap.

## Build, flash, monitor
```powershell
.\tools\pio.ps1 -Project 01-hello-display-touch -Action all              # build + upload + monitor
.\tools\pio.ps1 -Project 02-lvgl-hello -Action upload -Port COM5          # just upload, explicit port
.\tools\build-all.ps1                                                     # build every project, pass/fail table
```
Or in VS Code: **File > Open Folder** on the project folder, then use the PlatformIO toolbar.

## If auto-reset fails (manual download mode)
1. Hold **BOOT**
2. Tap **RST**
3. Release **BOOT**
4. Start the upload. Press **RST** afterwards to run the new firmware.

## Back up and restore the factory firmware
**Do this before your first upload.**
```powershell
.\tools\backup-flash.ps1                 # auto-detects the port; or -Port COM5
.\tools\restore-flash.ps1 -File firmware\factory\<file>.bin
```
- The backup reads all 16 MB, takes **a few minutes**, and saves to `firmware/factory/<mac>-<date>.bin`. That file **is committed to git**, so it can't be lost.
- No backup? The vendor's factory demo images can still be restored. They are full merged images written at 0x0. See [`../firmware/README.md`](../firmware/README.md).

## Manual esptool
esptool v5 ships inside PlatformIO. v5 uses hyphenated commands.
```powershell
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$py      = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"

& $py $esptool --port COM5 chip-id
& $py $esptool --port COM5 flash-id                                       # flash size, PSRAM info
& $py $esptool --port COM5 -b 921600 read-flash 0 0x1000000 backup.bin   # full 16 MB read
& $py $esptool --port COM5 -b 921600 write-flash 0x0 backup.bin          # restore
& $py $esptool --port COM5 erase-flash                                    # fixes most "weird state" problems
```

## Upload settings summary
| Setting | Value |
|---|---|
| Board | `esp32-s3-devkitc-1` + overrides from `common/platformio-base.ini` (PlatformIO) / "ESP32S3 Dev Module" (Arduino IDE) |
| Flash size | 16 MB (QIO, 80 MHz) |
| PSRAM | **OPI PSRAM** (Arduino IDE) / `memory_type = qio_opi` (PIO) |
| Partition | `default_16MB.csv` (two 6.25 MB OTA app slots) |
| USB CDC on boot | **Disabled** |
| Upload speed | 921600 (drop to 460800 if it's flaky) |

## OTA
Once you have Wi-Fi code running, ArduinoOTA or ESPHome OTA lets you update without the cable. That's handy when the unit is in a wall box. `default_16MB.csv` already has two OTA slots.
