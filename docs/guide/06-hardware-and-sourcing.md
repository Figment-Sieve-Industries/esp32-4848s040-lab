# 06. Hardware and sourcing

[Guide index](README.md) | [01 Quirks](01-quirks-and-gotchas.md) | [02 Debugging](02-debugging-playbook.md) | [03 Investigations](03-investigations.md) | [04 Patterns](04-patterns-and-decisions.md) | [05 New project](05-new-project-checklist.md) | [06 Hardware](06-hardware-and-sourcing.md) | [Credits](99-sources-and-credits.md)

What the board is, what else you need, and where to buy it. Prices were checked on **2026-10-11**. They change often, so treat every figure as a snapshot and check the listing before you order.

## The board

The Guition **ESP32-4848S040** is a small all-in-one touch display module. The silkscreen reads `ESP32-4848S040`. It is also sold under the Sunton and JCZN names, and the same design is often listed with "86 box" or "smart switch" in the title (the module is about 86 x 86 mm, the size of a standard wall box).

The repo targets the **ESP32-4848S040C_I** (capacitive touch, most likely an IPS panel). The full part list, connectors and photos are in [01 Hardware overview](../01-hardware-overview.md); the pin map is in [02 Pinout](../02-pinout.md).

### Variants

| Suffix | Meaning | Notes |
|---|---|---|
| `ESP32-4848S040` | Family name | Used by most sellers in the title or as the "colour" option. |
| `C` | Capacitive touch (GT911) | `N` would be no touch and `R` resistive. The repo needs `C`. |
| `_I` | Not defined by the vendor; most likely IPS | Every SKU in the vendor data pack is `_I`. |
| `_Y_1` / `_Y_3` | Version with 1 or 3 mains relays | Has an on-board 110-240 V AC supply on a daughterboard. Called `CIY1` / `CIY3` in the PlatformIO board files in [`common/boards/`](../../common/boards/). |
| (no `_Y`) | No relay board | The cheap, safe option for a bench or desk display. |
| With or without shell | Wall-box case or bare board | Some listings say "without bottom shell". The bare board is usually the cheapest. |

Relay versions carry mains voltage. Do not have mains connected while USB is plugged in and the case is open. See the mains warning in [01](../01-hardware-overview.md).

Listings often get the metadata wrong (several show "480x800" as the resolution). Check the title, the photos and the colour or model option, not the specification table.

### Key specifications

| Item | Value |
|---|---|
| SoC / module | ESP32-S3, module `ESP32-S3-WROOM-1U-N16R8` per the schematic (external antenna connector) |
| Flash / PSRAM | 16 MB quad flash, 8 MB octal PSRAM (`qio_opi`) |
| Display | 4.0" TFT (likely IPS), 480 x 480, ST7701S driver, 16-bit RGB bus plus 3-wire SPI for setup |
| Touch | GT911, I2C, capacitive (see [05](../05-touch-gt911.md)) |
| Storage | microSD slot, SPI mode only, no card-detect pin |
| USB | USB-C to a CH340C USB-UART (UART0). Native USB pins are used by the display and touch, so the console is the CH340C |
| Audio | NS4168 I2S class-D mono amplifier, `Speak` connector |
| Power | 5 V over USB-C; also a 1S Li-ion/LiPo charger and boost on the `BAT` connector (see [08](../08-power-battery-sleep.md)) |
| Wireless | 2.4 GHz Wi-Fi and Bluetooth LE (ESP32-S3) |
| Spare I/O | Very little. Several pins are shared between relays, audio and a possible second I2C bus (see [02](../02-pinout.md)) |

## What else you need

| Item | Notes |
|---|---|
| USB-C cable (data, not charge-only) | The CH340C appears as a serial port. Auto-reset works, so uploads need no button presses. If no port appears, try another cable. Windows may need the CH340 driver from the maker, WCH: <https://www.wch-ic.com/downloads/CH341SER_EXE.html> |
| A computer with PlatformIO | See [03 Programming and flashing](../03-programming-and-flashing.md). Run the tools from PowerShell on Windows, not Git Bash. |
| 5 V supply | A phone charger or a computer port is enough. The display backlight is the main load. |
| microSD card (optional) | See below. |
| 2.4 GHz Wi-Fi network | The ESP32-S3 does not do 5 GHz. |
| Li-ion cell, speaker, relay board (all optional) | Not used by the projects in this repo. The battery and speaker connectors are 1.25 mm JST. |

Back up the factory flash before the first upload (`tools/backup-flash.ps1`). The vendor demo images are listed in [`firmware/README.md`](../../firmware/README.md).

### microSD cards

The slot runs in SPI mode on a bus shared with the display's setup lines, so it works only after the display is initialised. It tunes the SPI clock to each card. The test bench is [`projects/06-sd-card-test`](../../projects/06-sd-card-test/README.md), and the results are in the [hardware log](../hardware-log.md).

| Card | Result |
|---|---|
| 16 GB SDHC, class 10 / U1, made 2014 (CID maker `JT`, product `NCard`) | **Worked.** 19 of 19 tests at 20-25 MHz; 1 MB write about 670 KB/s, read about 1.47 MB/s |
| 2 GB SanDisk SU02G SDSC, made 2011, no speed class | **Failed.** Corrupt responses above 4 MHz and flaky at 4 MHz. Retired |

Advice from those two cards (a sample of two, not a compatibility list):

- Use a **plain SDHC card of 32 GB or less**, formatted FAT32. The firmware never formats a card.
- Old, small SDSC cards are the likely problem. A recent name-brand SDHC card is the safe choice, but it has not been tested here.
- The stock Arduino-ESP32 3.2.1 `SD` library failed multi-block writes intermittently on every card at every clock. The repo carries a patched copy; see [`common/lib/SD/README-mini-screen.md`](../../common/lib/SD/README-mini-screen.md). Do not blame the card before using it.
- Pulling and re-inserting a card while running works, but inserting a card can briefly drop the PC's USB-serial link.

## Where to buy

Check **model, variant and photos**, not just the headline. Prices are in the listing currency, include UK VAT where stated, and were read in a desktop browser with the delivery country set to the UK. AliExpress prices depend on the viewer, sales and coupons.

### Verified listings (opened, live on 2026-10-11)

| # | Seller / store | Listing | Price (2026-10-11) | Postage | Notes |
|---|---|---|---|---|---|
| 1 | **eBay UK: AceHub_01** (business seller, Hull) | [eBay item 147605654410](https://www.ebay.co.uk/itm/147605654410) | £19.69 or best offer | Free (Evri) | **The unit used for this repo.** Title: "ESP32-S3 Development Board WiFi BT 4 Inch 480 * 480 smart display capacitive tou..." (truncated by eBay). Item specifics give model `ESP32-4848S040`. Was live on the check date with "last one" showing, so it may have ended by the time you read this. |
| 2 | eBay UK: chaseemart (London) | [eBay item 318880502260](https://www.ebay.co.uk/itm/318880502260) | £19.65 | Free (3 to 5 days) | Title "ESP32-S3 Development Board WiFi BT 4 Inch IPS Touch Screen for Arduino LVGL IOT". Colour option `ESP32-4848S040`. 2 available. Returns accepted. |
| 3 | eBay UK: Elite-mart uk001 (business, London) | [eBay item 820001900906](https://www.ebay.co.uk/itm/820001900906) | £21.99 | Free (3 to 5 days) | Same title and colour option as #2. 3 available. |
| 4 | **AliExpress: GUITION Official Store** | [Item 1005008214679682](https://www.aliexpress.com/item/1005008214679682.html) | £19.17 (list £20.18) | Free | Manufacturer's own store. "...4.0 inch 480 * 480 smart display capacitive touch without bottom shell". Option `ESP32-4848S040`, bare board. 10,000+ sold, 4.8 stars from 384 reviews. Delivery estimate 16-19 Oct. |
| 5 | AliExpress: DIYmalls Store | [Item 1005012911942193](https://www.aliexpress.com/item/1005012911942193.html) | £28.52 (list £31.60) | Free | Title names `ESP32-4848S040C_I` exactly. 35 sold, 1 review. Delivery estimate 18-25 Oct. Dearer than the manufacturer's store. |
| 6 | AliExpress: Nawei Store | [Item 1005008506761923](https://www.aliexpress.com/item/1005008506761923.html) | £25.76 (list £27.12) | Free | "4 Inch IPS ... 86 Box" listing. The title does not name the model, but the photos and specification match (480 x 480, GT911, ESP32-S3-N16R8, 86.5 x 86.5 x 12 mm). 198 sold, 9 reviews. |
| 7 | AliExpress: STEM Hub Store | [Item 1005012936078722](https://www.aliexpress.com/item/1005012936078722.html) | £43.99 (struck-through £87.92) | Free | Title names `ESP32-4848S040C_I`. "Only 1 left" on the check date. Listed for completeness; the price is about double the others. |

Notes on the table:

- eBay prices and stock change daily and the "last one" listing in #1 will go. #2 and #3 are the same board at the same price class from other UK sellers.
- AliExpress items ship from China. The prices above include UK VAT and showed free shipping, so nothing should be due on delivery, but check at checkout.
- Sellers sometimes list several boards under one title with a "colour" or model option. Select `ESP32-4848S040` (not another module).
- Only some titles say "IPS". The vendor documents only say "TFT", so treat IPS as likely, not guaranteed.

### Not found, not verified, or a different board

| Source | Result |
|---|---|
| Amazon UK | Searched "ESP32-4848S040" and a descriptive 4 inch 480 x 480 query on 2026-10-11. **No Guition 4848S040 listing found.** The nearest are Waveshare's own 4 inch 480 x 480 board (about £39.99; a different pin map and drivers, so the repo code does not run unchanged) and a Freenove 4 inch 320 x 480 board (different panel). |
| Amazon US | Search found one 4 inch 480 x 480 ESP32-S3 module from "DORHEA" with a temperature and humidity sensor (ASIN B0GCMXHQ8M). **Not confirmed as the Guition 4848S040**; it does not name the model, and the sensor suggests another design. Not recommended without checking the pinout. |
| Tindie (Makerfabs "ESP32-S3 Parallel TFT with Touch 4.0 480x480") | Seen in a web search only; **not opened and not verified**. It is a different Makerfabs board, not the Guition unit. |
| Guition's own site and data pack | The vendor data pack link is in [`docs/README.md`](../README.md). It is a slow server and was last checked on 2026-10-08. |
| An AliExpress link in the PlatformIO board files | `common/boards/esp32-4848S040CIY1.json` and `...CIY3.json` carry a vendor URL (item 1005006302276974) that returned "page not found" on 2026-10-11. |

### Other boards

If you want a bigger screen, a different driver, or a newer chip, see:

- [09 Alternative display boards](../09-alternative-displays.md): ESP32-S3 boards with the same RGB panel and GT911 approach (Waveshare 4 inch, 4.3 inch and 7 inch), and Guition's larger panels, with UK pricing.
- [10 ESP32-P4 display boards](../10-esp32-p4-displays.md): MIPI-DSI boards with an ESP32-P4. The repo's board library will not run on them unchanged.

## Before you order

1. Decide on relays. For a display project you almost certainly want the **no-relay** version.
2. Prefer a listing that shows the exact model in the title or option, the 4 inch square front, and the back of the board with the TF slot, USB-C, and the 1.25 mm `BAT` and `Speak` connectors.
3. Confirm that it is capacitive touch (GT911). A resistive or no-touch variant will not match the repo's touch code.
4. Keep the receipt and photos. Revisions differ slightly between sellers (module marking, which 0 ohm links are fitted, backlight driver part).
