# 05 — powerwall-display

**Goal:** a live Tesla Powerwall 3 power-flow display, read straight from the gateway on the home LAN. No cloud, no server.

**Layout:** Solar top, Powerwall (SOC ring) left, Home right, Grid bottom.

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
