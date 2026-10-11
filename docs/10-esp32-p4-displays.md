# 10 – ESP32-P4 display boards (UK buying options)

Cheapest ESP32-P4 boards with a built-in touchscreen that can be bought in the UK, checked on **2026-10-11**. This follows on from [09 – Alternative display boards](09-alternative-displays.md), which already covers the Guition JC1060P470 (7") in its "Verified UK pricing" section. Prices for that board are refreshed here.

## Summary

- **Cheapest landed:** Guition **JC4880P443C-I-W**, a 4.3" 480x800 MIPI-DSI IPS panel with GT911 touch, ESP32-P4 and an ESP32-C6 for Wi-Fi. It costs **£25.92** delivered on AliExpress ([Guition Store listing](https://www.aliexpress.com/item/1005009621821443.html)). The same board from the [GUITION Official Store](https://www.aliexpress.com/item/1005009618259341.html) is **£25.95**, with 109 reviews at 4.7 stars. Both prices include UK VAT with free shipping, estimated delivery Oct 16-19, and nothing more to pay on arrival. I would pick the official-store listing for a £0.03 difference.
- **Best value for the stated goal (a bigger, faster screen):** the Guition **JC1060P470C-I-W-Y**, 7" 1024x600, at **£42.22** delivered ([GUITION Official Store](https://www.aliexpress.com/item/1005010022828767.html)). The 4.3" panel is only slightly larger than the current 4" board, and its 480x800 shape suits a portrait UI better than landscape. The 7" gives about 3.5 times the pixels of the current 480x480 panel and ships with a case and camera.
- **Cheapest UK-stock option (Amazon UK, no import step, returns handled by Amazon):** £28.29 for an unbranded 4.3" board. It is generic (brand "Evenoonest"), so I could not confirm the exact model, and I could not verify the panel or touch chip.
- **Not worth it for price:** M5Stack Tab5 (£64.80 at The Pi Hut, sold out; £78.99 on AliExpress), the 4D Systems 7" (£113.33 at RS UK), and the LilyGO T-Display-P4 (about $97-119, sold out).

## How the prices were checked

- Every price in the tables was read from the product page in Chrome on 2026-10-11, unless the notes column says otherwise. Pages that blocked the fetch are listed under "Not verified" below.
- AliExpress prices change within hours (there was a "Brand day" sale running while I checked). The browser session was whatever Chrome had open and I did not confirm whether it was signed in, so a signed-in or coupon price may differ slightly. Doc 09 recorded £44.49 for the 7" Guition when signed in; today the same listing showed **£42.22**.
- AliExpress GBP prices say "Price includes VAT", so nothing more is due on delivery. Waveshare's own store does not collect UK VAT: its [shipping terms](https://www.waveshare.com/shipping) say customers are responsible for local taxes, duties and fees. For a Waveshare direct order I treat the GBP price as ex-VAT, so 20% import VAT applies plus any courier handling fee. Waveshare's shipping cost to the UK is only calculated at checkout (it needs an address), so I did not capture it.
- UK retailers (The Pi Hut, Amazon UK, RS) show prices including VAT, except RS, which shows both.
- USD to GBP uses 0.7557 (xe.com mid-market, 2026-10-11 01:21 UTC). Only M5Stack's $60 needed converting.

## Comparison table (sorted by landed price)

All boards have an ESP32-P4 with 32 MB PSRAM in the package. Wireless comes from an ESP32-C6 over SDIO using esp_hosted, because the P4 has no radio. "Landed" means item plus shipping plus VAT to a UK address.

| Landed | Board | Screen | Touch | Seller | Stock / delivery | Link |
|---|---|---|---|---|---|---|
| **£25.75 + shipping + 20% VAT (about £30.90+)** | Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 (SKU 33874, no camera) | 4.3" 480x800 IPS, MIPI-DSI 2-lane, ST7701 | GT911, 5-point | Waveshare direct | Add to cart shown (no stock count); shipping not captured | [page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm) |
| **£25.92** | Guition JC4880P443C-I-W | 4.3" 480x800 IPS, MIPI-DSI | not stated on listing | Guition Store (AliExpress) | Free shipping, Oct 16-19; 341 sold | [item](https://www.aliexpress.com/item/1005009621821443.html) |
| **£25.95** | Guition JC4880P443C-I-W | as above | as above | GUITION Official Store | Free shipping, Oct 16-19; 1,000+ sold, 4.7 stars from 109 reviews | [item](https://www.aliexpress.com/item/1005009618259341.html) |
| £28.29 | "Evenoonest ESP32-P4 Development Board", 4.3" IPS | 4.3", model unconfirmed | not stated | Amazon UK, seller easynewstar | In stock, delivery 24-29 Oct | [B0HH9R3ZS1](https://www.amazon.co.uk/dp/B0HH9R3ZS1) |
| £28.70 | Guition JC4880P443C-I-W | as above | as above | Guition Factory Store | Free shipping, Oct 16-19; "Only 20 left" | [item](https://www.aliexpress.com/item/1005010425057615.html) |
| £30.30+ (+ shipping + VAT) | Waveshare ESP32-P4 Smart 86 Box (4" 720x720) | 4" 720x720, square, MIPI-DSI | 5-point | Waveshare direct | **Out of stock** on the page; price shown as a range £30.30-£37.87 | [page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm) |
| £31.32 | Guition JC4880P443 with shell + camera | 4.3" 480x800 | as above | GUITION Official Store | Free shipping, Oct 16-19 | [item](https://www.aliexpress.com/item/1005009673625472.html) |
| £33.35 (£27.39 + £5.96) | Guition JC4880P443C, no case/camera | 4.3" 480x800 | as above | SURENOO Official Store | Standard shipping £5.96, Oct 18-22 | [item](https://www.aliexpress.com/item/1005009623494651.html) |
| £35.99 | "ZYWUOY ESP32-P4 Development Board", 4.3" | 4.3", model unconfirmed | not stated | Amazon UK, seller Carpango | In stock, delivery 19-24 Oct | [B0HH8N5BJB](https://www.amazon.co.uk/dp/B0HH8N5BJB) |
| £35.60+ (+ shipping + VAT, about £42.72+) | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B | 7" 1024x600 IPS, MIPI-DSI | GT911, 5-point | Waveshare direct | Page shows a price range £35.60-£42.42 (camera option); standard version assumed to be the low end | [page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm) |
| £34.08+ (+ shipping + VAT, about £40.90+) | Waveshare ESP32-P4-WIFI6-Touch-LCD-5 | 5" 720x1280 IPS, MIPI-DSI, HX8394 | GT911, 5-point | Waveshare direct | Range £34.08-£40.15 (camera option) | [page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-5.htm) |
| £28.78+ (+ shipping + VAT) | Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 | 3.5" 320x480, **SPI** (ST7796), not MIPI-DSI | FT6336 | Waveshare direct | Range £28.78-£30.30 (battery option) | [page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-3.5.htm) |
| £40.31 | Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 via Amazon UK | 4.3" 480x800 | GT911 | Amazon UK, seller Bolin Electronic | In stock, delivery 23-28 Oct | [B0GV42GCRQ](https://www.amazon.co.uk/dp/B0GV42GCRQ) |
| **£42.22** | Guition JC1060P470C-I-W-Y, with housing and camera | 7" 1024x600 IPS, MIPI-DSI | not stated on listing | GUITION Official Store | Free shipping, Oct 16-19 | [item](https://www.aliexpress.com/item/1005010022828767.html) |
| £64.80 | M5Stack Tab5 kit (with battery) | 5" 1280x720 IPS | not checked | The Pi Hut | **Sold out** ("Tab5 Only" option greyed out) | [page](https://thepihut.com/products/m5stack-tab5) |
| £62.40 | Waveshare ESP32-P4 3.4" round (800x800) | 3.4" round IPS, MIPI-DSI | GT9271, 10-point | The Pi Hut | **Sold out** | [page](https://thepihut.com/products/esp32-p4-3-4-round-touch-display-dev-board-800x800) |
| £78.99 | M5Stack Tab5, no battery | 5" 1280x720 | not checked | FMchip AE Store (AliExpress) | Free shipping, Oct 16-19; 11 available | [item](https://www.aliexpress.com/item/1005013031631176.html) |
| about £54 + shipping | M5Stack Tab5 | 5" | not checked | M5Stack's own store | **Out of stock**; $60 x 0.7557 = £45.34, +20% VAT = £54.41; shipping calculated at checkout | [page](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4) |
| £113.33 (£94.44 ex VAT) | 4D Systems ESP32-P4-70 | 7" 800x1280 | not checked | RS UK | Temporarily out of stock, shipping from 12 Oct; free delivery over £60 | [page](https://uk.rs-online.com/web/p/lcd-colour-displays/0800328) |

Notes on the table:

- The search snippet for the RS 4D Systems board said £78.05 ex VAT; the live page says **£94.44**. Search-engine prices were stale in several other cases too.
- Waveshare "+" prices are the low end of the range shown on the page, which I assumed to be the standard (no camera / no battery) variant. For the 4.3" I selected the "NOT Included" camera option and the page showed £25.75 (SKU 33874). For the others I did not select the variants individually.
- AliExpress listings show a coupon prompt for new users and "extra 2% off with coins" on some. I did not apply any.

### Not verified (blocked or only seen in search results)

| Item | Price seen | Why not verified |
|---|---|---|
| Amazon UK: Elecrow CrowPanel Advanced 7" ESP32-P4 (B0G34ZXVZ6) | £64.99 | Seen on the search results page only |
| Amazon UK: Elecrow 10.1" ESP32-P4 (B0GCJVLP7B) | £71.24 | Search results page only |
| Amazon UK: Waveshare 7" ESP32-P4 (B0FZC1PXN2 / B0FSXGK4TG) | £55.68 / £57.99 | Search results page only |
| Amazon UK: Waveshare 4.3" (B0GQB5PY1X) | £41.99 | Search results page only |
| Amazon UK: DIYmalls 4.3" with case (B0FX9XM87X) | £49.97 | Search results page only |
| eBay UK: 4.3" ESP32-P4 with camera (seller allthings10) | £52.49 | Search results page only |
| Elecrow own store, CrowPanel Advance 7" | $45.90 (about £34.69 before VAT) | CNX Software snippet; the store search redirected to the home page |
| LilyGO T-Display-P4 | about $97 (TFT) to $119 (AMOLED) | Search snippets say sold out; not opened |
| Mouser UK, Farnell UK, DigiKey UK | n/a | Mouser returned an empty page, Farnell timed out, DigiKey not reached. They are not typical for these hobby boards in any case. |

Nothing was found at **Pimoroni** (0 results for "esp32-p4") or **Cool Components** (0 results; its page showed "We are closed"). The Pi Hut has the Waveshare 3.4" round board and the Tab5 (both sold out) but no 4.3" or 7" P4 display; its search page returned nothing for "esp32-p4".

## Board notes

### Guition JC4880P443C-I-W (4.3", 480x800) - cheapest

- **Screen:** 4.3" IPS, 480x800 native portrait (13 x 10 x 4 cm package, 0.35 kg). The listing says contrast 300:1. The same panel size on Waveshare's board is ST7701 with GT911 touch, 2-lane MIPI-DSI; I did **not** verify that Guition uses the identical panel and touch chip, but reviews say it initialises as a MIPI-DSI display and one reviewer notes the demo "works out of the box".
- **Chip:** ESP32-P4 with 32 MB PSRAM ("32MB PSRAM" on the listing image); ESP32-C6 for Wi-Fi 6 / BLE 5. Flash size is not stated on the listing.
- **Extras:** camera and speaker connectors (the listing says "supports external camera + speaker"), microSD slot (user manual PDF is linked from the listing). The "C-I-W" suffix means capacitive touch (C), IPS (I), Wi-Fi (W). The 'with shell and camera' variant is £31.32 on the official store.
- **Pros:** the cheapest option by about £5 against anything else with a screen, UK VAT included, free shipping. Manufacturer store with decent review volume.
- **Cons:** no enclosure at this price. Portrait panel: landscape needs software rotation, so a 800x480 UI loses some of the hardware advantage. One reviewer says there is "no documentation" (5 reviews mention this) while another says lots is provided; expect to dig through Guition's PDF and GitHub.
- **Software:** Guition advertises its design tool, LVGL, Arduino and ESP-IDF for the JC1060P470 (see the CNX Software article on that board); I did not confirm the JC4880P443 has the same package.

### Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 (and 5", 7B) - best documented

- **4.3":** 480x800 IPS, 500 cd/m2, ST7701, GT911, 6H glass with optical bonding, 32 MB PSRAM in the package, 32 MB NOR flash, ESP32-C6-MINI over SDIO, ES8311 audio codec plus ES7210 echo cancellation, dual microphones, microSD, LiPo header with charging, 40-pin header. £25.75 on the Waveshare site, but UK buyers pay import VAT and any courier handling on top.
- **5":** 720x1280 IPS, HX8394 driver, GT911. **7B:** 7" 1024x600, GT911.
- **Pros:** the best vendor documentation of the lot (wiki at docs.waveshare.com, ESP-IDF and Arduino examples are linked from the product pages), a lot of onboard extras (audio, battery, camera).
- **Cons:** import VAT and shipping make it dearer than the AliExpress Guition listings once landed, and Waveshare's shipping cost for the UK is only shown at checkout. The same board costs £40.31 via an Amazon UK third party (Bolin Electronic).
- **Smart 86 Box (4", 720x720):** the nearest match for the current 480x480 square panel with much higher density. Listed at £30.30-£37.87 but **out of stock** on Waveshare today; it also has Ethernet, RS485 and a 2-channel relay on one variant.

### Guition JC1060P470 (7", 1024x600) - best value for a bigger screen

- £42.22 delivered today from the GUITION Official Store. This listing includes housing and camera. Doc 09 has more sellers and the "N" (no-touch) warning.
- **Pros:** the largest screen per pound, about 3.5 times the pixels of the current 4" board, case included.
- **Cons:** the full-width 7" is a big layout change, and touch and panel controller names are not stated on the listing (doc 09 records the board as ESP32-C6 for Wi-Fi).
- A Waveshare 7B at £35.60 plus shipping and 20% import VAT lands around £43 or higher, so there is no price advantage over the Guition, but Waveshare has the better documentation.

### M5Stack Tab5 (5", 1280x720)

- ESP32-P4 with ESP32-C6-MINI-1U, 16 MB flash, 32 MB PSRAM (M5Stack store page).
- £64.80 at The Pi Hut (sold out), M5Stack's own store out of stock, £78.99 on AliExpress from a reseller. A polished, well-supported device but about three times the price of the Guition 4.3" board.

### 4D Systems ESP32-P4-70 (RS UK)

- 7" 800x1280, £94.44 ex VAT / £113.33 inc VAT, temporarily out of stock. Aimed at industrial users with the Workshop IDE; not a hobby-price option.

### Others not worth chasing today

- **Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5:** its display is SPI (ST7796) with FT6336 touch, so it does not exercise MIPI-DSI at all.
- **Waveshare 3.4" round, Elecrow CrowPanel Advance (5", 7", 9", 10.1"), LilyGO T-Display-P4:** more expensive, sold out or only found on search pages, as above.
- **Espressif ESP32-P4-Function-EV-Board:** no UK price found (only USD/EUR snippets of about $55 / EUR 65 at launch from AliExpress, and a DigiKey lead time of 16 weeks in one snippet). Not verified.

## Software support (state as of this check)

What I could confirm from the sources I opened:

- **Arduino core:** the arduino-esp32 3.3.0-rc1 readme on the Espressif component registry names the ESP32-P4 among its supported chips, and the 3.1.0 readme does too. Wi-Fi on the P4 goes through the C6 co-processor over esp_hosted. A forum thread on a Waveshare P4 board using pioarduino (Arduino 3.3.4) reports a warning that the host firmware is newer than the co-processor's, but Wi-Fi and a web server still worked. That thread says the hosted binaries are bundled in the core and pushed to the C6 at boot; I could not confirm that this applies to the official core.
- **ESP-IDF:** Espressif documents multiple P4 silicon revisions (a v3.x user guide dated 2026-03 exists) and says newer silicon can be incompatible with older ESP-IDF releases. Boards on sale now may carry v3.x silicon, so use a current ESP-IDF 5.5.x or newer. Which exact release first supports v3.0 was not confirmed.
- **ESP-IDF BSP and LVGL:** Espressif publishes `esp32_p4_function_ev_board` on the component registry (EK79007 panel plus GT911 via `esp_lcd_touch_gt911`, LVGL through `esp_lvgl_port`). It is the closest official reference for the same touch chip; the 4.3" boards use ST7701 instead, for which `esp_lcd_st7701` is the usual driver.
- **Waveshare:** product pages link a wiki per board with ESP-IDF and Arduino demos; I did not read the wikis.

Not verified: Guition's own demo repository, its panel init sequence stability (one AliExpress reviewer wrote that the MIPI DSI panel "has issues with the initialization sequence"), and PlatformIO support for P4 on the pioarduino version this repo uses.

## How this compares to our current S3 board

Current board: Guition ESP32-4848S040C_I, ESP32-S3 N16R8, 4" 480x480 RGB panel (ST7701S), GT911, about £19 on AliExpress (a 4.0" 480x480 ESP32-S3 listing at £19.17 showed up in the related items while checking).

| Aspect | S3 board (today) | P4 boards |
|---|---|---|
| Display bus | 16-bit parallel RGB, PSRAM framebuffer with bounce buffers | MIPI-DSI 2-lane. No RGB pin juggling and no bounce-buffer workaround, but a different driver stack (`esp_lcd` DSI) |
| CPU | Dual Xtensa LX7, 240 MHz | Dual RISC-V HP cores up to 400 MHz (Waveshare's pages say 360-400 MHz depending on the page) plus a 40 MHz LP core |
| Memory | 8 MB octal PSRAM, 512 KB SRAM | 32 MB PSRAM in the package, 768 KB HP L2 memory; flash 16-32 MB depending on the board |
| Graphics help | none | Pixel Processing Accelerator (PPA), JPEG codec, H.264 encoder, image signal processor (per Waveshare's feature list) |
| Wireless | on-chip Wi-Fi 4 / BLE | **none on the P4**; ESP32-C6 over SDIO (Wi-Fi 6 / BLE 5) via esp_hosted |
| Console / flashing | CH340C on UART0 | USB-to-UART Type-C port plus a separate USB OTG 2.0 HS port (per Waveshare's pages; confirm for Guition) |
| Touch | GT911 | GT911 on the 4.3", 5", 7B and Guition 7"; Guition 4.3" not confirmed |

What carries over:

- **LVGL UI code: yes.** The screens, `ui_common`, animations and pill widgets in project 05 are LVGL 9.5 and independent of the panel. They would need re-laying out for the new aspect ratio (480x800 portrait, 1024x600, or 720x720), because layout is hard-coded for 480x480.
- **Network logic in `powerwall.cpp`: mostly.** The Powerwall API calls, the Open-Meteo fetch, TLS handling and the "one TLS session at a time" rule are the same ideas, but Wi-Fi bring-up goes through esp_hosted instead of the on-chip radio. The earlier heap-pressure notes (mbedTLS needing about 50 KB internal RAM) need rechecking, because the memory map differs.
- **GT911 touch handling: largely.** The I2C protocol is the same. The pins, reset and interrupt wiring differ per board.
- **`energy.cpp` RTC memory, SD card history:** RTC memory exists on the P4 (LP SRAM and RTC registers behave differently from the S3, so this needs rework). The "no internal-flash writes" rule still applies; every board above has a microSD slot, so the SD plan fits.

What does not carry over:

- **`common/lib/Board4848S040`: no.** The pin map, the RGB display init, the ST7701 3-wire SPI sequence and the LEDC backlight code are S3/RGB specific. A new `BoardP4xxx` library (MIPI-DSI display, touch, LVGL glue) would be needed, and `Board4848S040_pins.h` stays the only pin source for the existing board.
- **`common/platformio-base.ini` assumptions:** `memory_type = qio_opi`, `ARDUINO_USB_CDC_ON_BOOT=0`, the S3 octal PSRAM settings and the 120-line LVGL buffer measurements do not apply to the P4. The measured performance defaults would have to be re-benchmarked with `03-lvgl-demos`.
- **Toolchain:** the repo currently pins PlatformIO Core 6.1.x with pioarduino `54.03.21` (Arduino core 3.2.1, IDF 5.4). I did not confirm that this combination works for a current P4 board; the P4 silicon notes above suggest a newer core/IDF, which per CLAUDE.md needs PlatformIO Core 6.2 or newer. Check this before buying.
- **Arduino vs ESP-IDF:** [docs/06](06-frameworks-and-libraries.md) leaves this undecided. The P4 does not decide it either, but esp_hosted and DSI are better trodden in ESP-IDF.

Suggested next step if you want to try one: buy the cheapest 4.3" Guition (£25.95) purely as a P4 / MIPI-DSI / esp_hosted learning board, or the Guition 7" (£42.22) if you actually want the bigger UI. Either way, run Guition's factory demo first and back up the flash (`tools/backup-flash.ps1` is S3-specific, so the P4 needs its own esptool command).
