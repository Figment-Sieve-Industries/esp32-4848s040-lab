# 01 — hello-display-touch

**Goal:** prove that the display, backlight, touch and serial all work.

**What it does**
1. Prints free PSRAM to serial.
2. Flashes red → green → blue → white. If you see the wrong colours, check the pin map.
3. Detects the GT911 and prints its address.
4. Finger-paint screen. Tap **CLR** (top right) to clear. Coordinates are shown bottom-left and printed to serial.

**Run** (PowerShell at the repo root, not Git Bash)
```powershell
.\tools\pio.ps1 -Project 01-hello-display-touch -Action all
```

**Status:** `builds` (not yet run on hardware)
