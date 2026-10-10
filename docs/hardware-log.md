# Hardware log

What has been verified on real units. Add an entry each time something is tested.

## Unit 1: MAC <mac>

| Item | Result |
|---|---|
| Chip | ESP32-S3 (QFN56) rev v0.2, 40 MHz crystal |
| Flash | **16 MB** detected (mfr 0x68, dev 0x4018). The factory bootloader header says 8 MB; that's just a conservative vendor setting. |
| PSRAM | 8 MB embedded (AP_3v3), octal. 8,384,052 bytes free at boot with `qio_opi` |
| USB | CH340C on COM9 (1A86:7523). Auto-reset upload works with no buttons |
| Factory backup | `firmware/factory/<mac>-20261010-0934.bin`, SHA256 `DA9F18AE…1A33`. Factory partitions: nvs, otadata, one 7.9 MB app0, coredump |
| Display | ST7701 init + RGB timings OK. Red/green/blue/white colour test correct |
| Flicker | Whole-screen flicker while drawing **without** bounce buffers. **Stable with** `B4848_LCD_BOUNCE_PX` = 480×10 |
| Touch | GT911 at **0x5D**, product ID "911". Finger drawing tracks correctly |
| Backlight | On at 255 (100 kHz LEDC). Minimum level and whine not yet checked |
| Not yet tested | Corner accuracy, rotation, backlight minimum, SD card, audio, relays, Wi-Fi, battery |
