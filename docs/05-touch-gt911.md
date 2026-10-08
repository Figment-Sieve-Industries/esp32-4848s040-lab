# Touch: GT911

- **Bus:** I2C, SDA **19**, SCL **45** (4.7k pull-ups on board), 400 kHz, using `Wire` (I2C_NUM_0).
- **INT and RST** are **not connected to the ESP32**.
  - RST shares the LCD's RC power-on reset (R16 10k / C22 1 µF).
  - INT runs only between the LCD and touch FPCs and floats.
  - So you must **poll**, and firmware can't run the address-select reset sequence.
- **Up to 5 touch points.** In rotation 0 the coordinates already match the 480×480 panel.

## I2C address
GT911 datasheet §4.1/4.2: the address is latched at the end of reset from the level on INT.
- INT **low** during reset → **0x5D** (0xBA/0xBB 8-bit)
- INT **high** during reset → **0x14** (0x28/0x29 8-bit)

Here INT floats and reset is a plain RC, so the address depends on the touch flex and the power-up behaviour, not on firmware. Every reference config for this board uses **0x5D**. 0x14 is possible in principle but unconfirmed on this board.

`board4848::Touch::begin()` probes 0x5D, then 0x14, and accepts a device only if its product ID (0x8140) reads `"911"`. A different device answering at one of those addresses is rejected.

Note: some reference code (esp32-smartdisplay `esp_touch_gt911.h`, the joevial `monolith.cpp` comment) states the INT→address mapping backwards. The datasheet is the authority.

## Register cheat-sheet (16-bit big-endian addresses)
| Reg | Meaning |
|---|---|
| `0x8140`–`0x8143` | Product ID, ASCII "911" |
| `0x8047`–`0x80FE` | Config block (resolution, thresholds); checksum at `0x80FF` |
| `0x814E` | Status: bit 7 = buffer ready, bits 0–3 = number of points. **Write 0 after reading.** |
| `0x814F + 8*i` | Point i: track id, X lo, X hi, Y lo, Y hi, size lo, size hi, reserved |

## Polling loop
1. Read `0x814E`.
2. If bit 7 is clear: there's no new data, so `read()` returns **-1**.
3. If bit 7 is set: read N points, write `0x00` to `0x814E`, and return N. **N = 0 means the finger was lifted**: the GT911 reports a release as ready with zero points.

```cpp
board4848::Touch touch;
touch.begin();
touch.setRotation(gfxRotation);      // keep in step with initDisplay(rotation)

board4848::TouchPoint p;
int8_t n = touch.read(&p, 1);
if (n > 0)       { /* pressed at p.x, p.y */ }
else if (n == 0) { /* released */ }
// n == -1: nothing new since the last read; keep the previous state
```

`board4848::lvglBegin()` already wires this into an LVGL input device, with a 100 ms release fallback in case a release report is missed.

## Rotation
`Touch::setRotation(r)` applies the same transform as the display rotation (r = 0..3) so coordinates stay in screen space:
- 1 → (x, y) = (y, 479 − x)
- 2 → (479 − x, 479 − y)
- 3 → (479 − y, x)

Check the direction on your unit. ESPHome's `swap_xy` + `mirror_x` matches `rotation: 270`.

## Libraries
- Our driver (`board4848::Touch`) has no dependencies.
- `TAMC_GT911` (Arduino). Pass `-1` for INT and RST.
- `esp_lcd_touch_gt911` (ESP-IDF component) with `int_gpio_num = rst_gpio_num = GPIO_NUM_NC`.
- LovyanGFX `Touch_GT911`.
- ESPHome `touchscreen: platform: gt911`.
