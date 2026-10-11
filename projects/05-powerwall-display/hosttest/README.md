# 05 host tests

Plain-C++ checks of the network-side code, built with MSVC on the PC (no board needed):

- `src/net_body.h`: the chunked-transfer decoder, fed every split of each sample (one cut at every byte
  offset, every pair of offsets for short samples, and byte by byte), and the body read loop against a fake
  socket and clock (Content-Length, chunked, until close, stalls hitting the deadline, too-big bodies).
- `src/time_util.cpp`: ISO 8601 and HTTP-date parsing on the formats the APIs send, plus month names.
- `src/enso.cpp` (built with `-DENSO_HOST_TEST`, which leaves out the Arduino half): the discussion-page
  stripper and parsers, and the data-file tail parsers, on real CPC payloads from 2026-10-11 in `data/`.

Run from PowerShell:

```powershell
.\projects\05-powerwall-display\hosttest\run.ps1
```

It prints `N passed, 0 failed` and exits with the number of failures. It needs Visual Studio with the
C++ workload and a Windows SDK; `run.ps1` finds them with `vswhere` (no developer prompt needed).
`stubs/` holds minimal stand-ins for the two ESP/Arduino headers that `mem_util.h` includes.
PlatformIO ignores this folder.
