# Patched copy of the Arduino-ESP32 3.2.1 `SD` library

Projects list it in `lib_deps` (`symlink://../../common/lib/SD`) so it replaces the core's built-in `SD`.

Changes from upstream (search for "mini-screen" in `src/`):
1. `sdStop()` in `sd_diskio.cpp`: after the Stop Tran token, skip the one 0xFF byte and wait for the card's busy
   period. Upstream deselected immediately, so the next CMD13 hit a busy card and multi-block writes (any file
   write of 1 KB+) failed intermittently at every clock. Found and verified with `projects/06-sd-card-test`.
2. `sd_read_register()` / `SDFS::readRegister()`: read CID, CSD, SCR and SD Status for card reports and per-card
   settings (`SdStore`).

When the core is upgraded, re-copy the new upstream library and re-apply both changes (or drop 1 if upstream fixed it).
