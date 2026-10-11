// SD-card cache of the background data - see cache.h.
// Record file: Header (24 B) + payload + CRC-32 (4 B, of header and payload), one sdcard::writeFile.
// The record is built and read back in a PSRAM buffer (the sun image record is 80 KB). The checks and
// time conversions are plain C++ and the card calls go through SdStore's helpers only, so hosttest/
// builds this whole file (-DCACHE_HOST_TEST, stub Arduino.h/sd_store.h with an in-memory card);
// only printStatus(), which lists the directory through SD.*, is left out there.
#include "cache.h"
#include "mem_util.h"
#include "time_util.h"
#include <Arduino.h>
#include <sd_store.h>
#include <stdio.h>
#include <string.h>

namespace cache {
namespace {

constexpr char MAGIC[4] = {'P', 'W', 'C', '1'};
constexpr const char *DIR = "/powerwall/cache";
constexpr size_t PATH_CAP = 48;
constexpr size_t CRC_BYTES = 4;
constexpr size_t OVERHEAD = sizeof(Header) + CRC_BYTES;
constexpr size_t MAX_PAYLOAD = 256 * 1024;       // sanity cap (the largest record, the sun image, is ~80 KB)
constexpr int64_t MAX_VALID_UTC = 4102444800;    // 2100-01-01: anything later is not a real fetch time
constexpr int64_t MAX_FUTURE_S = 5 * 60;         // clock skew tolerated between saving and restoring
constexpr uint32_t MAX_AGE_MS = 0x7FFFFFFF;      // msFromUtc clamp: still a positive int32 difference

// Why a record was rejected (Ok = accepted)
enum class Check : uint8_t { Ok, Short, Magic, Version, Size, Length, Crc, Time };

// CRC-32 (IEEE 802.3 / zlib), 4 bits per step: a 64-byte table instead of 1 KB (a few ms for the 80 KB
// sun record, on the network task)
uint32_t crc32(const void *data, size_t n, uint32_t crc = 0) {
    static const uint32_t T[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                   0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                   0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    const uint8_t *p = static_cast<const uint8_t *>(data);
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ T[crc & 15];
        crc = (crc >> 4) ^ T[crc & 15];
    }
    return ~crc;
}

// A fetch time that can be real: past 2024 (NTP-set clock) and before 2100
bool validUtc(int64_t t) { return tu::clockValid((time_t)t) && t < MAX_VALID_UTC; }

// The header checks that don't involve the clock
Check checkFields(const Header &h, uint16_t version, uint32_t size) {
    if (memcmp(h.magic, MAGIC, sizeof(MAGIC)) != 0) return Check::Magic;
    if (h.version != version) return Check::Version;
    if (h.size != size) return Check::Size;
    return Check::Ok;
}

bool plausibleTime(int64_t fetchedUtc, time_t nowUtc) {
    if (!validUtc(fetchedUtc)) return false;
    return !tu::clockValid(nowUtc) || fetchedUtc <= (int64_t)nowUtc + MAX_FUTURE_S;
}

// Writes header + data + CRC into rec (OVERHEAD + size bytes)
void buildRecord(uint8_t *rec, uint16_t version, const void *data, size_t size, time_t fetchedUtc) {
    Header h;
    makeHeader(h, version, (uint32_t)size, fetchedUtc);
    memcpy(rec, &h, sizeof(h));
    memcpy(rec + sizeof(h), data, size);
    const uint32_t crc = crc32(rec, sizeof(h) + size);
    memcpy(rec + sizeof(h) + size, &crc, CRC_BYTES);
}

// Checks a whole record file (len bytes at rec) for (version, size); on Ok fetchedUtc is its time
Check checkRecord(const uint8_t *rec, size_t len, uint16_t version, uint32_t size, time_t nowUtc,
                  time_t &fetchedUtc) {
    if (len < sizeof(Header)) return Check::Short;
    Header h;
    memcpy(&h, rec, sizeof(h));
    const Check c = checkFields(h, version, size);
    if (c != Check::Ok) return c;
    if (len != OVERHEAD + size) return Check::Length;
    uint32_t crc = 0;
    memcpy(&crc, rec + sizeof(h) + size, CRC_BYTES);
    if (crc != crc32(rec, sizeof(h) + size)) return Check::Crc;
    if (!plausibleTime(h.fetchedUtc, nowUtc)) return Check::Time;
    fetchedUtc = (time_t)h.fetchedUtc;
    return Check::Ok;
}

// One line saying why record `name` (len bytes at rec) was ignored
void logRejected(const char *name, Check c, const uint8_t *rec, size_t len, uint16_t version, size_t size) {
    Header h = {};
    if (len >= sizeof(h)) memcpy(&h, rec, sizeof(h));
    switch (c) {
    case Check::Version:
        Serial.printf("Cache: %s ignored (version %u != %u)\n", name, (unsigned)h.version, (unsigned)version);
        break;
    case Check::Size:
        Serial.printf("Cache: %s ignored (size %u != %u B)\n", name, (unsigned)h.size, (unsigned)size);
        break;
    case Check::Short:
    case Check::Length: {
        const size_t want = OVERHEAD + size;
        Serial.printf("Cache: %s ignored (file %u B, expected %u)\n", name, (unsigned)len, (unsigned)want);
        break;
    }
    case Check::Magic: Serial.printf("Cache: %s ignored (not a cache record)\n", name); break;
    case Check::Crc: Serial.printf("Cache: %s ignored (CRC mismatch)\n", name); break;
    case Check::Time: Serial.printf("Cache: %s ignored (implausible fetch time)\n", name); break;
    case Check::Ok: break;
    }
}

bool recordPath(const char *name, char (&path)[PATH_CAP]) {
    const int n = snprintf(path, sizeof(path), "%s/%s.bin", DIR, name);
    return n > 0 && (size_t)n < sizeof(path);
}

} // namespace

// ---- Pure helpers ----

void makeHeader(Header &h, uint16_t version, uint32_t size, time_t fetchedUtc) {
    memcpy(h.magic, MAGIC, sizeof(MAGIC));
    h.version = version;
    h.reserved = 0;
    h.size = size;
    h.reserved2 = 0;
    h.fetchedUtc = (int64_t)fetchedUtc;
}

bool checkHeader(const Header &h, uint16_t version, uint32_t size, time_t nowUtc, time_t &fetchedUtc) {
    if (checkFields(h, version, size) != Check::Ok || !plausibleTime(h.fetchedUtc, nowUtc)) return false;
    fetchedUtc = (time_t)h.fetchedUtc;
    return true;
}

uint32_t msFromUtc(time_t fetchedUtc, time_t nowUtc, uint32_t nowMs) {
    if (!tu::clockValid(nowUtc) || fetchedUtc >= nowUtc) return nowMs; // age unknown / future: just fetched
    if (fetchedUtc <= 0) return nowMs - MAX_AGE_MS;
    const int64_t ageS = (int64_t)nowUtc - (int64_t)fetchedUtc; // > 0, no overflow: both positive
    const uint32_t ageMs = ageS >= (int64_t)(MAX_AGE_MS / 1000) ? MAX_AGE_MS : (uint32_t)ageS * 1000U;
    return nowMs - ageMs; // wraps below 0 like millis() itself: millis() - result == age
}

time_t utcFromMs(uint32_t fetchedMs, time_t nowUtc, uint32_t nowMs) {
    if (!tu::clockValid(nowUtc)) return 0;
    return nowUtc - (time_t)((nowMs - fetchedMs) / 1000U); // unsigned difference: wrap-safe
}

uint32_t dueInMs(time_t fetchedUtc, time_t nowUtc, uint32_t periodMs) {
    if (fetchedUtc <= 0 || !tu::clockValid(nowUtc)) return 0;
    if (fetchedUtc >= nowUtc) return periodMs; // from the future (clock skew): a full period
    const int64_t ageS = (int64_t)nowUtc - (int64_t)fetchedUtc;
    if (ageS > (int64_t)(periodMs / 1000U)) return 0;
    const uint32_t ageMs = (uint32_t)ageS * 1000U; // <= periodMs
    return ageMs >= periodMs ? 0 : periodMs - ageMs;
}

void fmtAge(char *buf, size_t n, int64_t seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds < 90) snprintf(buf, n, "%ld s", (long)seconds);
    else if (seconds < 90 * 60) snprintf(buf, n, "%ld min", (long)(seconds / 60));
    else if (seconds < 48 * 3600) snprintf(buf, n, "%ld h", (long)(seconds / 3600));
    else snprintf(buf, n, "%ld d", (long)(seconds / 86400 > 99999 ? 99999 : seconds / 86400));
}

// ---- Card I/O ----

bool saveRaw(const char *name, uint16_t version, const void *data, size_t size, time_t fetchedUtc) {
    char path[PATH_CAP];
    // No real fetch time (clock not set) = nothing worth caching: the age could never be known
    if (!validUtc(fetchedUtc) || size > MAX_PAYLOAD || !recordPath(name, path) || !sdcard::mounted()) return false;
    HeapBuf rec;
    if (!rec.psram(OVERHEAD + size)) {
        Serial.printf("Cache: no PSRAM to save %s\n", name);
        return false;
    }
    buildRecord(rec.as<uint8_t>(), version, data, size, fetchedUtc);
    // mkdirs every time (two exists() checks): a swapped card has no directory, and writing into a missing
    // one would count as an I/O error and step the card's clock down
    if (sdcard::mkdirs(DIR) && sdcard::writeFile(path, rec.p, rec.size)) return true;
    Serial.printf("Cache: saving %s failed\n", name);
    return false;
}

bool loadRaw(const char *name, uint16_t version, void *data, size_t size, time_t &fetchedUtc) {
    char path[PATH_CAP];
    if (size > MAX_PAYLOAD || !recordPath(name, path) || !sdcard::mounted()) return false;
    HeapBuf rec;
    if (!rec.psram(OVERHEAD + size + 1)) { // + 1: a longer file shows up as too long, not cut to fit
        Serial.printf("Cache: no PSRAM to load %s\n", name);
        return false;
    }
    size_t len = 0;
    if (!sdcard::readFile(path, rec.p, rec.size, &len)) return false; // no file (or unreadable: SdStore counts it)
    time_t utc = 0;
    const Check c = checkRecord(rec.as<uint8_t>(), len, version, (uint32_t)size, time(nullptr), utc);
    if (c != Check::Ok) {
        logRejected(name, c, rec.as<uint8_t>(), len, version, size);
        return false;
    }
    memcpy(data, rec.as<uint8_t>() + sizeof(Header), size);
    fetchedUtc = utc;
    return true;
}

uint32_t msNow(time_t fetchedUtc) { return msFromUtc(fetchedUtc, time(nullptr), millis()); }

#ifndef CACHE_HOST_TEST

void printStatus() {
    if (!sdcard::mounted()) {
        Serial.println("Cache: no SD card");
        return;
    }
    struct Entry {
        char name[24];
        uint32_t bytes;
    };
    constexpr int MAX_FILES = 16;
    Entry files[MAX_FILES];
    int count = 0;
    {
        sdcard::Lock lk; // names and sizes first; the headers are read after the directory is closed
        File dir = SD.open(DIR);
        while (dir && dir.isDirectory() && count < MAX_FILES) {
            File f = dir.openNextFile();
            if (!f) break;
            if (!f.isDirectory()) {
                copyStr(files[count].name, sizeof(files[count].name), f.name());
                files[count].bytes = (uint32_t)f.size();
                count++;
            }
            f.close();
        }
        if (dir) dir.close();
    }
    const time_t now = time(nullptr);
    Serial.printf("Cache: %d file(s) in %s\n", count, DIR);
    for (int i = 0; i < count; i++) {
        char path[PATH_CAP + 8];
        snprintf(path, sizeof(path), "%s/%s", DIR, files[i].name);
        Header h;
        size_t n = 0;
        if (!sdcard::readFile(path, &h, sizeof(h), &n) || n != sizeof(h) || memcmp(h.magic, MAGIC, sizeof(MAGIC)) != 0) {
            Serial.printf("  %-12s %7lu B  not a cache record\n", files[i].name, (unsigned long)files[i].bytes);
            continue;
        }
        char age[16] = "?";
        if (tu::clockValid(now) && validUtc(h.fetchedUtc)) fmtAge(age, sizeof(age), (int64_t)now - h.fetchedUtc);
        Serial.printf("  %-12s %7lu B  v%u  fetched %s ago\n", files[i].name, (unsigned long)files[i].bytes,
                      (unsigned)h.version, age);
    }
}

#endif // !CACHE_HOST_TEST

} // namespace cache
