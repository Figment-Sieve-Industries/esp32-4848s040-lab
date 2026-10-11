# 09 – Alternative display boards

Boards similar to the Guition ESP32-4848S040C_I, with UK buying options checked on **2026-10-11**.

## How the prices were checked

- Prices in the "Verified UK pricing" section were read live in Chrome on 2026-10-11. The AliExpress ones were read while you were signed in, so they show what you would be charged, apart from checkout coupons. They are noted otherwise where they weren't.
- The "Overview" section uses rough figures from memory. Those prices were **not verified**.
- AliExpress prices depend on who is looking. Signed out, one listing showed a "Welcome deal" for new shoppers (£42.67); signed in, it was £44.49. Timed sales ("Brand day", "SuperDeals") also change prices within hours. One listing showed a different price on the search page than on the item page (£41.06 vs £41.83).
- Prices quoted by search engines and review sites are often stale. Spotpear's widely quoted $34.90/$36.90 JC1060P470 pages are **off-production**.
- AliExpress prices shown in GBP include UK VAT, so nothing is due on delivery. Sellers outside the UK that don't collect VAT (such as Waveshare direct) leave you to pay import VAT and a handling fee when the parcel arrives.

## Verified UK pricing

### Guition JC1060P470 (ESP32-P4, 7" 1024×600 MIPI-DSI, ESP32-C6 for Wi-Fi)

| Seller | Variant | Item | UK shipping | Delivered | Delivery est. | Notes |
|---|---|---|---|---|---|---|
| **[GUITION Official Store (AliExpress)](https://www.aliexpress.com/item/1005010022828767.html)** | JC1060P470C-I-W-Y, case + camera | **£44.49** incl. VAT | Free (Choice) | **£44.49** | Oct 14–17 | Checked signed in. Manufacturer store; 1,000+ sold; 4.8★ from 81 reviews. £42.39 each for 3 or more. **Recommended.** |
| [SURENOO Official Store (AliExpress)](https://www.aliexpress.com/item/1005008472369035.html) | JC1060P470C-W-Y | £49.99 (sale; normally £62.49) | £9.45 | ~£59.44 | Oct 18–27 | Checked signed out |
| [YiXin Store (AliExpress)](https://www.aliexpress.com/item/1005012374722129.html) | without / with case | £58.59 / £70.39 | £7.62 | ~£66 / ~£78 | Oct 18–27 | Checked signed out |
| [Unnamed seller (AliExpress)](https://www.aliexpress.com/item/1005013070888404.html) | JC1060P470C-I-W-Y | £36.89 | £15.19 | ~£52 | Oct 18–27 | Checked signed out; only 1 sale |
| [Spotpear](https://spotpear.com/shop/ESP32-P4-Display-7inch-Capacitive-TouchScreen-JC1060P470C.html) | JC1060P470C_I_W_Y, in stock | $49.90 | Shown only at checkout | ? + import VAT | – | The older `_I_W` pages are off-production |

Other AliExpress listings were found by a web-search agent but **not opened in Chrome**. Their prices come from signed-out search pages and are unverified:

- `1005010766741181`: £51.89
- `1005010748515638`: £46.39
- `1005010023056143`: £39.69. Its title doesn't name the JC1060P470.
- `1005012097974079`: £54.99
- `1005013098934168`: £56.99

Watch for the **JC1060P470N** suffix, which is a version without touch.

### Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3, 7" 800×480 RGB, touch, SKU 27078)

| Seller | Item | UK shipping | Delivered | Notes |
|---|---|---|---|---|
| **[Development Board Store (AliExpress)](https://www.aliexpress.com/item/1005008300327579.html)** | **£39.49** incl. VAT | £7.10 | **£46.59** | Checked signed in. Select the **"With Touch Function"** bundle; the page defaults to the no-touch one at £34.29. The package photo shows the ESP32-S3-Touch-LCD-7. Oct 18–24; 446 available. |
| [Waveshare direct](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm?sku=27078) | $36.99 (1–9 units) | Calculated at checkout (E-packet, DHL, FedEx, UPS) | ? | You pay UK import VAT and duties on delivery. |
| [The Pi Hut](https://thepihut.com/products/esp32-s3-development-board-with-7-capacitive-touch-display-800-x-480) | £35.60 incl. VAT | – | – | **Sold out** |
| [BerryBase (DE)](https://www.berrybase.de/en/waveshare-esp32-s3-touch-lcd-7-800x480-7-5-point-touch-wi-fi-bluetooth-5-512kb-sram-240mhz) | €42.90 incl. DE VAT | Not stated | – | Currently not available. Fetched by the agent, not opened in Chrome. |

### Also worth a look: Waveshare ESP32-S3-Touch-LCD-7B (ESP32-S3, 7" 1024×600)

| Seller | Item | UK shipping | Delivered | Notes |
|---|---|---|---|---|
| [Makerigh… store (AliExpress; name cut off on the page)](https://www.aliexpress.com/item/1005006187373697.html) | £41.83 (SuperDeals; normally £45.05) | Free (Choice) | **£41.83** | Checked signed in. Oct 15–18; 433 sold; 5★ from 43 reviews; "Only 10 left" at the deal price. Higher resolution than the 800×480 board and cheaper delivered. |

### Searched, nothing found or blocked

- **No listing found:** Pimoroni, Coolcomponents, Welectron, Antratek, DigiKey UK and Banggood had no listing for either board. The Pi Hut has no JC1060P470.
- **Not checked:** Amazon UK and eBay UK blocked the automated fetches, so they still need checking by hand.

## Fit with this repo

| Board | Code reuse from `common/lib/Board4848S040` |
|---|---|
| Waveshare ESP32-S3-Touch-LCD-7 / 7B | High. Same ESP32-S3 with RGB panel and GT911 touch approach; new pins and timings, and the backlight and reset go through a CH422G IO expander. The Powerwall UI would need re-laying out for the wider screen. |
| Guition JC1060P470 | Low. ESP32-P4 uses a MIPI-DSI display, needs a newer ESP-IDF or Arduino core, and gets Wi-Fi through an ESP32-C6 co-processor (esp-hosted). It is much faster and has a 2D accelerator, but needs a new board layer. |

## Overview of similar boards (unverified prices)

Rough AliExpress prices from memory. **Check them before buying.**

### Closest to the 4848S040 (ESP32-S3, RGB panel, GT911 touch)

| Board | Screen | ~Price | Notes |
|---|---|---|---|
| Sunton/Guition ESP32-8048S043C | 4.3" 800×480 IPS | $25–35 | Popular. Get the `C` suffix for capacitive touch. |
| Sunton ESP32-8048S050C / 8048S070C | 5" / 7" 800×480 | $30–45 | The 7" uses a TN panel with worse viewing angles. |
| Waveshare ESP32-S3-Touch-LCD-4 | 4" 480×480 | $30–35 | Same class of panel as the 4848S040, with better docs, CAN/RS485 and an RTC. |
| Waveshare ESP32-S3-Touch-LCD-4.3 / 7 | 800×480 / 1024×600 | $30–45 | Good docs and examples. |
| Elecrow CrowPanel ESP32-S3 HMI 4.3/5/7" | 480×272 to 800×480 | $25–40 | Good LVGL tutorials; cases available. |
| Seeed SenseCAP Indicator D1 | 4" 480×480 | $45–60 | Enclosure included; RP2040 co-processor. |

### Cheaper or smaller (different display drivers)

| Board | Screen | ~Price | Notes |
|---|---|---|---|
| ESP32-2432S028R ("Cheap Yellow Display") | 2.8" 320×240 SPI | $12–15 | Classic ESP32, resistive touch, no PSRAM. Large community. |
| ESP32-3248S035C | 3.5" 480×320 SPI | $15–20 | Classic ESP32, capacitive touch. |
| Guition JC3248W535 | 3.5" 320×480 QSPI | $15–20 | S3 with PSRAM; the AXS15231B controller is quirky. |
| Guition JC4827W543 | 4.3" 480×272 QSPI | $15–20 | S3 with the NV3041A controller. |
| LilyGO T-Display-S3 | 1.9" 170×320 | $15–20 | Stick form factor. |

### Step up

| Board | Screen | ~Price | Notes |
|---|---|---|---|
| Guition JC1060P470 / Waveshare ESP32-P4 boards | 7" 1024×600 MIPI-DSI | see above | Verified pricing is in the section above. |
| LilyGO T-RGB | 2.1" round 480×480 | ~$40 | Suits gauge-style UIs. |
