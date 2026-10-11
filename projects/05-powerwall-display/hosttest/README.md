# 05 host tests

Plain-C++ checks of the network-side code, built with MSVC on the PC (no board needed):

- `src/net_body.h`: the chunked-transfer decoder, fed every split of each sample (one cut at every byte
  offset, every pair of offsets for short samples, and byte by byte), and the body read loop against a fake
  socket and clock (Content-Length, chunked, until close, stalls hitting the deadline, too-big bodies).
- `src/time_util.cpp`: ISO 8601 and HTTP-date parsing on the formats the APIs send, plus month names.
- `src/enso.cpp` (built with `-DENSO_HOST_TEST`, which leaves out the Arduino half): the discussion-page
  stripper and parsers, and the data-file tail parsers, on real CPC payloads from 2026-10-11 in `data/`.
- `src/cache.cpp` (built with `-DCACHE_HOST_TEST`, which leaves out `printStatus()`): record header checks and
  every rejection path, the UTC/millis() conversions (unknown clock, future times, huge ages, the 2^32 wrap),
  job due times, and save/load round trips on an in-memory SD card (`stubs/sd_store.h`, faked in test.cpp):
  no card, missing directory, version/size mismatches, truncated, too long and corrupt files, an 80 KB record.

- `src/day_history_calc.h`: the history screen's pure logic: CRC-32 check value, month lengths and leap years, date
  arithmetic, Monday-first weeks across month/year boundaries, period slots and ASCII labels, the card months a period
  needs, and the counter-delta binning rules (negative delta, >30 min gap, hour change, DST repeated/skipped hour).

- `src/backlight_curve.h`: the brightness slider's gamma curve (endpoints, monotonic, min-but-on at 0 %, rounding,
  empty ranges, inverse) and the saved-settings decoding (V2 percentage, V1 raw-duty conversion, rejects).

Run from PowerShell:

```powershell
.\projects\05-powerwall-display\hosttest\run.ps1
```

It prints `N passed, 0 failed` and exits with the number of failures. It needs Visual Studio with the
C++ workload and a Windows SDK; `run.ps1` finds them with `vswhere` (no developer prompt needed).
`stubs/` holds minimal stand-ins for the ESP/Arduino headers that `mem_util.h` and `cache.cpp` include
(`Arduino.h` keeps Serial output in a string for the checks and has a settable `millis()`).
PlatformIO ignores this folder.
