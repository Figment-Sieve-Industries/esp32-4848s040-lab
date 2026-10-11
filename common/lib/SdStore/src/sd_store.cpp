// sd_store - see sd_store.h for the rules this module enforces.
#include "sd_store.h"

#include <Board4848S040_pins.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_random.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sdcard {

namespace {

constexpr uint32_t RETRY_MS = 2000; // poll(): gap between mount attempts
constexpr size_t ATOMIC_MAX = 4096; // writeAtomic() payload limit
constexpr char ATOMIC_MAGIC[4] = {'S', 'D', 'A', '1'};
// Clocks tried from the top down. The driver caps at 25 MHz.
constexpr uint32_t LADDER[] = {25000000, 20000000, 10000000, 4000000, 1000000};
// Files this module keeps at the card root
const char *const CFG_PATH = "/.sdstore.cfg";    // "cid=<32 hex>\nhz=<clock>\n": this card's proven clock
const char *const VERIFY_PATH = "/.sdstore.chk"; // scratch for the mount-time link test
constexpr size_t VERIFY_BLOCK = 4096;
constexpr size_t VERIFY_BLOCKS = 8; // 32 KB: long enough for multi-sector writes and reads

struct AtomicHeader {
    char magic[4];
    uint32_t seq;
    uint32_t len;
    uint32_t crc; // over seq, len, payload
};
static_assert(sizeof(AtomicHeader) == 16, "on-card format");

// Result of one file operation: Ok, a valid "no" (missing file, no valid copy, bad argument), or an
// I/O error that may mean a bad link or a lost card.
enum class Res : uint8_t { Ok, No, Err };

// Own bus object: nobody else can re-begin it on the default pins (10-13 = LCD red bits).
SPIClass sdSpi(FSPI);

bool busReady = false;
bool isMounted = false;
bool autoMount = false; // poll() may (re)mount
uint32_t curHz = 0;
uint32_t lastGoodHz = 0; // clock that last passed the link test; poll() remounts at this
uint32_t preferredHz = 20000000;
uint32_t lastMountTry = 0;
uint8_t curType = CARD_NONE;
uint8_t curCid[16];
bool haveCid = false;
uint32_t nMounts = 0, nMountFails = 0, nLost = 0, nIo = 0, nDegrades = 0, nVerifyFails = 0;
alignas(4) uint8_t probeBuf[512];

SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex(); // thread-safe static init
    return m;
}

uint32_t nextLower(uint32_t hz) {
    for (uint32_t l : LADDER)
        if (l < hz) return l;
    return 0;
}

void setupBus() {
    if (busReady) return;
    sdSpi.begin(B4848_SD_SCK, B4848_SD_MISO, B4848_SD_MOSI, B4848_SD_CS);
    // A floating MISO reads as random data while no card drives it. Pull it up like the card's
    // own DAT lines (the pin stays routed to SPI; this only enables the internal pull-up).
    gpio_pullup_en((gpio_num_t)B4848_SD_MISO);
    busReady = true;
}

void unmount() {
    if (isMounted) SD.end(); // sends CMD0 and unregisters the VFS; up to ~0.5 s if the card is gone
    isMounted = false;
    curHz = 0;
    curType = CARD_NONE;
    haveCid = false;
}

bool mountAt(uint32_t hz) {
    unmount();
    SD.end(); // in case a previous begin() half-succeeded
    bool ok = SD.begin(B4848_SD_CS, sdSpi, hz, "/sd", 5, false /* never format */);
    if (ok) {
        curType = SD.cardType();
        if (curType == CARD_NONE || curType == CARD_UNKNOWN) {
            SD.end();
            ok = false;
        }
    }
    // The mount read the boot sector; read it once more at the final clock to be sure.
    if (ok && !SD.readRAW(probeBuf, 0)) {
        SD.end();
        ok = false;
    }
    if (ok) {
        isMounted = true;
        curHz = hz > LADDER[0] ? LADDER[0] : hz;
        haveCid = SD.readRegister(10, false, curCid, sizeof(curCid));
        nMounts++;
    } else {
        curType = CARD_NONE;
        nMountFails++;
    }
    return ok;
}

void fillVerify(uint8_t *buf, uint32_t seed, uint32_t block) {
    uint32_t x = (seed ^ (block * 0x9E3779B9u)) | 1u;
    for (size_t i = 0; i < VERIFY_BLOCK; i += 4) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        memcpy(buf + i, &x, 4);
    }
}

// Write 32 KB and read it back at the current clock. This exercises the multi-sector write and
// read paths that fail first when the clock is too fast for a card; a mount alone only reads.
bool verifyLink() {
    uint8_t *want = (uint8_t *)malloc(VERIFY_BLOCK);
    uint8_t *got = (uint8_t *)malloc(VERIFY_BLOCK);
    if (!want || !got) {
        free(want);
        free(got);
        return true; // can't test: don't blame the card
    }
    uint32_t seed = esp_random();
    bool ok = false;
    File f = SD.open(VERIFY_PATH, FILE_WRITE);
    if (f) {
        ok = true;
        for (size_t b = 0; b < VERIFY_BLOCKS && ok; b++) {
            fillVerify(want, seed, b);
            ok = f.write(want, VERIFY_BLOCK) == VERIFY_BLOCK;
        }
        f.close();
    }
    if (ok) {
        f = SD.open(VERIFY_PATH, FILE_READ);
        ok = f && f.size() == VERIFY_BLOCK * VERIFY_BLOCKS;
        for (size_t b = 0; b < VERIFY_BLOCKS && ok; b++) {
            fillVerify(want, seed, b);
            ok = f.read(got, VERIFY_BLOCK) == (int)VERIFY_BLOCK && memcmp(want, got, VERIFY_BLOCK) == 0;
        }
        if (f) f.close();
    }
    free(want);
    free(got);
    return ok;
}

// The clock this card proved last time (0 if unknown, or the file belongs to another card).
uint32_t loadCfg() {
    if (!haveCid || !SD.exists(CFG_PATH)) return 0;
    File f = SD.open(CFG_PATH, FILE_READ);
    if (!f) return 0;
    char text[96];
    int n = f.read((uint8_t *)text, sizeof(text) - 1);
    f.close();
    if (n <= 0) return 0;
    text[n] = '\0';
    char cidHex[33];
    for (int i = 0; i < 16; i++) snprintf(cidHex + i * 2, 3, "%02X", curCid[i]);
    unsigned long hz = 0;
    char fileCid[33];
    if (sscanf(text, "cid=%32s hz=%lu", fileCid, &hz) != 2 || strcmp(fileCid, cidHex) != 0) return 0;
    return hz >= LADDER[4] && hz <= LADDER[0] ? (uint32_t)hz : 0;
}

void saveCfg(uint32_t hz) {
    if (!haveCid) return;
    char text[96];
    int p = snprintf(text, sizeof(text), "cid=");
    for (int i = 0; i < 16; i++) p += snprintf(text + p, sizeof(text) - p, "%02X", curCid[i]);
    p += snprintf(text + p, sizeof(text) - p, "\nhz=%lu\n", (unsigned long)hz);
    // Not atomic on purpose: a torn file fails to parse and is ignored (the card is re-tested).
    File f = SD.open(CFG_PATH, FILE_WRITE);
    if (!f) return;
    f.write((const uint8_t *)text, p);
    f.close();
}

// Mount at startHz or the fastest lower clock that passes the link test. Honours the clock the
// card remembered, and saves the result when it changes. With fullLadder false it gives up when
// the first mount fails (poll(): no card, don't spend seconds stepping down an empty slot).
bool mountBest(uint32_t startHz, bool fullLadder) {
    bool cfgChecked = false;
    bool first = true;
    uint32_t remembered = 0;
    for (uint32_t hz = startHz; hz; hz = nextLower(hz)) {
        if (!mountAt(hz)) {
            if (first && !fullLadder) return false;
            first = false;
            continue;
        }
        first = false;
        if (!cfgChecked) {
            cfgChecked = true;
            remembered = loadCfg();
            if (remembered && remembered < hz) {
                if (!mountAt(remembered)) continue; // hz steps down from the original start
                hz = remembered;
            }
        }
        if (verifyLink()) {
            lastGoodHz = hz;
            if (remembered != hz) saveCfg(hz);
            return true;
        }
        nVerifyFails++;
        unmount();
        // a remembered clock that fails steps down from there
    }
    return false;
}

bool degrade() {
    uint32_t lower = nextLower(curHz);
    if (!lower) return false;
    nDegrades++;
    return mountBest(lower, true);
}

bool cardGone() {
    // Two tries: one failed read can be a glitch, two in a row means the card has gone.
    if (SD.readRAW(probeBuf, 0) || SD.readRAW(probeBuf, 0)) return false;
    unmount();
    nLost++;
    lastMountTry = millis();
    return true;
}

// Run a file operation. On an I/O error: if the card is gone, unmount; else retry once at the same
// clock (a glitch), then step the clock down and retry once more (a link too fast for this card).
template <typename F> bool attempt(F op) {
    if (!isMounted) return false;
    Res r = op();
    if (r != Res::Err) return r == Res::Ok;
    nIo++;
    if (cardGone()) return false;
    r = op();
    if (r != Res::Err) return r == Res::Ok;
    nIo++;
    if (cardGone() || !degrade()) return false;
    r = op();
    if (r == Res::Err) nIo++;
    return r == Res::Ok;
}

void tmpPath(const char *path, char *out, size_t cap) { snprintf(out, cap, "%s.tmp", path); }

uint32_t atomicCrc(uint32_t seq, uint32_t len, const void *data) {
    uint32_t c = esp_rom_crc32_le(0, (const uint8_t *)&seq, sizeof(seq));
    c = esp_rom_crc32_le(c, (const uint8_t *)&len, sizeof(len));
    return esp_rom_crc32_le(c, (const uint8_t *)data, len);
}

// Read and validate one copy. buf must hold ATOMIC_MAX bytes. Returns false if missing or corrupt.
bool readCopy(const char *path, uint8_t *buf, uint32_t *len, uint32_t *seq) {
    if (!SD.exists(path)) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    AtomicHeader h;
    bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && memcmp(h.magic, ATOMIC_MAGIC, 4) == 0 &&
              h.len <= ATOMIC_MAX && f.size() == sizeof(h) + h.len && f.read(buf, h.len) == h.len &&
              atomicCrc(h.seq, h.len, buf) == h.crc;
    f.close();
    if (ok) {
        *len = h.len;
        *seq = h.seq;
    }
    return ok;
}

bool removeFileIfExists(const char *path) { return !SD.exists(path) || SD.remove(path); }

Res opMkdirs(const char *path) {
    if (!path || path[0] != '/') return Res::No;
    char buf[128];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return Res::No;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (buf[i] != '/' && buf[i] != '\0') continue;
        char c = buf[i];
        buf[i] = '\0';
        if (!SD.exists(buf) && !SD.mkdir(buf)) return Res::Err;
        buf[i] = c;
    }
    return Res::Ok;
}

Res opAppend(const char *path, const char *line) {
    File f = SD.open(path, FILE_APPEND);
    if (!f) return Res::Err;
    size_t n = strlen(line);
    bool ok = f.write((const uint8_t *)line, n) == n && f.write('\n') == 1;
    f.close(); // updates the directory entry: the line survives a power cut from here on
    return ok ? Res::Ok : Res::Err;
}

Res opWrite(const char *path, const void *data, size_t len) {
    File f = SD.open(path, FILE_WRITE);
    if (!f) return Res::Err;
    bool ok = f.write((const uint8_t *)data, len) == len;
    f.close();
    return ok ? Res::Ok : Res::Err;
}

Res opRead(const char *path, void *buf, size_t cap, size_t *outLen) {
    if (outLen) *outLen = 0;
    if (!SD.exists(path)) return Res::No; // missing is an answer, not an I/O error
    File f = SD.open(path, FILE_READ);
    if (!f) return Res::Err;
    if (f.isDirectory()) {
        f.close();
        return Res::No;
    }
    size_t n = f.read((uint8_t *)buf, cap);
    size_t want = f.size() < cap ? f.size() : cap;
    f.close();
    if (n != want) return Res::Err;
    if (outLen) *outLen = n;
    return Res::Ok;
}

Res opWriteAtomic(const char *path, const void *data, size_t len) {
    if (len > ATOMIC_MAX) return Res::No;
    char tmp[136];
    if (strlen(path) + 5 > sizeof(tmp)) return Res::No;
    tmpPath(path, tmp, sizeof(tmp));

    uint8_t *scratch = (uint8_t *)malloc(ATOMIC_MAX);
    if (!scratch) return Res::No;
    uint32_t seq = 0, l, s;
    if (readCopy(path, scratch, &l, &s)) seq = s;
    if (readCopy(tmp, scratch, &l, &s) && s > seq) seq = s;
    seq++;

    AtomicHeader h;
    memcpy(h.magic, ATOMIC_MAGIC, 4);
    h.seq = seq;
    h.len = len;
    h.crc = atomicCrc(seq, len, data);

    bool ok = false;
    File f = SD.open(tmp, FILE_WRITE);
    if (f) {
        ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) && f.write((const uint8_t *)data, len) == len;
        f.close();
    }
    // Read the temp copy back before touching the old one, so a bad write never replaces good data.
    ok = ok && readCopy(tmp, scratch, &l, &s) && s == seq && l == len && memcmp(scratch, data, len) == 0;
    free(scratch);
    // FatFs can't rename over an existing file: remove, then rename. A cut in between leaves only
    // the valid .tmp, which readAtomic() promotes.
    ok = ok && removeFileIfExists(path) && SD.rename(tmp, path);
    return ok ? Res::Ok : Res::Err;
}

Res opReadAtomic(const char *path, void *data, size_t len) {
    if (len > ATOMIC_MAX) return Res::No;
    char tmp[136];
    if (strlen(path) + 5 > sizeof(tmp)) return Res::No;
    tmpPath(path, tmp, sizeof(tmp));

    uint8_t *a = (uint8_t *)malloc(ATOMIC_MAX);
    uint8_t *b = (uint8_t *)malloc(ATOMIC_MAX);
    if (!a || !b) {
        free(a);
        free(b);
        return Res::No;
    }
    uint32_t lenA = 0, seqA = 0, lenB = 0, seqB = 0;
    bool okA = readCopy(path, a, &lenA, &seqA);
    bool okB = readCopy(tmp, b, &lenB, &seqB);

    bool useTmp = okB && (!okA || seqB > seqA);
    bool found = okA || okB;
    if (useTmp) { // NOLINT(bugprone-branch-clone): false positive, the branches differ
        // Valid newer .tmp: the cut came after it was written. Finish the replace.
        if (removeFileIfExists(path)) SD.rename(tmp, path);
    } else if (SD.exists(tmp)) {
        SD.remove(tmp); // stale or torn
    }
    uint8_t *src = useTmp ? b : a;
    uint32_t srcLen = useTmp ? lenB : lenA;
    bool ok = found && srcLen == len;
    if (ok) memcpy(data, src, len);
    free(a);
    free(b);
    return ok ? Res::Ok : Res::No; // no valid copy is an answer: corrupt copies are not retried
}

Res opRemoveTree(const char *path) {
    if (!SD.exists(path)) return Res::Ok;
    File f = SD.open(path, FILE_READ);
    if (!f) return Res::Err;
    bool isDir = f.isDirectory();
    f.close();
    if (!isDir) return SD.remove(path) ? Res::Ok : Res::Err;

    // Take the first child, close the directory, delete the child, repeat. Never deletes while a
    // directory handle is open, and needs no list of names.
    char child[160];
    for (;;) {
        File dir = SD.open(path, FILE_READ);
        if (!dir) return Res::Err;
        File c = dir.openNextFile();
        child[0] = '\0';
        if (c) {
            snprintf(child, sizeof(child), "%s", c.path());
            c.close();
        }
        dir.close();
        if (!child[0]) break;
        Res r = opRemoveTree(child);
        if (r != Res::Ok) return r;
    }
    return SD.rmdir(path) ? Res::Ok : Res::Err;
}

} // namespace

const char *cardTypeName(uint8_t t) {
    switch (t) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    case CARD_UNKNOWN: return "unknown";
    default: return "none";
    }
}

Lock::Lock() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); }
Lock::~Lock() { xSemaphoreGiveRecursive(mutex()); }

bool begin(uint32_t maxHz) {
    Lock lk;
    setupBus();
    autoMount = true;
    preferredHz = maxHz > LADDER[0] ? LADDER[0] : maxHz;
    lastGoodHz = 0;
    lastMountTry = millis();
    // Without a card this takes a few seconds (each clock waits for timeouts); poll() later
    // retries at one clock only.
    return mountBest(preferredHz, true);
}

bool remount(uint32_t hz, bool remember) {
    Lock lk;
    setupBus();
    autoMount = true;
    lastMountTry = millis();
    if (!mountAt(hz)) return false;
    if (remember) {
        if (!verifyLink()) {
            nVerifyFails++;
            unmount();
            return false;
        }
        saveCfg(curHz);
    }
    lastGoodHz = hz;
    return true;
}

void end() {
    Lock lk;
    unmount();
    autoMount = false;
}

bool forgetClock() {
    Lock lk;
    if (!isMounted) return false;
    return removeFileIfExists(CFG_PATH);
}

bool probe() {
    Lock lk;
    return isMounted && SD.readRAW(probeBuf, 0);
}

bool readRegs(CardRegs &regs) {
    Lock lk;
    regs = {};
    if (!isMounted) return false;
    regs.cidOk = SD.readRegister(10, false, regs.cid, sizeof(regs.cid));
    regs.csdOk = SD.readRegister(9, false, regs.csd, sizeof(regs.csd));
    regs.scrOk = SD.readRegister(51, true, regs.scr, sizeof(regs.scr));
    regs.statusOk = SD.readRegister(13, true, regs.status, sizeof(regs.status));
    return regs.cidOk || regs.csdOk;
}

bool poll() {
    Lock lk;
    if (isMounted) {
        cardGone();
    } else if (autoMount && millis() - lastMountTry >= RETRY_MS) {
        // A swapped card starts at the top again; the clock it remembers brings it straight down.
        mountBest(lastGoodHz ? lastGoodHz : preferredHz, false);
        lastMountTry = millis(); // after the attempt: a slow failed mount must not retry back-to-back
    }
    return isMounted;
}

bool mounted() {
    Lock lk;
    return isMounted;
}

Info info() {
    Lock lk;
    Info i = {};
    i.state = isMounted ? State::Mounted : State::NoCard;
    i.cardType = curType;
    i.freqHz = curHz;
    if (isMounted) {
        i.cardBytes = SD.cardSize();
        i.totalBytes = SD.totalBytes();
        i.usedBytes = SD.usedBytes();
    }
    i.mounts = nMounts;
    i.mountFails = nMountFails;
    i.cardLost = nLost;
    i.ioErrors = nIo;
    i.degrades = nDegrades;
    i.verifyFails = nVerifyFails;
    return i;
}

bool mkdirs(const char *path) {
    Lock lk;
    return attempt([&] { return opMkdirs(path); });
}

bool appendLine(const char *path, const char *line) {
    Lock lk;
    return attempt([&] { return opAppend(path, line); });
}

bool writeFile(const char *path, const void *data, size_t len) {
    Lock lk;
    return attempt([&] { return opWrite(path, data, len); });
}

bool readFile(const char *path, void *buf, size_t cap, size_t *outLen) {
    Lock lk;
    if (outLen) *outLen = 0;
    return attempt([&] { return opRead(path, buf, cap, outLen); });
}

bool writeAtomic(const char *path, const void *data, size_t len) {
    Lock lk;
    return attempt([&] { return opWriteAtomic(path, data, len); });
}

bool readAtomic(const char *path, void *data, size_t len) {
    Lock lk;
    return attempt([&] { return opReadAtomic(path, data, len); });
}

bool removeTree(const char *path) {
    Lock lk;
    return attempt([&] { return opRemoveTree(path); });
}

} // namespace sdcard
