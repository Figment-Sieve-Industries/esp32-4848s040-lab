#pragma once
// SD-card cache of the background data (weather, aurora, Kp, moon times, space weather, sun image,
// ENSO), so the screens fill straight after a reboot and fetches still inside their refresh window
// are skipped. Implemented in cache.cpp; used by the fetcher modules and the job scheduler in
// powerwall.cpp (network task, core 0). Nothing goes to internal flash.
//
// Files: /powerwall/cache/<name>.bin = Header + payload + CRC-32 (of header and payload), written in
// one sdcard::writeFile call. Not sdcard::writeAtomic: that is capped at 4 KB and the sun image is
// 80 KB. A cut mid-write leaves a file that fails its CRC, so the record is ignored and refetched.
// A record is only accepted when magic, version, payload size, file length and CRC all match, so
// changing a cached struct needs a version bump (or at least changes its size) and an old file is
// then ignored rather than misread.
//
// Times: millis() restarts at boot, so records store the fetch time as UTC. On restore the data's
// fetchedMs is rebuilt from its age (msFromUtc) so the existing staleness checks keep working.
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <type_traits>

namespace cache {

// ---- Pure helpers (no Arduino/SD; host-tested in hosttest/) ----

struct Header {
    char magic[4];       // "PWC1"
    uint16_t version;    // per-record version (bump when the struct's meaning changes)
    uint16_t reserved;   // 0
    uint32_t size;       // payload bytes (sizeof the record struct)
    uint32_t reserved2;  // 0 (explicit padding: the same layout whatever int64_t's alignment)
    int64_t fetchedUtc;  // when the data was fetched (UTC seconds)
};
static_assert(sizeof(Header) == 24 && offsetof(Header, fetchedUtc) == 16, "on-card layout");

void makeHeader(Header &h, uint16_t version, uint32_t size, time_t fetchedUtc);
// True when h is a valid header for (version, size) and fetchedUtc is plausible (a valid clock time,
// not more than 5 minutes in the future of nowUtc when nowUtc is a valid clock time).
bool checkHeader(const Header &h, uint16_t version, uint32_t size, time_t nowUtc, time_t &fetchedUtc);

// millis() value to store as fetchedMs for data fetched at fetchedUtc. Unknown clock (nowUtc not
// valid) or a future fetchedUtc -> nowMs (treated as just fetched). Ages beyond what fits are clamped
// so the result is always "older than any staleness limit" (at most ~24 days back).
uint32_t msFromUtc(time_t fetchedUtc, time_t nowUtc, uint32_t nowMs);
// UTC time of data fetched at fetchedMs (millis()); 0 when the clock isn't valid.
time_t utcFromMs(uint32_t fetchedMs, time_t nowUtc, uint32_t nowMs);
// Milliseconds until a job with this period is due again, given its data's fetch time. 0 = due now
// (also when fetchedUtc is 0 or the clock isn't valid).
uint32_t dueInMs(time_t fetchedUtc, time_t nowUtc, uint32_t periodMs);
// Short age for logs: "45 s", "12 min", "5 h", "3 d" (negative ages count as 0).
void fmtAge(char *buf, size_t n, int64_t seconds);

// ---- Card I/O (network task only) ----

// Save a record (header + data) in one write. False if no card, the clock isn't set (fetchedUtc not
// a valid time) or the write failed (logged).
bool saveRaw(const char *name, uint16_t version, const void *data, size_t size, time_t fetchedUtc);
// Load a record into data (exactly size bytes). False if no card, no file, wrong version/size,
// corrupt, or an implausible time; a rejected file logs one line saying why (no card / no file are
// silent: the caller's summary covers them). data is untouched on failure.
bool loadRaw(const char *name, uint16_t version, void *data, size_t size, time_t &fetchedUtc);

template <class T> bool save(const char *name, uint16_t version, const T &v, time_t fetchedUtc) {
    static_assert(std::is_trivially_copyable<T>::value, "cache records must be plain data");
    return saveRaw(name, version, &v, sizeof(T), fetchedUtc);
}
template <class T> bool load(const char *name, uint16_t version, T &v, time_t &fetchedUtc) {
    static_assert(std::is_trivially_copyable<T>::value, "cache records must be plain data");
    return loadRaw(name, version, &v, sizeof(T), fetchedUtc);
}

// msFromUtc() with the current clock and millis().
uint32_t msNow(time_t fetchedUtc);

// The usual restore: load record `name` into a T, set its millis() timestamp field (fetchedMs) from
// the record's age and publish it with shared.set() (a Shared<T>). Returns the record's fetch time,
// 0 when nothing was restored. T must be small enough for the network task's stack.
template <class T, class S> time_t restoreInto(const char *name, uint16_t version, S &shared, uint32_t T::*fetchedMs) {
    T v{};
    time_t utc = 0;
    if (!load(name, version, v, utc)) return 0;
    v.*fetchedMs = msNow(utc);
    shared.set(v);
    return utc;
}

// Status for the serial "c" command: one line per record file (name, version, size, age).
void printStatus();

} // namespace cache
