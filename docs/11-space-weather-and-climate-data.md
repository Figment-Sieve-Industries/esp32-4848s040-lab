# 11. Space weather and ENSO data sources (research)

Status: **idea**. Nothing here is built. Every endpoint below was fetched with curl on **2026-10-11 ~12:00 UTC**; sizes are bytes as downloaded (uncompressed, no `Accept-Encoding`). "Unverified" means it failed or returned the wrong thing.

Budget reminder: ~100 KB free internal RAM, 8 MB PSRAM. Parse with a streaming/filtering ArduinoJson doc (`DeserializationOption::Filter`) and put big buffers in PSRAM. Only the ESP32 task on core 0 does network (one TLS session at a time, as in project 05).

## 1. TLS and pinning

| Host | Chain (verified with openssl) | Pin this root | Notes |
|---|---|---|---|
| `services.swpc.noaa.gov` | Amazon RSA 2048 M01 -> **Amazon Root CA 1** | Amazon Root CA 1 (already in `ca_certs.h`) | Plain `http://` returns a 301 to HTTPS, so always use HTTPS. |
| `www.cpc.ncep.noaa.gov` | Go Daddy Secure CA G2 -> **Go Daddy Root CA G2** | Go Daddy Root Certificate Authority G2 (new cert) | Server sends only leaf + intermediate. |
| `soho.nascom.nasa.gov`, `sdo.gsfc.nasa.gov`, `api.helioviewer.org`, `psl.noaa.gov`, `www.ncei.noaa.gov` | Let's Encrypt (YR1/YR2, "Root YR") -> **ISRG Root X1** | ISRG Root X1 | The new "Root YR" is cross-signed by X1; pin X1 and check mbedTLS accepts the cross-sign chain (unverified on device). |
| `www.bom.gov.au` | GeoTrust TLS RSA CA G1 -> DigiCert Global Root G2 | DigiCert Global Root G2 | Returns 403 to default curl UA (Akamai); 200 with a browser UA. Not needed. |

SWPC JSON responses carry `Cache-Control: max-age=60` and a `Last-Modified`; fetch with `If-Modified-Since` only if you poll often. None of the sources below needs a key. Attribution: SWPC/CPC/NASA/ESA-NASA SOHO are public domain or free-to-use with credit; show "NOAA SWPC" / "NOAA CPC" / "SOHO (ESA & NASA)" in the About screen. SWPC asks for no hammering; one fetch per 5-30 min per product is polite.

## 2. SOLAR screen: verified sources

Sizes in **bold** are the ones I would actually use.

| Data | Endpoint (all `https://services.swpc.noaa.gov`) | Format | Bytes | Cadence | Use |
|---|---|---|---|---|---|
| NOAA R/S/G scales now + 3-day probs | `/products/noaa-scales.json` | JSON | **1,115** | ~1 min file; changes rarely | R/S/G badges. Keys `"0"` = now (R,S,G Scale), `"1".."3"` = forecast days, `"-1"` = past 24 h max. `Scale` is a string, null in forecast rows. |
| Current flare class + last flare | `/json/goes/primary/xray-flares-latest.json` | JSON array(1) | **454** | 1 min | Current class ("B7.0"), max class of the latest flare with begin/max/end times. |
| Flares last 7 days | `/json/goes/primary/xray-flares-7-day.json` | JSON | **23,883** (62 flares) | 1 min | List/bars of flares with `max_class`. Filter to M/X only to keep it tiny. |
| Flare probabilities (C/M/X, protons) 3 days | `/json/solar_probabilities.json` | JSON | **9,059** (30 days of rows, first row = today) | daily | M-class 65 %, X-class 15 % etc. Range fetch or filter to row 0. |
| Solar wind speed (latest) | `/products/summary/solar-wind-speed.json` | JSON | **59** | 1 min | `[{"proton_speed":595,"time_tag":"..."}]` |
| IMF Bt/Bz (latest) | `/products/summary/solar-wind-mag-field.json` | JSON | **60** | 1 min | `[{"bt":5,"bz_gsm":1,...}]` (integers only) |
| Speed + density + Bz + Bt, last hour, propagated to Earth | `/products/geospace/propagated-solar-wind-1-hour.json` | JSON array-of-arrays (header row) | **6,286** (60 rows) | 1 min | Only small source with **density**. Columns `time_tag, speed, density, temperature, bx, by, bz, bt, vx, vy, vz, propagated_time_tag`. Gives a 1 h sparkline for free. |
| 10.7 cm radio flux (latest) | `/products/summary/10cm-flux.json` | JSON | **47** | daily | `[{"flux":126,...}]` |
| 10.7 cm flux 30 days | `/products/10cm-flux-30-day.json` | JSON | **1,370** | daily | Sparkline. |
| Smoothed/monthly sunspot number + predicted cycle 25 | `/json/solar-cycle/predicted-solar-cycle.json` | JSON | **17,838** (2026-04 ... 2030-12 monthly, SSN + F10.7, low/high bands) | monthly | Cycle 25 curve forward. Keys have hyphens: `"time-tag"`, `"predicted_ssn"`, `"predicted_f10.7"`. |
| Observed monthly SSN history | `/json/solar-cycle/observed-solar-cycle-indices.json` | JSON | **512,444** (flag: too big) | monthly | Whole record since 1749. Fetch with an HTTP `Range: bytes=-N` tail, or do not use. Last rows: 2026-06 94.4, 07 78.1, 08 76.0, 09 60.3 (`ssn`; `smoothed_ssn` is -1 for the last 6 months). Also `/json/solar-cycle/sunspots-smoothed.json` 157,545 (flag). Range support on `services.swpc` is **unverified** (I did not test it). |
| Daily sunspot number, last 30 days | `/text/daily-solar-indices.txt` | text table | **2,919** | daily | Columns: date, F10.7, **SESC sunspot number**, area, new regions, flare counts C/M/X/S. E.g. `2026 10 10  126  99  690  0 ... 7 2 0`. Fixed-width, easy `sscanf`. Best source for "today's SSN". |
| 3-day Kp forecast + storm rationale | `/text/3-day-forecast.txt` | text | **2,160** | 2x/day (00:30, 12:30 UTC...) | Kp table per 3 h and S-scale probs. |
| 27-day outlook | `/text/27-day-outlook.txt` | text table | **1,606** | weekly (Mondays) | Rows `YYYY Mon DD  F10.7  Ap  maxKp`, 27 rows. Good for a 27-day Kp bar chart. |
| Forecast discussion (human-written, CMEs, sunspot regions) | `/text/discussion.txt` | text | **4,402** | 2x/day | Where CME arrival news lives ("CME ... near miss just ahead of Earth"). Show the first ~300 chars under "Solar activity". |
| Daily geophysical alert (WWV) | `/text/wwv.txt` | text | **683** | 3x/day | "Solar flux 126 and estimated planetary A-index 46", "Geomagnetic storms reaching the G3 level occurred", past/next-24 h summary. Tiny and very readable. |
| Kp forecast (observed/estimated/predicted) | `/products/noaa-planetary-k-index-forecast.json` | JSON | **6,822** | 3 h | Already close to what the aurora screen fetches. |
| Active alerts/warnings/watches | `/products/alerts.json` | JSON (83 msgs, free text) | 53,429 (flag) | event driven | Big and unstructured. Skip; use `noaa-scales.json` + `wwv.txt`. |
| Proton flux 6 h | `/json/goes/primary/integral-protons-6-hour.json` | JSON | 59,881 (flag) | 1 min | S-scale already in scales json. Skip. |
| X-ray flux 6 h / 1 day / 3 day | `/json/goes/primary/xrays-6-hour.json` | JSON | 160,620 / 653,083 / 1,961,424 (flag) | 1 min | No smaller variant exists (404 for 1-minute/5-minute names). A live X-ray graph is therefore not affordable. Use the flare class from `xray-flares-latest.json` instead. |
| Real-time solar wind full 1-min records | `/json/rtsw/rtsw_wind_1m.json`, `rtsw_mag_1m.json` | JSON | 2,679,730 / 1,586,480 (flag) | 1 min | Far too big. Source is now IMAP (`"source":"IMAP"`), not DSCOVR/ACE. |
| Old `/products/solar-wind/plasma-2-hour.json` etc. | | | 404 | | **Removed**. Do not use the old DSCOVR product names found in older tutorials. |
| NASA DONKI (CME/FLR APIs) | `https://api.nasa.gov/DONKI/CME?...&api_key=DEMO_KEY` | | **Unverified** | | Returned an HTML "CCMC Announcements" page (58,490 B) for every DONKI URL today, including the kauai.ccmc host. Treat DONKI as unavailable/migrated. Use `discussion.txt` for CME news instead. |
| SWPC Enlil model frames | `/products/animations/enlil.json` | JSON list of image URLs | 12,506 | 6 h | Just a list of frames (images are separate JPGs). Not recommended. |

Sample (trimmed) payloads:

```
xray-flares-latest.json  [{"time_tag":"2026-10-11T11:57:00Z","satellite":18,"current_class":"B7.0",
  "begin_class":"B6.5","max_time":"2026-10-11T10:38:00Z","max_class":"C1.1","end_class":"B8.7",...}]
noaa-scales.json  {"0":{"DateStamp":"2026-10-11","R":{"Scale":"0","Text":"none"},"S":{"Scale":"0",...},
  "G":{"Scale":"0","Text":"none"}},"1":{"R":{"MinorProb":"65","MajorProb":"15"},"S":{"Prob":"10"},"G":{"Scale":"1","Text":"minor"}},...}
propagated-solar-wind-1-hour.json  [["time_tag","speed","density",...,"bz","bt",...],
  ["2026-10-11T11:00:00Z",604.4,0.64,79520.0,4.75,1.03,0.89,4.94,...],...]
```

### Images

| Image | URL | Format / dims | Bytes | Freshness | Verdict |
|---|---|---|---|---|---|
| SOHO EIT 304 (chromosphere, orange sun) | `https://soho.nascom.nasa.gov/data/realtime/eit_304/512/latest.jpg` | baseline JPEG 512x512 | **97,532** | updated today 11:50 UTC | Good. Decode with TJpgDec at scale 1/2 -> 256x256 (RGB565, 128 KB in PSRAM). |
| SOHO HMI intensitygram (sunspots, white light) | `.../data/realtime/hmi_igr/512/latest.jpg` | baseline JPEG 512x512 | **72,487** | 10:56 UTC today | Best "see the sunspots" image. |
| SOHO LASCO C2 coronagraph (CMEs) | `.../data/realtime/c2/512/latest.jpg` (same file as SWPC `/images/animations/lasco-c2/latest.jpg`) | baseline JPEG 512x512 | 98,635 | ~hourly | Good for "CME watch". C3 is 150,636. |
| SDO AIA 193/304, HMI | `https://sdo.gsfc.nasa.gov/assets/img/latest/latest_256_0193.jpg` | baseline JPEG 256x256 | **~13-15 KB** (HMIIC 12.8 KB) | **STALE: Last-Modified is 21 Sep 2026 for every `latest_*` file** | Smallest and prettiest, but the "latest" images have not updated for 3 weeks. Do not use unless it is fixed. Check `Last-Modified` before trusting. |
| Helioviewer screenshot (any instrument, any size, server-side scaled) | `https://api.helioviewer.org/v2/takeScreenshot/?date=<UTC ISO>&imageScale=9.6&layers=[13,1,100]&x0=0&y0=0&width=256&height=256&display=true&watermark=false` (layer 13 = AIA 304; use `curl -g`/URL-encode brackets as `%5B13,1,100%5D`) | **PNG** 256x256 | **69,640** | real-time (`getClosestImage` returned 11:00:17 today) | Works, lets you choose size/instrument. PNG needs lodepng or LVGL PNG decoder; ~70 KB, PSRAM fine. API is an unofficial-for-embedded use; be polite (<= 1 request/30 min). `date=` must be a current UTC time you compute (needs NTP, which project 05 has). |
| SWPC SUVI 304 / 195 | `/images/animations/suvi/primary/304/latest.png` | PNG | 1,149,515 / 716,618 | 4-min cadence | Too large for the chip. Flag. |
| SWPC overview mosaic | `/images/swx-overview-small.gif` / `-large.gif` | GIF | 35,311 / 43,763 | live | GIF, needs a GIF decoder, content too busy for 480x480. Skip. |

Decoding notes: all the JPEGs are **baseline** (SOF0), which TJpgDec/`lv_tjpgd` handles. Progressive would not decode, so re-check after any URL change. LVGL can show the decoded buffer through an `lv_image` from a RAM `lv_image_dsc_t`. Alternative with no on-device decode cost: pre-scale to a 240x240 RGB565 blob on a small server. I do not recommend running a server for a hobby board.

## 3. ENSO screen: verified sources

CPC files are static text on Apache and **honour HTTP `Range`** (tested: `Range: bytes=-400` on `oni.ascii.txt` returned 206, 400 bytes), so you can fetch only the tail. `Cache-Control: no-store` is sent, so no conditional GET; use Range. Dates below are the real values found today.

| Data | Endpoint (all `https://www.cpc.ncep.noaa.gov`) | Format | Bytes | Cadence | Use |
|---|---|---|---|---|---|
| **ENSO status + probabilities (human text)** | `/products/analysis_monitoring/enso_advisory/ensodisc.shtml` | HTML | **13,499** (text is in the first ~6 KB; Range `0-8191` returned 206) | monthly, 2nd Thursday. Next: **12 Nov 2026** | "El Nino Advisory" headline, synopsis sentence, current Nino indices, odds. Parse: strip tags, take text after `Synopsis:` and the "chance is 54% for SON, 83% OND, 70% NDJ" sentence. Fragile (HTML scrape) but small and the only machine-reachable place for the alert status and %s; no JSON exists. `/enso_advisory/enso.json` is a 404. |
| ONI (3-month Nino3.4 anomaly) | `/data/indices/oni.ascii.txt` | text `SEAS YR TOTAL ANOM` | 23,025 full; **~400 via Range tail** | monthly (Last-Modified 3 Oct) | Tail: `JJA 2026 29.09 1.80`, `JAS 2026 29.12 2.16`. Official threshold is +0.5. |
| **RONI** (relative ONI, new official index) | `/data/indices/RONI.ascii.txt` | text `SEAS YR ANOM` | 14,736 full; **~300 via Range tail** | monthly | `JJA 2026 1.36`, `JAS 2026 1.69`. The 2026 CPC discussion quotes RONI for event strength ("+2.5 C or more for a 3-month RONI"), so show RONI, with ONI as secondary. |
| Monthly Nino 1+2/3/4/3.4 SST + anomaly | `/data/indices/sstoi.indices` | text | 39,272 full; **~200 via Range tail** | monthly | Last row `2026 9 ... NINO3.4 29.60 ANOM 2.84`. |
| **Weekly Nino SSTs** | `/data/indices/wksst9120.for` | fixed-width text | 148,416 full (flag); **~300 via Range tail (206 verified)** | weekly (Wed) | `30SEP2026  Nino34 SST 29.9 SSTA 3.2` and the 3 weeks before. Weekly Nino3.4 anomaly +3.2 vs the monthly +2.1 in the discussion (different baseline/periods; label clearly). |
| SOI (Tahiti-Darwin, standardised) | `/data/indices/soi` | text, two tables (raw, then standardised) | 12,968 | monthly | Sep 2026 = -3.3 (raw table) / -2.0 (standardised, second table). Missing months are `-999.9`. Tail-range awkward because of two tables; fetch whole 13 KB or just skip. |
| Nino3.4 detrended monthly | `/products/analysis_monitoring/ensostuff/detrend.nino34.ascii.txt` | text | 30,328 (flag) | monthly | Stops at 2026-06 (lags the others). Skip. |
| MEI.v2 | `https://psl.noaa.gov/enso/mei/data/meiv2.data` | text | **5,599** | monthly | Alternative index. ISRG X1 chain. |
| BoM ENSO | `https://www.bom.gov.au/climate/enso/` | HTML | 192,156 with a browser UA, **403** otherwise | fortnightly | Too big and bot-blocked. BoM data would need scraping; skip. BoM SOI via FTP (`ftp://ftp.bom.gov.au/.../soiplaintext.html`, 16,187 B) works but is FTP-only; skip. |
| IRI plume / CPC probability plot | `iri.columbia.edu/...` (79 KB HTML), `/enso_advisory/figure06.gif` (211,431 B) | HTML/GIF | flag | monthly | No numeric endpoint found. The odds are only available as text in `ensodisc` (above). |
| Official PDF | `/products/analysis_monitoring/lanina/enso_evolution-status-fcsts-web.pdf` | PDF | 4,776,718 | | No. |

Sample tails (real, 2026-10-11):

```
oni.ascii.txt   JJA 2026  29.09   1.80      JAS 2026  29.12   2.16
RONI.ascii.txt  JJA 2026  1.36              JAS 2026  1.69
sstoi.indices   2026   9   25.27 4.69  28.65 3.64  29.62 0.95  29.60 2.84   (Nino1+2, 3, 4, 3.4: SST + anomaly)
wksst9120.for   30SEP2026     26.1 5.3     29.0 4.0     29.9 3.2     29.9 1.2   (Nino1+2, 3, 34, 4: SST SSTA)
ensodisc: "El Nino Advisory ... a strong-to-very strong El Nino likely through January-March 2027 (greater than an 83% chance)"
```

For a trend line, use ONI/RONI tail of ~18 rows (~500 B with Range `bytes=-540`). You need to align rows yourself (row length is fixed: ONI 19 B/row, RONI 15 B/row, fixed-width, so the first partial line gets dropped).

## 4. Screen proposals (480x480, Montserrat)

Common frame: reuse `ui_common` top bar and page dots. Colours: use the existing palette; flare class/scale colours green/yellow/orange/red/purple map to NOAA's scale colours.

### 4a. SOLAR screen "Sun now" (recommended)

```
+------------------------------------------------+ 480
| Top bar: SUN              updated 12:00 UTC    |  40
+------------------------------------------------+
| [ Sun image 200x200 ]  | FLARE   B7.0   (28)   |  big class, Montserrat 48
| SOHO HMI 512 -> /2.5   | max today C1.1 10:38  |
|  (round-clipped)       | M-flare chance 65 %   |
|                        | X-flare chance 15 %   |  200
+------------------------+-----------------------+
| R 0 | S 0 | G 3    (3 pills, coloured, 28)      |  60
| next 24h G2 likely      (Montserrat 16)        |
+------------------------------------------------+
| Wind 595 km/s  | Dens 0.6 p/cc | Bz +1 nT      |  3 tiles, 20pt
|  [1-h sparkline of speed, Bz-coloured]         |  90
+------------------------------------------------+
| SSN 99   F10.7 126   Cycle 25: past peak (bar) |  50
+------------------------------------------------+
```

Fetches (all `services.swpc.noaa.gov`, ~5 min refresh while visible, 15 min otherwise; the image every 30 min):

| # | Fetch | Bytes |
|---|---|---|
| 1 | `noaa-scales.json` | 1,115 |
| 2 | `xray-flares-latest.json` | 454 |
| 3 | `propagated-solar-wind-1-hour.json` | 6,286 |
| 4 | `solar_probabilities.json` (only row 0 is used; the whole file) | 9,059 |
| 5 (daily) | `daily-solar-indices.txt` (last row = SSN, F10.7) | 2,919 |
| 6 (30 min) | SOHO HMI or EIT 304 JPEG | 72,487 / 97,532 |

JSON/text subtotal 19,833 B (fits one 20 KB buffer, sequential); the image is a separate PSRAM buffer of ~100 KB. If TLS memory is tight, drop #4 and the second-level fetches go to 10.8 KB.

### 4b. SOLAR screen page 2 "Space weather outlook" (optional)

```
Top bar: OUTLOOK
3-day Kp bars (24 columns, from 3-day-forecast.txt)        2,160
27-day outlook: 27 bars of Ap/Kp + F10.7 line              1,606
Discussion snippet (3 lines, Montserrat 14): CME news       4,402
Cycle 25 mini-chart: predicted SSN curve + this year's dots 17,838
```
Total about 26 KB if all four fetched once a day; none is needed more often than 12 h (27-day outlook weekly).

### 4c. ENSO screen "El Nino"

```
+------------------------------------------------+
| Top bar: ENSO              CPC, 2026-10-08     |
+------------------------------------------------+
| EL NINO ADVISORY   (banner, orange/red, 32)    |
| "strong-to-very strong likely through JFM 2027"|  (16, 3 lines)
+------------------------------------------------+
| Nino3.4 weekly   +3.2 C     (huge, 64)         |
| ONI +2.16  RONI +1.69  SOI -2.0                |  (20)
+------------------------------------------------+
| Line chart: monthly Nino3.4 anomaly, last 24 mo|  180
| with +0.5 / -0.5 bands, coloured fill          |
+------------------------------------------------+
| Odds: strong+ SON 54 %  OND 83 %  NDJ 70 %     |  (pills)
| next update 12 Nov                             |
+------------------------------------------------+
```

Fetches (every 12 h at most; ENSO changes weekly/monthly, so once at boot plus once daily; the discussion only changes on the 2nd Thursday, and weekly SST on Wednesdays):

| # | Fetch | Bytes |
|---|---|---|
| 1 | `ensodisc.shtml` Range `0-8191` | ~8,192 (full 13,499) |
| 2 | `wksst9120.for` Range `-300` (last 4 weeks) | 300 |
| 3 | `sstoi.indices` Range `-1100` (last ~24 months of monthly 3.4 anomaly, 48 B/row) | ~1,150 |
| 4 | `oni.ascii.txt` Range `-200` or `RONI.ascii.txt` Range `-150` | ~350 |
| 5 (optional) | `soi` full | 12,968 |

Total about 10 KB without SOI, 23 KB with. Gotcha: Range replies with a 206 that `HTTPClient` handles fine via `addHeader("Range", "bytes=-300")`, but check `getSize()` and drop the first partial line.

## 5. Ranking

| Rank | Idea | Effort | Why |
|---|---|---|---|
| 1 | Solar "Sun now": flare class + R/S/G pills + wind speed/density/Bz + 1-h sparkline (fetches 1-3 only, 7,855 B) | low | Everything live, tiny, verified, and pairs well with the existing aurora/Kp screen. |
| 2 | Add SOHO HMI/EIT sun image (72-98 KB JPEG, scale to 256) | medium | The "wow" element; baseline JPEG so TJpgDec works. Beware the stale SDO `latest_*` images. |
| 3 | ENSO screen (header text + weekly Nino3.4 + 24-month anomaly line), ~10 KB, 1 fetch/day | medium | Timely: a historic El Nino (CPC: 83 % chance of a record event for OND). Only fragile part is scraping `ensodisc.shtml`. |
| 4 | "Outlook" page: 27-day Ap/F10.7 bars, 3-day Kp, cycle-25 curve | low | Plain text tables, `sscanf` parsing, small. |
| 5 | WWV text one-liner ("Geomagnetic storms reaching G3 occurred") on the aurora screen | very low | 683 B. |

## 6. Gotchas

- Many tutorial endpoints are gone: `products/solar-wind/plasma-2-hour.json`, `mag-2-hour.json`, `xrays-1-minute.json`, `products/solar-flux.json`, `cme_analysis_latest.json`, `text/sunspot-report.txt` return 404. Use the ones in the table above.
- `rtsw` feeds now come from IMAP, field `"active"` is false; ignore that flag. `bz_gsm` / `bt` in the summary files are rounded integers.
- Several SWPC JSONs have mixed formats: `noaa-scales.json` values are strings (and null), `predicted-solar-cycle.json` keys have hyphens/dots, the propagated feed is array-of-arrays.
- Sizes of the "history" files (X-ray 6 h = 160 KB, observed cycle indices = 512 KB, ONI full = 23 KB, weekly SST full = 148 KB) make Range tail requests (CPC) or the 7-day flare list (SWPC) the right tool. Range on SWPC is untested.
- CPC sends `Cache-Control: no-store`; do not rely on ETag.
- DONKI is unreachable right now; do not plan on it. CME arrival news comes via `discussion.txt`, which is prose.
- SDO "latest" JPEGs are 3 weeks stale (all products), so either use SOHO or Helioviewer.
- Two different Nino3.4 numbers on the same page (weekly +3.2, monthly +2.1/+2.84, ONI +2.16, RONI +1.69) will confuse; label each with period and index name.
- Date sensitivity: ensodisc "next discussion 12 November 2026" is a text sentence; fetch daily and only re-parse when Last-Modified changes (HEAD returns it; CPC's `no-store` does not block `Last-Modified`).
- All figures above were true on 2026-10-11; flux/storm values are examples only.
