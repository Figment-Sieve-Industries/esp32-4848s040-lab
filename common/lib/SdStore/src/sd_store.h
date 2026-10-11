#pragma once
// sd_store - robust microSD access for the ESP32-4848S040 (SPI mode, no card-detect pin).
// Intended to move into common/lib once proven on hardware.
//
// Rules this module enforces:
// - Its own SPIClass on the SD pins. SD.begin() calls spi.begin() with no arguments, which on the
//   S3 would grab the default FSPI pins 10-13 = LCD red bits. Pinning the bus first prevents that.
// - Init only after board4848::initDisplay(): SCK/MOSI are shared with the ST7701's 3-wire init bus.
//   After init the panel's CS (GPIO39) stays high, so it ignores SD traffic.
// - Never formats the card.
// - No card-detect: presence is checked by reading sector 0 (poll()). A removed card is unmounted;
//   poll() remounts it when it comes back.
// - Every file helper opens, writes and closes in one call, so the directory entry is up to date
//   after each call (power-cut safe up to the last completed call) and no File survives a card swap.
// - Thread safe: one recursive mutex around every card access. Use Lock for direct SD.* calls.
#include <Arduino.h>
#include <FS.h>
#include <SD.h>

namespace sdcard {

enum class State : uint8_t {
    NoCard,  // nothing answered (no card, or not inserted properly)
    Mounted, // FAT volume mounted at "/sd", usable through SD.* and the helpers below
};

struct Info {
    State state;
    uint8_t cardType;    // sdcard_type_t: CARD_NONE, CARD_MMC, CARD_SD (SDSC), CARD_SDHC, CARD_UNKNOWN
    uint32_t freqHz;     // SPI clock in use (0 if not mounted)
    uint64_t cardBytes;  // raw card capacity
    uint64_t totalBytes; // FAT volume size
    uint64_t usedBytes;
    uint32_t mounts;     // successful mounts since boot
    uint32_t mountFails; // failed mount attempts since boot
    uint32_t cardLost;   // times poll() found a mounted card gone
    uint32_t ioErrors;   // failed I/O attempts while mounted (each retry counts)
    uint32_t degrades;   // times an error made the clock step down
    uint32_t verifyFails; // link tests that failed at some clock
};

// Card type as text: "MMC", "SDSC", "SDHC/SDXC", "unknown", "none".
const char *cardTypeName(uint8_t cardType);

// Set up the SPI bus and mount at the fastest clock that works for this card, up to maxHz.
// Call once, after board4848::initDisplay(). Returns true if mounted. Without a card it returns false
// (after a few seconds); keep calling poll() and the card is mounted when inserted.
//
// Clock selection: the clocks are 25, 20, 10, 4 and 1 MHz. A card's rated clock (CSD) is
// almost always "25 MHz" and isn't trustworthy, so each candidate clock is proven by a link test
// (write and read back 32 KB in /.sdstore.chk). The clock that passed is remembered on the card
// itself in /.sdstore.cfg, keyed by the card's CID, so the next mount starts there. During use, an
// I/O error on a present card is retried once at the same clock, then once a step lower (and
// that lower clock is remembered). Nothing is written to the ESP32's flash.
bool begin(uint32_t maxHz = 20000000);

// Unmount (if mounted) and mount again at exactly hz, without stepping down. For speed tests.
// remember = true: also run the link test and save hz as this card's clock.
bool remount(uint32_t hz, bool remember = false);

// Delete this card's remembered clock, so the next begin() tests from maxHz again.
bool forgetClock();

// Unmount. The bus stays configured; poll() will NOT remount until begin() or remount() is called.
void end();

// Call about once a second from one task. Checks the card is still there (one 512-byte read,
// ~1 ms at 20 MHz) and unmounts if not; if unmounted, retries the mount every 2 s.
// Returns mounted().
bool poll();

bool mounted();
Info info(); // totalBytes/usedBytes walk the FAT the first time on a big card; avoid calling every frame

// Raw presence check: reads sector 0. Does not change the mount state.
bool probe();

// The card's own identity and capability registers, raw (decode with sdtests::cardReport()).
struct CardRegs {
    uint8_t cid[16];    // manufacturer, OEM, product name, revision, serial, date
    uint8_t csd[16];    // capacity, rated max clock, access and write timing
    uint8_t scr[8];     // SD spec version, bus widths
    uint8_t status[64]; // speed class, UHS/video/app class, AU size
    bool cidOk, csdOk, scrOk, statusOk;
};
// Reads all four over SPI (needs the patched SD library in lib/SD). False if not mounted.
bool readRegs(CardRegs &regs);

// ---- File helpers. Paths are absolute on the card ("/log/2026-10-11.csv"). All return false when
// unmounted, on a "no" answer (missing file, no valid copy, bad argument) or on an I/O error that
// survived the retries above. appendLine() may leave a partial line if it fails mid-write and is
// retried, so log readers should skip lines that don't parse.

// Create every missing directory in path ("/a/b/c"). OK if they already exist.
bool mkdirs(const char *path);

// Append line plus '\n' to path, creating the file (not its directories) if needed.
bool appendLine(const char *path, const char *line);

// Write a whole small file (replaces it). Not atomic: use writeAtomic() for state you must not lose.
bool writeFile(const char *path, const void *data, size_t len);

// Read up to cap bytes of a file into buf. *outLen gets the byte count. False if missing.
bool readFile(const char *path, void *buf, size_t cap, size_t *outLen);

// Power-cut-safe replace of a small binary record (e.g. a struct of today's totals), max 4 KB.
// On-card format: 16-byte header {magic "SDA1", uint32 seq, uint32 len, uint32 crc32} + payload.
// Sequence: write path.tmp and close, remove path, rename path.tmp -> path.
// A cut at any point leaves at least one valid copy; readAtomic() picks the newest valid one.
bool writeAtomic(const char *path, const void *data, size_t len);

// Read what writeAtomic() wrote. Checks path and path.tmp, returns the valid copy with the higher seq,
// and tidies up (a valid orphaned .tmp becomes path; a corrupt .tmp is removed).
// len must equal the stored length. False if no valid copy.
bool readAtomic(const char *path, void *data, size_t len);

// Remove a file or a directory tree. True if it is gone afterwards (including "never existed").
bool removeTree(const char *path);

// Hold this to call SD.* directly (e.g. open a File for streaming) without racing poll().
// Recursive, so helpers can be called while holding it. Don't hold it for long: poll() waits.
class Lock {
public:
    Lock();
    ~Lock();
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
};

} // namespace sdcard
