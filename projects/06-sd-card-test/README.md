# 06 — sd-card-test

**Goal:** bring up the microSD slot and prove the shared SD code (`common/lib/SdStore` + the patched `common/lib/SD`) before project 05 relies on it.

**What it does**
- Mounts the card on its own SPI bus after the display (SD shares the LCD's init-bus pins), at the fastest clock that passes a 32 KB write/read link test. The working clock is remembered on the card (`/.sdstore.cfg`, keyed by the card's CID).
- Shows card state, counters and the soak status on screen, and runs:
  - **TESTS:** 19 functional tests (folders, text, 256 KB pattern, 1 MB throughput, 500 appends, power-cut-safe atomic files incl. torn/orphan/stale copies, 100 files, long names, a PC-written file).
  - **SPEED:** a clock sweep 1–25 MHz, then remembers one step below the fastest pass.
  - **SOAK:** one CSV record per second with read-back every 10th; pull the card mid-soak to test hot-swap.
  - **REMOUNT**
- Everything can be driven over serial (CH340, 115200): `t` tests, `s` sweep, `k` soak on/off, `r` remount, `i` info, `c` card registers (maker, model, date, rated clock, speed class), `f` forget the card's clock, `1/4/9/2/5` remount at 1/4/10/20/25 MHz, `b/B` backlight off/on, `h` help. Each command ends with a `#done <c>` line.

**Run** (PowerShell at the repo root, not Git Bash)
```powershell
.\tools\pio.ps1 -Project 06-sd-card-test -Action upload -Port COM9
cd projects\06-sd-card-test
& $env:USERPROFILE\.platformio\penv\Scripts\python.exe sd_serial.py COM9 --reset i c t s soak:120 i
```
`sd_serial.py` runs the steps in order, waits for each `#done`, and saves `sd_serial_<time>.log` (git-ignored). `wait:N` just listens, e.g. while you pull the card. For the PC-written file test, put `pc-check.txt` on the card first: `python make_pc_check.py E:/`.

**Findings** (details in [`docs/hardware-log.md`](../../docs/hardware-log.md))
- The Arduino-ESP32 3.2.1 SD driver doesn't wait for the card's busy period after a multi-block write's Stop Tran token, so large writes failed intermittently at every clock. Fixed in the patched copy (`common/lib/SD/README-mini-screen.md`).
- A 16 GB SDHC card (class 10 / U1) passes everything at 20–25 MHz: about 670 KB/s write and 1.47 MB/s read for 1 MB files, and ~17 ms per log-line append (open, write, close).
- An old 2 GB SanDisk SDSC card (2011, no speed class) is unreliable above 4 MHz and occasionally at 4 MHz: retired.
- Pulling and re-inserting the card while running works (unmount, pause, remount, resume). Inserting a card can briefly drop the PC's USB-serial link; the ESP32 does not reset.

**Status:** `works on hw` (2026-10-11).
