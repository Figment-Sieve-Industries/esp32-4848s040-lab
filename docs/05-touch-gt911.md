# Touch: GT911

- Bus: I2C, SDA **19**, SCL **45**, 400 kHz
- Address: **0x5D** (0xBA/0xBB 8-bit). Some units use **0x14**. Our driver probes both.
- INT and RST are **not** connected to the ESP32, so you must **poll**. You also can't force the address-select reset sequence.
- Up to 5 touch points. The coordinates already match the 480×480 panel in rotation 0.

## Register cheat-sheet (16-bit big-endian addresses)
| Reg | Meaning |
|---|---|
| `0x8140`–`0x8143` | Product ID, ASCII "911" |
| `0x8047`–`0x80FF` | Config block (resolution, thresholds, checksum at 0x80FF) |
| `0x814E` | Status: bit7 = buffer ready, bits 0–3 = number of points. **Write 0 after reading.** |
| `0x814F + 8*i` | Point i: track id, X lo, X hi, Y lo, Y hi, size lo, size hi, reserved |

## Polling loop
1. Read `0x814E`.
2. If bit7 is set, read N points.
3. Write `0x00` to `0x814E`.

See `Touch::read()` in [`Board4848S040.cpp`](../common/lib/Board4848S040/src/Board4848S040.cpp).

## Libraries
- Our minimal driver (`board4848::Touch`) has no dependencies.
- `TAMC_GT911` (Arduino). Pass `-1` for INT and RST.
- `esp_lcd_touch_gt911` (ESP-IDF component) with `int_gpio_num = rst_gpio_num = GPIO_NUM_NC`
- ESPHome `touchscreen: platform: gt911`

## Rotation
For rotation r: 1 → (x,y) = (y, 479−x); 2 → (479−x, 479−y); 3 → (479−y, x). Verify the direction on your unit. ESPHome's `swap_xy` + `mirror_x` matches `rotation: 270`.
