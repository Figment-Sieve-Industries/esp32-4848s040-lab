# 01 — hello-display-touch

**Goal:** prove that the display, backlight, touch and serial all work.

**What it does**
1. Prints free PSRAM to serial.
2. Flashes red → green → blue → white. If you see the wrong colours, check the pin map.
3. Detects the GT911 and prints its address.
4. Finger-paint screen. Tap **CLR** (top right) to clear. Coordinates are shown bottom-left and printed to serial.

**Run**
```sh
~/.platformio/penv/Scripts/pio run -t upload
~/.platformio/penv/Scripts/pio device monitor
```

**Status:** builds; not yet tested on hardware.
