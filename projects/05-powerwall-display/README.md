# 05 — powerwall-display

**Goal:** a live Tesla Powerwall 3 power-flow display, read straight from the gateway on the home LAN. No cloud, no server.

**Layout:** design M "values in pills" (see the design canvas): house centre, Solar pill top-left, Grid pill bottom-left, Powerwall pill bottom-right (with an SOC bar), and a weather + grid-status + VPP card top-right. Chevron flows run in the direction of power flow. Tag chips show voltage, VA / var, flow direction and charge state. Icons are drawn with LVGL lines and arcs (`icons.cpp`). Weather comes from Open-Meteo every 15 min (`WEATHER_LAT`/`WEATHER_LON` in `secrets.h`).

**What it shows**
- **Nodes:** Solar, Grid, Home and Powerwall, with live watts.
- **Flow dots:** they run along each link in the direction power is flowing; faster for more power.
- **Powerwall:** an SOC ring and %, plus "Charging" / "Discharging" / "Idle".
- **Grid:** "Importing" / "Exporting", and a red **OFF-GRID** banner during an outage.
- **Top bar:** clock (NTP), date, and the Wi-Fi network with its signal.
- **Faults:** errors or stale data (>10 s old) show at the bottom centre, below the page dots, and values change to `--`.
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
- LVGL uses its own 1 MB PSRAM pool (see Memory below).

**Status:** `works on hw` (2026-10-10). Layout (rotation 3; Solar top-left, Grid bottom-left, Powerwall bottom-right) confirmed; flow animation ~28 fps, smooth.

## Screens (swipe left / right)
| # | Screen | Shows |
|---|---|---|
| 1 | **Power flow (design M)** | House hub with chevron flows; Solar / Grid / Powerwall as pills (value inside, SOC bar on the Powerwall pill); tag chips for Home voltage (amber outside 207-253 V) and VA / var, Grid direction and kVA / var, Powerwall charge state. Top-right card adds a VPP row (grey ring "VPP idle", pulsing amber "VPP event active" from `grid_services_active`). Weather card: temperature, scrolling condition, high/low, today's rain chance, animated icon. Grid status. Top bar: Wi-Fi network + dBm |
| 2 | **G · Today** | Self-powered % ring, solar / home / Powerwall in-out kWh, exported / imported tiles, solar-by-hour bars |
| 3 | **H · Battery & grid** | SOC, charge/discharge power and time to full/empty (13.5 kWh assumed), grid voltage and frequency with normal-range markers, lifetime round-trip efficiency, grid-services (VPP) badge |
| 4 | **I · Weather** | Current conditions (feels-like, wind, high/low, rain %), next 10 h (now to +10 h) in 2 h steps, lifetime totals |

Code: `screen_house.cpp`, `screen_today.cpp`, `screen_health.cpp`, `screen_weather.cpp`. Shared look and helpers: `ui_common.*`. Icons: `icons.*`.

## No flash wear
- Nothing is written to flash at runtime.
- **"Today" totals** come from the Powerwall's lifetime counters minus a midnight snapshot. The snapshot is kept in **RTC memory** (`RTC_NOINIT_ATTR`, `energy.cpp`). It survives software restarts and crashes, but not a power cut, the reset button or a reflash; the `EN` reset reports as power-on.
- **After such a reset**, the screen shows "TODAY SINCE HH:MM" until the next midnight.
- A future SD card could keep a full history.

## Memory
- LVGL uses its own 1 MB pool in **PSRAM** (`LV_MEM_POOL_ALLOC` in `lv_conf.h`). With the C heap, every small widget lands in internal RAM, and the four screens left only ~38 KB for TLS.
- Now: ~188 KB internal free at boot and ~140 KB while polling.

## Behaviour notes
- **Battery %** matches the Tesla app: `(raw - 5) / 0.95`. Disable with `-DPW_SOC_APP_SCALE=0` to show the raw value.
- **Capacity** (used for time to full/empty) can be set with `-DPW_CAPACITY_KWH=<kWh>`.
- **Stale weather:** weather data older than 1 h is shown as stale.
- **Login back-off:** after failed logins the gateway login is retried with increasing delays.
- **One HTTPS connection** to the gateway is reused across polls.
- **Weather** is fetched by its own task, using a pinned CA certificate.
