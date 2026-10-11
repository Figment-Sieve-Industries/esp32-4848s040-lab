#include <Arduino.h>
#include <SD.h>
#include <stdarg.h>
#include <iterator>
#include "sd_store.h"
#include "sd_tests.h"

namespace sdtests {

namespace {

constexpr size_t CHUNK = 4096;
uint8_t wbuf[CHUNK];
uint8_t rbuf[CHUNK];

uint16_t gPass = 0;
uint16_t gFail = 0;

const char *RUN_DIR = "/sdtest/run";

void logf(LogFn log, const char *fmt, ...) {
    if (!log) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log(buf);
}

// PASS name (detail) / FAIL name: detail, and counts it.
void report(LogFn log, bool ok, const char *name, const char *fmt, ...) {
    char det[112];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(det, sizeof(det), fmt, ap);
    va_end(ap);
    if (ok) {
        gPass++;
        logf(log, "PASS %s (%s)", name, det);
    } else {
        gFail++;
        logf(log, "FAIL %s: %s", name, det);
    }
}

uint32_t kbps(uint32_t bytes, uint32_t ms) {
    if (ms == 0) ms = 1;
    return (uint32_t)(((uint64_t)bytes * 1000ULL) / ((uint64_t)ms * 1024ULL));
}

// Fills n (<= 4096) bytes with the pattern of 4 KB block blockIdx. Same (seed, blockIdx) gives the same bytes.
void fillPattern(uint8_t *buf, size_t n, uint32_t seed, uint32_t blockIdx) {
    uint32_t x = seed ^ (blockIdx * 0x9E3779B1u);
    if (x == 0) x = 0x1234567u;
    for (size_t i = 0; i < n; i += 4) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        for (size_t k = 0; k < 4 && i + k < n; k++) buf[i + k] = (uint8_t)(x >> (8 * k));
    }
}

// Writes total bytes of pattern in chunk-sized writes (time includes close). Verifiable only with chunk == 4096.
bool writePattern(const char *path, uint32_t total, uint32_t chunk, uint32_t seed, uint32_t &ms) {
    sdcard::Lock lk;
    uint32_t t0 = millis();
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    bool ok = true;
    for (uint32_t off = 0; off < total && ok; off += chunk) {
        uint32_t n = total - off < chunk ? total - off : chunk;
        fillPattern(wbuf, n, seed, off / CHUNK);
        ok = f.write(wbuf, n) == n;
    }
    f.close();
    ms = millis() - t0;
    return ok;
}

// Reads the file back; verify compares against the pattern (chunk must be 4096 then).
bool readPattern(const char *path, uint32_t total, uint32_t chunk, uint32_t seed, bool verify, uint32_t &ms,
                 uint32_t &badOff) {
    sdcard::Lock lk;
    uint32_t t0 = millis();
    badOff = 0xFFFFFFFFu;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    bool ok = true;
    for (uint32_t off = 0; off < total && ok; off += chunk) {
        uint32_t n = total - off < chunk ? total - off : chunk;
        if (f.read(rbuf, n) != (int)n) {
            ok = false;
            badOff = off;
            break;
        }
        if (verify) {
            fillPattern(wbuf, n, seed, off / CHUNK);
            if (memcmp(wbuf, rbuf, n) != 0) {
                ok = false;
                for (uint32_t i = 0; i < n; i++)
                    if (wbuf[i] != rbuf[i]) {
                        badOff = off + i;
                        break;
                    }
            }
        }
    }
    f.close();
    ms = millis() - t0;
    return ok;
}

bool fileSize(const char *path, uint32_t &size) {
    sdcard::Lock lk;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    size = (uint32_t)f.size();
    f.close();
    return true;
}

// ---- functional tests ----

void testCard(LogFn log) {
    logf(log, "== Card");
    sdcard::Info in = sdcard::info();
    uint32_t cardMb = (uint32_t)(in.cardBytes / 1048576ULL);
    uint32_t totMb = (uint32_t)(in.totalBytes / 1048576ULL);
    uint32_t usedMb = (uint32_t)(in.usedBytes / 1048576ULL);
    logf(log, "%s %luMB fat %lu/%luMB %.1fMHz", sdcard::cardTypeName(in.cardType), (unsigned long)cardMb,
         (unsigned long)usedMb, (unsigned long)totMb, in.freqHz / 1e6);
    uint32_t freeMb = totMb > usedMb ? totMb - usedMb : 0;
    report(log, freeMb >= 4, "free space", "%lu MB free", (unsigned long)freeMb);
}

void testProbe(LogFn log) {
    report(log, sdcard::probe(), "probe", "sector 0 read");
}

void testMkdirs(LogFn log) {
    bool ok = sdcard::mkdirs("/sdtest/run/a/b/c");
    bool ex;
    {
        sdcard::Lock lk;
        ex = SD.exists("/sdtest/run/a/b/c");
    }
    report(log, ok && ex, "mkdirs", ok ? (ex ? "a/b/c exists" : "created but missing") : "mkdirs returned false");
}

void testText(LogFn log) {
    const char *path = "/sdtest/run/text.txt";
    const char msg[] = "Hello SD card, 123 \xC2\xB0 test.";
    char buf[64];
    size_t n = 0;
    bool w = sdcard::writeFile(path, msg, strlen(msg));
    bool r = w && sdcard::readFile(path, buf, sizeof(buf), &n);
    bool same = r && n == strlen(msg) && memcmp(buf, msg, n) == 0;
    report(log, same, "text round trip", "w=%d r=%d len=%u/%u", w, r, (unsigned)n, (unsigned)strlen(msg));
    n = 123;
    bool miss = sdcard::readFile("/sdtest/run/nope.txt", buf, sizeof(buf), &n);
    report(log, !miss, "read missing", miss ? "returned true" : "returned false");
}

void testPattern(LogFn log) {
    const char *path = "/sdtest/run/pat256.bin";
    const uint32_t total = 262144;
    uint32_t wms = 0, rms = 0, bad = 0;
    if (!writePattern(path, total, CHUNK, 0xA5A5u, wms)) {
        report(log, false, "pattern 256K", "write failed");
        return;
    }
    uint32_t sz = 0;
    if (!fileSize(path, sz) || sz != total) {
        report(log, false, "pattern 256K", "size %lu != %lu", (unsigned long)sz, (unsigned long)total);
        return;
    }
    if (!readPattern(path, total, CHUNK, 0xA5A5u, true, rms, bad)) {
        report(log, false, "pattern 256K", "verify failed at %ld", (long)(int32_t)bad);
        return;
    }
    report(log, true, "pattern 256K", "W %lu R %lu KB/s", (unsigned long)kbps(total, wms),
           (unsigned long)kbps(total, rms));
}

void testThroughput(LogFn log) {
    const char *path = "/sdtest/run/tp1m.bin";
    const uint32_t total = 1048576;
    uint32_t wms = 0, rms = 0, bad = 0;
    bool w = writePattern(path, total, CHUNK, 0x77u, wms);
    bool r = w && readPattern(path, total, CHUNK, 0x77u, false, rms, bad);
    if (!r) {
        report(log, false, "throughput 1M", w ? "read failed" : "write failed");
    } else {
        report(log, true, "throughput 1M", "W %lu R %lu KB/s", (unsigned long)kbps(total, wms),
               (unsigned long)kbps(total, rms));
    }
    sdcard::removeTree(path);

    const char *path2 = "/sdtest/run/tp128k.bin";
    uint32_t ms2 = 0;
    bool w2 = writePattern(path2, 131072, 512, 0x99u, ms2);
    if (!w2) {
        report(log, false, "small writes 512B", "write failed");
    } else {
        report(log, true, "small writes 512B", "W %lu KB/s", (unsigned long)kbps(131072, ms2));
    }
    sdcard::removeTree(path2);
}

void testAppend(LogFn log) {
    const char *path = "/sdtest/run/append.csv";
    const unsigned N = 500;
    char line[48];
    uint64_t totUs = 0;
    uint32_t maxUs = 0;
    unsigned failed = 0;
    for (unsigned i = 0; i < N; i++) {
        snprintf(line, sizeof(line), "%u,%lu,ABCDEFGHIJKLMNOP", i, (unsigned long)millis());
        uint32_t t0 = micros();
        bool ok = sdcard::appendLine(path, line);
        uint32_t dt = micros() - t0;
        if (!ok) {
            failed++;
            continue;
        }
        totUs += dt;
        if (dt > maxUs) maxUs = dt;
    }
    if (failed) {
        report(log, false, "append x500", "%u appends failed", failed);
        return;
    }

    // read back and check the line count and index order
    unsigned count = 0, badIdx = 0;
    char cur[64];
    size_t curLen = 0;
    bool openOk = true;
    {
        sdcard::Lock lk;
        File f = SD.open(path, FILE_READ);
        if (!f) {
            openOk = false;
        } else {
            int n;
            while ((n = f.read(rbuf, sizeof(rbuf))) > 0) {
                for (int i = 0; i < n; i++) {
                    char c = (char)rbuf[i];
                    if (c == '\n') {
                        cur[curLen < sizeof(cur) ? curLen : sizeof(cur) - 1] = 0;
                        if ((unsigned)strtoul(cur, nullptr, 10) != count) badIdx++;
                        count++;
                        curLen = 0;
                    } else if (curLen < sizeof(cur) - 1) {
                        cur[curLen++] = c;
                    }
                }
            }
            f.close();
        }
    }
    uint32_t avgUs = (uint32_t)(totUs / N);
    if (!openOk) {
        report(log, false, "append x500", "reopen failed");
    } else if (count != N || badIdx) {
        report(log, false, "append x500", "lines %u (want %u) badIdx %u", count, N, badIdx);
    } else {
        report(log, true, "append x500", "avg %luus max %lums", (unsigned long)avgUs,
               (unsigned long)(maxUs / 1000));
    }
}

struct State {
    uint32_t a;
    float b;
    char tag[16];
};

State makeState(uint32_t a) {
    State s;
    memset(&s, 0, sizeof(s));
    s.a = a;
    s.b = a * 1.5f;
    snprintf(s.tag, sizeof(s.tag), "tag%lu", (unsigned long)a);
    return s;
}

bool sameState(const State &x, const State &y) {
    return x.a == y.a && x.b == y.b && strncmp(x.tag, y.tag, sizeof(x.tag)) == 0;
}

bool exists(const char *p) {
    sdcard::Lock lk;
    return SD.exists(p);
}

void testAtomic(LogFn log) {
    const char *P = "/sdtest/run/state.bin";
    const char *T = "/sdtest/run/state.bin.tmp";
    const char *O = "/sdtest/run/other.bin";
    State s, got;

    // 20 writes with read back
    unsigned bad = 0;
    for (uint32_t i = 1; i <= 20; i++) {
        s = makeState(i);
        memset(&got, 0, sizeof(got));
        if (!sdcard::writeAtomic(P, &s, sizeof(s)) || !sdcard::readAtomic(P, &got, sizeof(got)) ||
            !sameState(s, got))
            bad++;
    }
    report(log, bad == 0, "atomic x20", bad ? "%u mismatches" : "all matched", bad);

    // torn tmp (garbage) must be ignored and removed
    uint8_t garbage[37];
    for (size_t i = 0; i < sizeof(garbage); i++) garbage[i] = (uint8_t)(0xC3 + i * 7);
    bool g = sdcard::writeFile(T, garbage, sizeof(garbage));
    memset(&got, 0, sizeof(got));
    bool r = sdcard::readAtomic(P, &got, sizeof(got));
    bool gone = !exists(T);
    report(log, g && r && got.a == 20 && gone, "atomic torn tmp", "w=%d r=%d a=%lu tmpGone=%d", g, r,
           (unsigned long)got.a, gone);

    // orphan tmp: the cut between remove and rename
    State b = makeState(99);
    bool w = sdcard::writeAtomic(O, &b, sizeof(b));
    bool rm, rn;
    {
        sdcard::Lock lk;
        rm = SD.remove(P);
        rn = SD.rename(O, T);
    }
    memset(&got, 0, sizeof(got));
    r = sdcard::readAtomic(P, &got, sizeof(got));
    bool pEx = exists(P), tEx = exists(T);
    report(log, w && rm && rn && r && got.a == 99 && pEx && !tEx, "atomic orphan tmp",
           "r=%d a=%lu main=%d tmp=%d", r, (unsigned long)got.a, pEx, tEx);

    // newest wins: stale valid tmp with lower seq
    sdcard::removeTree(P);
    sdcard::removeTree(T);
    bool ok = true;
    for (uint32_t i = 1; i <= 3; i++) {
        s = makeState(i);
        ok = sdcard::writeAtomic(P, &s, sizeof(s)) && ok;
    }
    const char *O2 = "/sdtest/run/other2.bin";
    State c = makeState(77);
    ok = sdcard::writeAtomic(O2, &c, sizeof(c)) && ok;
    {
        sdcard::Lock lk;
        ok = SD.rename(O2, T) && ok;
    }
    memset(&got, 0, sizeof(got));
    r = sdcard::readAtomic(P, &got, sizeof(got));
    tEx = exists(T);
    report(log, ok && r && got.a == 3 && !tEx, "atomic newest wins", "setup=%d r=%d a=%lu tmp=%d", ok, r,
           (unsigned long)got.a, tEx);

    // wrong length
    memset(&got, 0, sizeof(got));
    r = sdcard::readAtomic(P, &got, sizeof(State) - 1);
    report(log, !r, "atomic wrong len", r ? "accepted short length" : "rejected");
}

void testMany(LogFn log) {
    const char *dir = "/sdtest/run/many";
    if (!sdcard::mkdirs(dir)) {
        report(log, false, "100 files", "mkdir failed");
        return;
    }
    char path[48];
    char body[24];
    uint32_t t0 = millis();
    unsigned created = 0;
    for (unsigned i = 0; i < 100; i++) {
        snprintf(path, sizeof(path), "%s/f%03u.txt", dir, i);
        int n = snprintf(body, sizeof(body), "file number %03u\n", i);
        if (sdcard::writeFile(path, body, n)) created++;
    }
    uint32_t createMs = millis() - t0;

    unsigned count = 0;
    bool opened = true;
    {
        sdcard::Lock lk;
        File d = SD.open(dir);
        if (!d) {
            opened = false;
        } else {
            for (File f = d.openNextFile(); f; f = d.openNextFile()) {
                count++;
                f.close();
            }
            d.close();
        }
    }
    t0 = millis();
    bool removed = sdcard::removeTree(dir);
    uint32_t removeMs = millis() - t0;
    bool gone = !exists(dir);
    bool ok = created == 100 && opened && count == 100 && removed && gone;
    if (ok)
        report(log, true, "100 files", "create %lums remove %lums", (unsigned long)createMs,
               (unsigned long)removeMs);
    else
        report(log, false, "100 files", "created %u listed %u open=%d removed=%d gone=%d", created, count, opened,
               removed, gone);
}

void testLongName(LogFn log) {
    const char *path = "/sdtest/run/Long File Name 2026 test.csv";
    bool w = sdcard::writeFile(path, "a,b,c\n", 6);
    bool ex = w && exists(path);
    if (ex) {
        report(log, true, "long file name", "LFN ok");
    } else {
        logf(log, "WARN long file name unsupported (w=%d)", w);
    }
}

void testRenameOver(LogFn log) {
    const char *a = "/sdtest/run/ren_a.txt";
    const char *b = "/sdtest/run/ren_b.txt";
    sdcard::writeFile(a, "AAA", 3);
    sdcard::writeFile(b, "BBB", 3);
    bool rn;
    {
        sdcard::Lock lk;
        rn = SD.rename(a, b);
    }
    char buf[8] = {0};
    size_t n = 0;
    sdcard::readFile(b, buf, sizeof(buf) - 1, &n);
    buf[n] = 0;
    gPass++;
    if (rn)
        logf(log, "PASS rename over existing (replaced, b=%s)", buf);
    else
        logf(log, "PASS rename over existing (refused, b=%s)", buf);
}

// File written on the PC at the card root: 20000 lines of 42 bytes. Never deleted.
void testPcCheck(LogFn log) {
    const char *path = "/pc-check.txt";
    const uint32_t LINES = 20000, LEN = 42, TOTAL = LINES * LEN;
    if (!exists(path)) {
        logf(log, "WARN pc-check.txt missing (optional)");
        return;
    }
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
    uint32_t sz = 0;
    if (!fileSize(path, sz) || sz != TOTAL) {
        report(log, false, "pc-check", "size %lu != %lu", (unsigned long)sz, (unsigned long)TOTAL);
        return;
    }
    char exp[LEN + 1];
    uint32_t curLine = 0xFFFFFFFFu;
    uint32_t pos = 0;
    uint32_t badLine = 0xFFFFFFFFu;
    bool ioOk = true;
    uint32_t t0 = millis();
    {
        sdcard::Lock lk;
        File f = SD.open(path, FILE_READ);
        if (!f) {
            ioOk = false;
        } else {
            while (pos < TOTAL && badLine == 0xFFFFFFFFu) {
                int n = f.read(rbuf, sizeof(rbuf));
                if (n <= 0) {
                    ioOk = false;
                    break;
                }
                for (int i = 0; i < n && pos < TOTAL; i++, pos++) {
                    uint32_t ln = pos / LEN;
                    if (ln != curLine) {
                        curLine = ln;
                        uint32_t x = ln * 2654435761u;
                        snprintf(exp, sizeof(exp), "PC %05lu ", (unsigned long)ln);
                        for (int k = 0; k < 32; k++) exp[9 + k] = alphabet[(x >> (k % 27)) & 31];
                        exp[41] = '\n';
                    }
                    if (rbuf[i] != (uint8_t)exp[pos % LEN]) {
                        badLine = ln;
                        break;
                    }
                }
            }
            f.close();
        }
    }
    uint32_t ms = millis() - t0;
    if (badLine != 0xFFFFFFFFu)
        report(log, false, "pc-check", "mismatch at line %lu", (unsigned long)badLine);
    else if (!ioOk)
        report(log, false, "pc-check", "read failed at byte %lu", (unsigned long)pos);
    else
        report(log, true, "pc-check", "20000 lines, %lu KB/s", (unsigned long)kbps(TOTAL, ms));
}

void testCleanup(LogFn log) {
    bool rm = sdcard::removeTree(RUN_DIR);
    bool gone = !exists(RUN_DIR);
    report(log, rm && gone, "cleanup", "removed=%d gone=%d", rm, gone);
}

using TestFn = void (*)(LogFn);

} // namespace

Result runAll(LogFn log) {
    gPass = 0;
    gFail = 0;
    if (!sdcard::mounted()) {
        logf(log, "FAIL no card mounted");
        return {0, 1};
    }
    sdcard::removeTree(RUN_DIR);
    if (!sdcard::mkdirs(RUN_DIR)) {
        report(log, false, "workdir", "cannot create %s", RUN_DIR);
        return {gPass, gFail};
    }

    static const TestFn tests[] = {testCard,   testProbe,    testMkdirs,   testText,       testPattern,
                                   testThroughput, testAppend, testAtomic, testMany, testLongName, testRenameOver,
                                   testPcCheck};
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        tests[i](log);
        if (!sdcard::poll()) { // probes the card: mounted() alone only changes on poll()
            gFail++;
            logf(log, "FAIL card lost");
            logf(log, "== Done: %u passed, %u failed", gPass, gFail);
            return {gPass, gFail};
        }
    }
    testCleanup(log);
    logf(log, "== Done: %u passed, %u failed", gPass, gFail);
    return {gPass, gFail};
}

void speedSweep(LogFn log) {
    static const uint32_t clocks[] = {1, 4, 10, 20, 25};
    const size_t NC = std::size(clocks);
    const char *dir = "/sdtest/speed";
    const char *file = "/sdtest/speed/s.bin";
    bool passed[5] = {false, false, false, false, false};

    logf(log, "== Speed sweep");
    for (size_t i = 0; i < NC; i++) {
        uint32_t mhz = clocks[i];
        if (!sdcard::remount(mhz * 1000000UL)) {
            logf(log, "FAIL %lu MHz: mount failed", (unsigned long)mhz);
            continue;
        }
        sdcard::removeTree(dir);
        if (!sdcard::mkdirs(dir)) {
            logf(log, "FAIL %lu MHz: mkdir failed", (unsigned long)mhz);
            continue;
        }
        uint32_t ms = 0, bad = 0;
        if (!writePattern(file, 65536, CHUNK, 0x5EEDu + mhz, ms)) {
            logf(log, "FAIL %lu MHz: 64K write failed", (unsigned long)mhz);
            sdcard::removeTree(dir);
            continue;
        }
        if (!readPattern(file, 65536, CHUNK, 0x5EEDu + mhz, true, ms, bad)) {
            logf(log, "FAIL %lu MHz: verify failed at %ld", (unsigned long)mhz, (long)(int32_t)bad);
            sdcard::removeTree(dir);
            continue;
        }
        uint32_t wms = 0, rms = 0;
        bool w = writePattern(file, 524288, CHUNK, 0xBEEFu, wms);
        bool r = w && readPattern(file, 524288, CHUNK, 0xBEEFu, true, rms, bad);
        sdcard::removeTree(dir);
        if (!r) {
            logf(log, "FAIL %lu MHz: 512K %s failed", (unsigned long)mhz, w ? "read" : "write");
            continue;
        }
        passed[i] = true;
        logf(log, "PASS %lu MHz W %lu R %lu KB/s", (unsigned long)mhz, (unsigned long)kbps(524288, wms),
             (unsigned long)kbps(524288, rms));
    }

    int best = -1;
    for (size_t i = 0; i < NC; i++)
        if (passed[i]) best = (int)i;
    if (best < 0) {
        logf(log, "FAIL no clock passed, restoring default");
        sdcard::begin();
        return;
    }
    int chosen = best > 0 ? best - 1 : best;
    if (!sdcard::remount(clocks[chosen] * 1000000UL, true /* verify and remember for this card */)) {
        logf(log, "FAIL remount at %lu MHz", (unsigned long)clocks[chosen]);
        sdcard::begin();
        return;
    }
    logf(log, "== Using %lu MHz (fastest pass %lu MHz)", (unsigned long)clocks[chosen],
         (unsigned long)clocks[best]);
}

// ---- card registers ----

namespace {

// Common (unofficial) SD manufacturer IDs
const char *manufacturer(uint8_t mid) {
    switch (mid) {
    case 0x01: return "Panasonic";
    case 0x02: return "Toshiba/Kioxia";
    case 0x03: return "SanDisk/WD";
    case 0x1B: return "Samsung";
    case 0x1D: return "ADATA";
    case 0x27: return "Phison (OEM)";
    case 0x28: return "Lexar";
    case 0x31: return "Silicon Power";
    case 0x41: return "Kingston";
    case 0x74: return "Transcend";
    case 0x76: return "Patriot";
    case 0x82: return "Sony";
    case 0x9F: return "Kingston (OEM)";
    default: return "unknown";
    }
}

// CSD TRAN_SPEED / TAAC mantissa x10
const uint8_t CSD_MANT[16] = {0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80};

void hexLine(LogFn log, const char *name, const uint8_t *b, size_t n) {
    char line[160];
    int p = snprintf(line, sizeof(line), "%s ", name);
    for (size_t i = 0; i < n && p < (int)sizeof(line) - 3; i++) p += snprintf(line + p, sizeof(line) - p, "%02X", b[i]);
    log(line);
}

} // namespace

void cardReport(LogFn log) {
    sdcard::CardRegs r;
    logf(log, "== Card registers");
    if (!sdcard::readRegs(r)) {
        logf(log, "FAIL card registers: no card or read failed");
        return;
    }
    if (r.cidOk) {
        const uint8_t *c = r.cid;
        unsigned mdt = ((c[13] & 0x0F) << 8) | c[14];
        logf(log, "maker 0x%02X %s, OEM '%c%c'", c[0], manufacturer(c[0]), c[1], c[2]);
        logf(log, "product '%c%c%c%c%c' rev %u.%u", c[3], c[4], c[5], c[6], c[7], c[8] >> 4, c[8] & 0xF);
        logf(log, "made %04u-%02u, serial %02X%02X%02X%02X", 2000 + (mdt >> 4), mdt & 0xF, c[9], c[10], c[11],
             c[12]);
    } else {
        logf(log, "WARN CID read failed");
    }
    if (r.csdOk) {
        const uint8_t *s = r.csd;
        unsigned ver = s[0] >> 6;
        // TRAN_SPEED: unit 100 kbit/s * 10^n, mantissa 1.0-8.0 -> max SPI clock
        static const uint32_t unitKhz[4] = {100, 1000, 10000, 100000};
        uint32_t maxKhz = (s[3] & 7) < 4 ? unitKhz[s[3] & 7] * CSD_MANT[(s[3] >> 3) & 0xF] / 10 : 0;
        static const char *taacUnit[8] = {"1ns", "10ns", "100ns", "1us", "10us", "100us", "1ms", "10ms"};
        unsigned taacM = CSD_MANT[(s[1] >> 3) & 0xF];
        unsigned r2w = (s[12] >> 2) & 7;
        uint64_t bytes;
        if (ver == 1) {
            uint32_t cSize = ((uint32_t)(s[7] & 0x3F) << 16) | (s[8] << 8) | s[9];
            bytes = (uint64_t)(cSize + 1) * 512 * 1024;
        } else {
            uint32_t cSize = ((s[6] & 3) << 10) | (s[7] << 2) | (s[8] >> 6);
            uint32_t mult = ((s[9] & 3) << 1) | (s[10] >> 7);
            bytes = (uint64_t)(cSize + 1) << (mult + 2 + (s[5] & 0xF));
        }
        logf(log, "CSD v%u.0 (%s), %llu MB", ver + 1, ver ? "SDHC/XC" : "SDSC", (unsigned long long)(bytes >> 20));
        logf(log, "rated max clock %lu.%lu MHz (TRAN_SPEED 0x%02X)", (unsigned long)(maxKhz / 1000),
             (unsigned long)(maxKhz % 1000 / 100), s[3]);
        logf(log, "TAAC %u.%u x %s, NSAC %u, write = read x %u", taacM / 10, taacM % 10, taacUnit[s[1] & 7], s[2],
             1u << r2w);
    } else {
        logf(log, "WARN CSD read failed");
    }
    if (r.scrOk) {
        const uint8_t *s = r.scr;
        unsigned spec = s[0] & 0xF, spec3 = s[2] >> 7, spec4 = (s[2] >> 2) & 1, specx = ((s[2] & 3) << 2) | (s[3] >> 6);
        const char *v = specx ? (specx == 1 ? "5.x" : specx == 2 ? "6.x" : specx == 3 ? "7.x" : "8.x+")
                        : spec4 ? "4.x"
                        : spec3 ? "3.0x"
                        : spec == 2 ? "2.00" : spec == 1 ? "1.10" : "1.0x";
        logf(log, "SD spec %s, bus widths 0x%X", v, s[1] & 0xF);
    } else {
        logf(log, "WARN SCR read failed");
    }
    if (r.statusOk) {
        const uint8_t *s = r.status;
        static const char *speedClass[5] = {"C0", "C2", "C4", "C6", "C10"};
        static const char *auSize[16] = {"-",    "16K",  "32K", "64K", "128K", "256K", "512K", "1M",
                                         "2M",   "4M",   "8M",  "12M", "16M",  "24M",  "32M",  "64M"};
        unsigned sc = s[8];
        logf(log, "speed %s, UHS U%u, video V%u, app A%u, AU %s", sc < 5 ? speedClass[sc] : "?", s[14] >> 4, s[15],
             s[21] & 0xF, auSize[s[10] >> 4]);
    } else {
        logf(log, "WARN SD Status read failed");
    }
    if (r.cidOk) hexLine(log, "CID", r.cid, 16);
    if (r.csdOk) hexLine(log, "CSD", r.csd, 16);
    if (r.scrOk) hexLine(log, "SCR", r.scr, 8);
}

// ---- soak ----

namespace {

const char *SOAK_FILE = "/sdtest/soak.csv";

bool gRunning = false;
bool gPaused = false;
bool gFailStreak = false;
uint32_t gLastStep = 0;
uint32_t gNextSeq = 1;
uint64_t gTotUs = 0;
uint32_t gMaxUs = 0;
SoakStats gStats = {0, 0, 0, 0, 0};

// Reads the tail of the soak file and returns the seq of the last complete line, or -1.
int64_t lastSeqOnCard() {
    sdcard::Lock lk;
    File f = SD.open(SOAK_FILE, FILE_READ);
    if (!f) return -1;
    size_t size = f.size();
    size_t pos = size > 64 ? size - 64 : 0;
    char tail[65];
    int n = 0;
    if (f.seek(pos)) n = f.read((uint8_t *)tail, 64);
    f.close();
    if (n <= 0) return -1;
    tail[n] = 0;
    int end = n - 1;
    while (end >= 0 && tail[end] != '\n') end--; // last complete line ends here
    if (end < 0) return -1;
    int start = end - 1;
    while (start >= 0 && tail[start] != '\n') start--;
    if (start < 0 && pos > 0) return -1; // line begins before the window
    return (int64_t)strtoul(tail + start + 1, nullptr, 10);
}

} // namespace

void soakStart(LogFn log) {
    sdcard::mkdirs("/sdtest");
    bool ok = sdcard::writeFile(SOAK_FILE, "seq,ms,heap\n", 12);
    gStats = {0, 0, 0, 0, 0};
    gTotUs = 0;
    gMaxUs = 0;
    gNextSeq = 1;
    gPaused = false;
    gFailStreak = false;
    gLastStep = millis();
    gRunning = true;
    logf(log, "== Soak started");
    if (!ok) logf(log, "WARN soak header write failed");
}

void soakStop(LogFn log) {
    if (!gRunning) return;
    logf(log, "== Soak stop: w=%lu v=%lu err=%lu avg=%luus max=%lums", (unsigned long)gStats.written,
         (unsigned long)gStats.verified, (unsigned long)gStats.errors, (unsigned long)gStats.avgAppendUs,
         (unsigned long)gStats.maxAppendMs);
    gRunning = false;
}

bool soakRunning() {
    return gRunning;
}

SoakStats soakStats() {
    return gStats;
}

void soakStep(LogFn log) {
    if (!gRunning) return;
    uint32_t now = millis();
    if (now - gLastStep < 1000) return;
    gLastStep = now;

    if (!sdcard::mounted()) {
        if (!gPaused) {
            gPaused = true;
            logf(log, "WARN soak paused: no card");
        }
        return;
    }
    if (gPaused) {
        gPaused = false;
        sdcard::mkdirs("/sdtest");
        logf(log, "PASS soak resumed");
    }

    char line[48];
    snprintf(line, sizeof(line), "%lu,%lu,%lu", (unsigned long)gNextSeq, (unsigned long)millis(),
             (unsigned long)ESP.getFreeHeap());
    uint32_t t0 = micros();
    bool ok = sdcard::appendLine(SOAK_FILE, line);
    uint32_t dt = micros() - t0;
    if (!ok) {
        gStats.errors++;
        if (!gFailStreak) {
            gFailStreak = true;
            logf(log, "FAIL soak append seq %lu", (unsigned long)gNextSeq);
        }
        return;
    }
    gFailStreak = false;
    uint32_t seq = gNextSeq++;
    gStats.written++;
    gTotUs += dt;
    if (dt > gMaxUs) gMaxUs = dt;
    gStats.avgAppendUs = (uint32_t)(gTotUs / gStats.written);
    gStats.maxAppendMs = gMaxUs / 1000;

    if (gStats.written % 10 == 0) {
        int64_t got = lastSeqOnCard();
        if (got == (int64_t)seq) {
            gStats.verified++;
        } else {
            gStats.errors++;
            logf(log, "FAIL soak verify: want %lu got %ld", (unsigned long)seq, (long)got);
        }
    }
    if (gStats.written % 60 == 0) {
        logf(log, "soak: w=%lu v=%lu err=%lu avg=%luus max=%lums", (unsigned long)gStats.written,
             (unsigned long)gStats.verified, (unsigned long)gStats.errors, (unsigned long)gStats.avgAppendUs,
             (unsigned long)gStats.maxAppendMs);
    }
}

} // namespace sdtests
