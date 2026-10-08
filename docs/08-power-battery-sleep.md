# Power, battery and sleep

Everything here comes from the vendor schematic ([`vendor/schematic-1.png`](vendor/schematic-1.png), [`schematic-2.png`](vendor/schematic-2.png)) unless marked otherwise. Nothing has been measured on hardware yet.

## Power tree
```
USB-C VBUS ──D1 (1N5819)──> 5V ──> U1 VIN ─┐
P1 VIN ──Q1 (AO3401)──────> 5V             │   U1 (IP5306-class power-bank IC)
                                           ├── charges ──> P6 BAT (1S Li-ion / LiPo)
                                           └── VOUT ─────> VOUT-BAT ──┬──> U3 buck ──> 3.3V (ESP32, LCD logic, touch, SD, CH340C VCC)
                                                                      ├──> U5 backlight boost driver (EN = IO38)
                                                                      └──> U4 NS4168 amp
```
- **U1** has pins VIN, LED1–3, VOUT, SW, BAT and KEY, and the photo marking reads "IP53…" (Injoinic). That's an **IP5306-class** charger + boost IC.
- **SW1** connects to U1's KEY input (via R29), so it acts as the power button.
- **P1 VIN** goes through Q1, which only protects against reverse polarity. With USB connected, 5 V is back-fed onto P1 pin 1.
- The spec PDF lists about 260 mA typical consumption.

## Battery
- **Connector:** P6 `BAT`, JST 1.25 2-pin. Use a single-cell (3.7 V nominal) Li-ion/LiPo **with its own protection circuit**, and check the polarity against the silkscreen.
- **Charging:** automatic whenever 5 V is present. U1's charge-status LED pins (LED1–3) are **not connected to the ESP32**.
- **No battery-voltage ADC:** no GPIO senses the battery voltage, and there are few spare pins to add one. So there's **no fuel gauge or charge status in firmware** without a hardware mod, such as a resistor divider from BAT to a free ADC pin (only 1/2/40/41/42 are candidates; see [02](02-pinout.md)).
- **Light-load shutoff:** IP5306-class ICs switch their boost output off when the load drops below a threshold (tens of mA) for some seconds. At ~260 mA this board stays above that, but very deep software power saving on battery could trigger a shutoff.

## Sleep and wake
- **Deep-sleep wake sources** are limited:
  - **timer**
  - **GPIO0 / BOOT button** (ext0, active low)
  - touch is **not** available, because GT911 INT isn't wired to the ESP32
  - the UART RX line can wake from light sleep
- **The amp is always on.** NS4168 CTRL is tied high (R24 1 MΩ to VOUT-BAT), with no mute or shutdown pin.
- **The backlight** turns off fully with `setBacklight(0)`, since U5 EN has R14 as a pull-down.
- **Always powered from VOUT-BAT:** the 3.3 V buck, the CH340C and the amp. Expect a noticeable baseline current even in ESP32 deep sleep.

## Screen-off pattern (instead of deep sleep)
This is what the ESPHome HA-panel projects do, and it suits a mains- or USB-powered wall panel:
1. After N seconds without touch: turn the backlight off and pause the UI (in ESPHome, `lvgl.pause`; in our LVGL code, skip `lv_timer_handler()` or lower the refresh rate).
2. Keep polling touch, so a tap wakes the screen. Swallow that first tap so it doesn't press a button.
3. Turn the backlight back on and resume the UI.

Wi-Fi modem sleep (`WiFi.setSleep(true)`, the default) is compatible with this pattern.
