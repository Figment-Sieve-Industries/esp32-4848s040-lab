# 05 — powerwall-display

**Goal:** a live Tesla Powerwall 3 power-flow display, read straight from the gateway on the home LAN. No cloud, no server.

**Layout:** design B "house hub" (see the design canvas): house centre, Solar top-left, Grid bottom-left, Powerwall bottom-right, weather + grid-status card top-right. Chevrons run along elbow lines in the direction of flow. Icons are drawn with LVGL lines and arcs (`icons.cpp`). Weather comes from Open-Meteo every 15 min (`WEATHER_LAT`/`WEATHER_LON` in `secrets.h`).

**What it shows**
- **Nodes:** Solar, Grid, Home and Powerwall, with live watts.
- **Flow dots:** they run along each link in the direction power is flowing; faster for more power.
- **Powerwall:** an SOC ring and %, plus "Charging" / "Discharging" / "Idle".
- **Grid:** "Importing" / "Exporting", and a red **OFF-GRID** banner during an outage.
- **Top bar:** clock (NTP), date, and the Wi-Fi network with its signal.
- **Faults:** errors or stale data (>10 s old) show bottom-left, and values change to `--`.
- **Tap anywhere** to cycle the backlight brightness (255 → 160 → 100).
- **`SCREEN_ROTATION`** in `main.cpp`: the default `3` is 90° counter-clockwise, so the USB-C port is at the bottom. Touch follows the rotation.

**How it gets the data** (local API on firmware 26.26.4, customer password, read-only):
| Call | Returns |
|---|---|
| `POST /api/login/Basic` | `{"token": …}`, then sent as `Authorization: Bearer <token>` |
| `GET /api/meters/aggregates` | site / battery / load / solar `instant_power` (chunked response) |
| `GET /api/system_status/soe` | `{"percentage": …}` |
| `GET /api/system_status/grid_status` | `SystemGridConnected` or islanded |

- No token gives 403; an expired or bad token gives 401. Either way it logs in again automatically.
- Polls every 2 s on core 0 (`powerwall.cpp`); LVGL runs in `loop()` on core 1. They share data only through a mutex-protected snapshot.
- Sign conventions: grid > 0 = importing; battery > 0 = discharging.

**Setup**
1. Copy `src/secrets.example.h` to `src/secrets.h` (git-ignored) and fill in:
   - the Wi-Fi networks: any number; `WiFiMulti` joins the strongest; **2.4 GHz only**
   - the gateway IP
   - the **customer password**: the last 5 characters of the gateway password on the QR sticker
2. Give the Powerwall a DHCP reservation so its IP doesn't change.
3. `.\tools\pio.ps1 -Project 05-powerwall-display -Action all` (PowerShell)

**Diagnostics:** serial prints a status line every 10 s. If no configured network is found, it prints a scan of every network it can see.

**Memory notes**
- No `LV_ATTRIBUTE_FAST_MEM` in this project. The core's mbedTLS allocates only from internal RAM, and needs ~40–50 KB per TLS session.
- The LVGL buffer is 48 lines.
- Result: about 170 KB internal free at boot and ~125 KB while polling.

**Status:** `works on hw` (2026-10-10). Layout (rotation 3, Powerwall left, Grid bottom) confirmed; flow animation ~28 fps, smooth.

## Screens (swipe left / right)
| # | Screen | Shows |
|---|---|---|
| 1 | **B · Power flow** | House hub with chevron flows. Weather card: temperature, scrolling condition, high/low, today's rain chance, animated icon. Grid status. Top bar: Wi-Fi network + dBm |
| 2 | **G · Today** | Self-powered % ring, solar / home / Powerwall in-out kWh, exported / imported tiles, solar-by-hour bars |
| 3 | **H · Battery & grid** | SOC, charge/discharge power and time to full/empty (13.5 kWh assumed), grid voltage and frequency with normal-range markers, lifetime round-trip efficiency, grid-services (VPP) badge |
| 4 | **I · Weather** | Current conditions (feels-like, wind, high/low, rain %), next 12 h in 2 h steps, lifetime totals |

Code: `screen_house.cpp`, `screen_today.cpp`, `screen_health.cpp`, `screen_weather.cpp`. Shared look and helpers: `ui_common.*`. Icons: `icons.*`.

## No flash wear
- Nothing is written to flash at runtime.
- **"Today" totals** come from the Powerwall's lifetime counters minus a midnight snapshot. The snapshot is kept in **RTC memory** (`RTC_NOINIT_ATTR`, `energy.cpp`). It survives software restarts and crashes, but not a power cut, the reset button or a reflash; the `EN` reset reports as power-on.
- **After such a reset**, the screen shows "TODAY SINCE HH:MM" until the next midnight.
- A future SD card could keep a full history.

## Memory
- LVGL uses its own 1 MB pool in **PSRAM** (`LV_MEM_POOL_ALLOC` in `lv_conf.h`). With the C heap, every small widget lands in internal RAM, and the four screens left only ~38 KB for TLS.
- Now: ~188 KB internal free at boot and ~140 KB while polling.
