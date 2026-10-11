# Development guide

A growing development guide for the **Guition ESP32-4848S040** and similar ESP32-S3 boards with an RGB-panel display. It is written from measurements and fixes on a real unit (ESP32-S3 N16R8, 480x480 ST7701 panel, GT911 touch, microSD, CH340C) with PlatformIO and pioarduino (Arduino core 3.2.1, IDF 5.4) and LVGL 9.5. The aim is that the next project, or the next person, starts from what was measured rather than from guesses.

Most of the networking and memory material comes from [project 05](../../projects/05-powerwall-display/README.md), a Wi-Fi and TLS dashboard, but it applies to any ESP32-S3 project with a PSRAM framebuffer, LVGL and HTTPS.

All results are from one board ("unit 1"). A hardware result is claimed only where a log or the owner confirmed it; everything else is marked as unverified or inferred.

## How to use it

| You want to | Start here |
|---|---|
| Start a new project, or port to another board | [05 New project checklist](05-new-project-checklist.md), in order |
| Fix something that is going wrong | The symptom table in [02 Debugging playbook](02-debugging-playbook.md#symptom-to-first-method), which points to the cheapest method that separates the likely causes |
| Check whether a problem is already known | [01 Quirks and gotchas](01-quirks-and-gotchas.md): look up the area (HW, GFX, TOUCH, SD, MEM, LVGL, PERF, NET, PW, DATA, FW, TOOL), read the entry, apply the fix |
| See what was measured, and what was tried and dropped | [03 Investigations](03-investigations.md): before and after numbers, status, open questions |
| Understand why the code is the way it is | [04 Patterns and decisions](04-patterns-and-decisions.md): each decision with its alternatives and consequences |
| Buy a board, pick a variant or a microSD card | [06 Hardware and sourcing](06-hardware-and-sourcing.md) |
| Check a licence or credit a source | [99 Sources and credits](99-sources-and-credits.md) |

## Chapters

| Chapter | What it covers |
|---|---|
| [01 Quirks and gotchas](01-quirks-and-gotchas.md) | A catalogue of hardware, display, touch, SD, memory, LVGL, performance, network, Powerwall API, public data source, firmware-pattern and toolchain quirks. Each has an ID, symptom, root cause, fix, evidence and status |
| [02 Debugging playbook](02-debugging-playbook.md) | Twenty methods that were used here (serial heartbeat, boot memory trace, A/B builds, backtrace decoding, serial capture, screenshots, soak tests, host tests, curl and openssl checks, lint, SD test bench, flash backup), each with the commands, what it told us and its traps |
| [03 Investigations](03-investigations.md) | One write-up per real debugging case (flicker, backlight PWM, performance defaults, TLS memory, task watchdog, SD multi-block writes, crackle, stutter and more), plus the performance baseline history and ideas tried and rejected |
| [04 Patterns and decisions](04-patterns-and-decisions.md) | Architecture decision records: shared library, task and core layout, memory strategy, display pipeline, networking, storage |
| [05 New project checklist](05-new-project-checklist.md) | Ordered steps from toolchain to commit, the defaults to keep, a pre-flight table, and notes on porting to another board |
| [06 Hardware and sourcing](06-hardware-and-sourcing.md) | The board and its variants, key specifications, what else you need, microSD advice and where to buy |
| [99 Sources and credits](99-sources-and-credits.md) | Every vendor document, reference repository, library, font, data source and tool used, with licence or terms |

Related documents outside the guide: the [hardware docs](../README.md) (pinout, display and touch details, flashing), the [hardware log](../hardware-log.md) (measurements) and the [research notes](../11-space-weather-and-climate-data.md) behind the data sources.

## Adding to the guide

Keep entries short, factual and dated, and say how sure you are. Use the repository's status words (`idea`, `wip`, `builds`, `works on hw`, `done`) for projects; each chapter defines its own status words for entries. Never put Wi-Fi names, passwords, tokens, MAC addresses, real IP addresses, personal names or your location in an entry: use `<home-ssid>`, `192.0.2.x` documentation addresses and similar placeholders, and trim serial logs before quoting them.

### A new quirk (chapter 01)

1. Pick the area prefix: `HW`, `GFX`, `TOUCH`, `SD`, `MEM`, `LVGL`, `PERF`, `NET`, `PW`, `DATA`, `FW` or `TOOL`. Add a new area only if none fits.
2. Take the next free number in that area: `Q-<AREA>-NN`, for example `Q-NET-11`. IDs are never reused or renumbered.
3. Add the entry in that area's section, with an anchor on the line above the heading:
   ```markdown
   <a id="q-net-11"></a>
   ### Q-NET-11 Short title
   - **Symptom:** What you see
   - **Root cause:** Why. Mark it "(inferred)" if no measurement isolated it
   - **Fix:** What was done, with repo paths
   - **Evidence:** Measurement, log line, commit or date
   - **Status:** `fixed`, `worked-around`, `open` or `documented`. **Applies to:** how far it generalises
   ```
4. Add a row to the area's table in the Index at the top, with the status letter (F, W, O, D) as the other rows do.
5. If it needed real digging, write the investigation (below) and link to it as `03-investigations.md#inv-<slug>`.

### A new investigation (chapter 03)

1. Add a section under "Investigations" with an anchor in the heading: `### <a id="inv-your-slug"></a>Title`. Keep the slug short and lower case; if you rename a section, keep the old anchor as well so links do not break.
2. Start with a small table (date, area, status, the fix or commit), then the symptom, the hypotheses with a verdict each, the attempts in the order tried, a **before and after** table with numbers, the conclusion, follow-ups and sources.
3. Use the chapter's status words: `fixed`, `works on hw` (only where the owner saw it on the panel), `mitigated`, `open`, `rejected` or `diagnosed`.
4. State what the numbers came from (a log, a commit, a datasheet) and mark conclusions that no measurement isolated as "(inferred)".
5. Add a row to the chapter's Index table with the key before and after figures, and link it from any quirk it explains.

### A new method (chapter 02)

1. Add the next free `D-NN` as a heading, `## D-NN Title`, and a row in the Index table (ID, method, layer, cost, main tool or file).
2. Use the same sub-headings as the other methods: when to reach for it, how (the exact commands or files), what it told us (with numbers), pitfalls, and cost.
3. Add a line to the "Symptom to first method" table if the method answers a symptom.
4. Only document a method that was actually used here, and say on which hardware.

### A new decision (chapter 04)

Add an `ADR-NN` section with Context, Decision, Alternatives, Consequences and Evidence, and a row in the ADR index. A decision that rests on reasoning only should say so.

### Before you commit

Check that every relative link resolves, that the status words match the evidence, and that nothing private is in the text. If a change alters code behaviour, build and lint it as described in the [new project checklist](05-new-project-checklist.md#10-build-and-lint).
