# Firmware images

## `factory/`: your own backups
`tools/backup-flash.ps1` writes full 16 MB flash dumps here, named `<MAC>-<yyyyMMdd-HHmm>.bin`. They are **committed on purpose** (16 MB each), so a backup can't be lost with a working copy. `.gitignore` has an exception for `firmware/**/*.bin`.

Restore one with:
```powershell
.\tools\restore-flash.ps1 -File firmware\factory\<file>.bin
```

## Vendor factory images
Guition's demo firmware is in the data-pack mirror:
https://github.com/NorthernMan54/ESP32-4848S040/tree/main/ESP32-4848S040/8-Burn%20operation/Burn%20files

| File | Size | Raw download |
|---|---|---|
| `86switch_onoff.bin` | 5,365,104 B | https://raw.githubusercontent.com/NorthernMan54/ESP32-4848S040/main/ESP32-4848S040/8-Burn%20operation/Burn%20files/86switch_onoff.bin |
| `switch86_lvgl_music.bin` | 3,094,576 B | https://raw.githubusercontent.com/NorthernMan54/ESP32-4848S040/main/ESP32-4848S040/8-Burn%20operation/Burn%20files/switch86_lvgl_music.bin |
| `4.0_LvglWidgets.bin` | 652,640 B | https://raw.githubusercontent.com/NorthernMan54/ESP32-4848S040/main/ESP32-4848S040/8-Burn%20operation/Burn%20files/4.0_LvglWidgets.bin |

All three are **merged full images**. Each has bootloader magic `0xE9` at 0x0, header flash mode DIO, `0x4F` = 16 MB @ 80 MHz, and partition-table magic `0xAA50` at 0x8000. These were checked on 2026-10-08 by reading the file headers. Write them at offset 0:

```powershell
.\tools\restore-flash.ps1 -File 86switch_onoff.bin
```

`86switch_onoff.bin` is most likely the shipped wall-switch demo. If you didn't take your own backup, it's the closest thing to factory state.
