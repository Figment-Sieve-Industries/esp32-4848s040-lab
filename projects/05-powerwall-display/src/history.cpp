// SD card task for project 05 - see history.h.
// One task on core 0 owns every card access: mount/hot-swap polling, the per-minute CSV log,
// the energy-baseline backup and the settings file. Other code hands data over under s_lock.
#include "history.h"

#include <Board4848S040.h>
#include <atomic>
#include <SD.h>
#include <sd_store.h>

#include "cache.h"
#include "day_history.h"
#include "powerwall.h"
#include "shared_state.h"
#include "time_util.h"

namespace {

constexpr const char *DIR = "/powerwall";
constexpr const char *BASELINE_PATH = "/powerwall/baseline.bin";
constexpr const char *SETTINGS_PATH = "/powerwall/settings.bin";
constexpr const char *CSV_HEADER = "time,solar_w,home_w,grid_w,battery_w,soc_pct,voltage_v,grid_up";
constexpr uint32_t LOOP_MS = 250;
constexpr uint32_t POLL_MS = 2000;         // card presence / remount
constexpr uint32_t INFO_MS = 10000;        // used/total space refresh
constexpr uint32_t SETTINGS_SETTLE_MS = 1500; // save brightness once the slider has stopped moving
constexpr size_t BASELINE_MAX = 512;

// settings.bin: { magic, brightness, reserved, screenMask } = 8 bytes (settings_codec.h). "SET3" = slider percent +
// screen mask; "SET2" = slider percent; "SET1" (older firmware) = raw 8-bit duty of the old linear slider,
// converted on load. The size never changed, so old files still read (their mask bytes are ignored).
struct SettingsFile {
    uint32_t magic;
    uint8_t value;
    uint8_t reserved;
    uint16_t screenMask;
};
static_assert(sizeof(SettingsFile) == 8, "settings.bin record size must stay 8 bytes");

Mutex s_lock; // static init: the getters work (defaults) before historyStart()
bool s_started = false;
SdStatus s_status;
AppSettings s_settings;
bool s_settingsLoaded = false;   // read from a card (or none found) since boot
bool s_settingsDirty = false;
bool s_touchedBrightness = false; // changed by the user before the card was read: wins over the file
bool s_touchedMask = false;
TaskHandle_t s_sdTask = nullptr;
std::atomic<bool> s_cacheStatusReq{false}; // serial `c`: list the cache from the SD task, not the UI thread
uint32_t s_settingsChangedMs = 0;
uint8_t s_baseline[BASELINE_MAX];
size_t s_baselineLen = 0;
bool s_baselineDirty = false;
uint32_t s_lastWriteMs = 0;
bool s_haveWrite = false;

// Minute averages: summed over fresh gateway readings within the current wall-clock minute
struct MinuteAcc {
    int minuteKey = -1;          // yday * 1440 + hour * 60 + min
    struct tm tm = {};
    uint32_t lastUpdatedMs = 0;  // PwData.updatedMs of the last reading added
    uint32_t n = 0;
    double solar = 0, home = 0, grid = 0, batt = 0, soc = 0, volt = 0;
    bool gridDown = false;       // any off-grid reading in the minute
} s_acc;

void applyBacklight(int pct) {
    board4848::setBacklightDuty(
        blcurve::dutyForPercent(pct, board4848::backlightMinDuty(), board4848::backlightDutyMax()));
}

void loadSettings() {
    SettingsFile f;
    memset(&f, 0, sizeof(f));
    setcodec::Decoded d;
    bool takeBrightness = false;
    // Wrong size, unknown magic or an out-of-range value all fall back to the defaults.
    bool ok = sdcard::readAtomic(SETTINGS_PATH, &f, sizeof(f)) &&
              setcodec::decodeSettings(f.magic, f.value, f.screenMask, B4848_LCD_BL_MIN, d);
    {
        LockGuard g(s_lock);
        s_settingsLoaded = true;
        if (!ok) return; // keep defaults (and any change made before the card was read)
        // A field the user already changed keeps its new value; the other one comes from the card. The
        // merged record is saved again (s_settingsDirty is already set by the change).
        if (!s_touchedBrightness) s_settings.brightnessPct = static_cast<uint8_t>(d.brightnessPct);
        if (!s_touchedMask) s_settings.screenMask = d.screenMask;
        d.brightnessPct = s_settings.brightnessPct;
        d.screenMask = s_settings.screenMask;
        takeBrightness = !s_touchedBrightness;
    }
    if (takeBrightness) applyBacklight(d.brightnessPct);
    Serial.printf("SD: settings loaded (%s, brightness %d%%, screens 0x%03x)\n",
                  f.magic == setcodec::SETTINGS_MAGIC_V3   ? "v3"
                  : f.magic == blcurve::SETTINGS_MAGIC_V2 ? "v2"
                                                          : "v1 converted",
                  d.brightnessPct, (unsigned)d.screenMask);
}

void saveSettingsIfDue() {
    SettingsFile f;
    {
        LockGuard g(s_lock);
        if (!s_settingsDirty || millis() - s_settingsChangedMs < SETTINGS_SETTLE_MS) return;
        s_settingsDirty = false;
        memset(&f, 0, sizeof(f)); // no stray padding bytes on the card
        f.magic = setcodec::SETTINGS_MAGIC_V3;
        f.value = s_settings.brightnessPct;
        f.screenMask = s_settings.screenMask;
    }
    if (!sdcard::mkdirs(DIR) || !sdcard::writeAtomic(SETTINGS_PATH, &f, sizeof(f))) {
        LockGuard g(s_lock);
        s_settingsDirty = true; // retry next round (e.g. no card yet)
    }
}

void saveBaselineIfDirty() {
    uint8_t copy[BASELINE_MAX];
    size_t len;
    {
        LockGuard g(s_lock);
        if (!s_baselineDirty) return;
        len = s_baselineLen;
        memcpy(copy, s_baseline, len);
        s_baselineDirty = false;
    }
    bool ok = sdcard::mkdirs(DIR) && sdcard::writeAtomic(BASELINE_PATH, copy, len);
    LockGuard g(s_lock);
    if (ok) s_status.baselineSaved = true;
    else s_baselineDirty = true; // retry next round (with a newer copy if one arrived meanwhile)
}

void logPath(const struct tm &tm, char *out, size_t n) {
    snprintf(out, n, "%s/%04d-%02d-%02d.csv", DIR, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

// Write the finished minute (if it had any fresh readings) to that day's CSV.
void flushMinute() {
    if (s_acc.n == 0) return;
    char path[32];
    logPath(s_acc.tm, path, sizeof(path));
    const double n = s_acc.n;
    char line[128];
    snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d,%.0f,%.0f,%.0f,%.0f,%.1f,%.1f,%d",
             s_acc.tm.tm_year + 1900, s_acc.tm.tm_mon + 1, s_acc.tm.tm_mday, s_acc.tm.tm_hour, s_acc.tm.tm_min,
             s_acc.solar / n, s_acc.home / n, s_acc.grid / n, s_acc.batt / n, s_acc.soc / n, s_acc.volt / n,
             s_acc.gridDown ? 0 : 1);
    bool ok = sdcard::mounted() && sdcard::mkdirs(DIR);
    if (ok) {
        bool exists;
        {
            sdcard::Lock lk;
            exists = SD.exists(path);
        }
        if (!exists) ok = sdcard::appendLine(path, CSV_HEADER);
    }
    ok = ok && sdcard::appendLine(path, line);
    LockGuard g(s_lock);
    if (strcmp(s_status.logFile, path) != 0) {
        strlcpy(s_status.logFile, path, sizeof(s_status.logFile));
        s_status.linesToday = 0;
    }
    if (ok) {
        s_status.linesToday++;
        s_lastWriteMs = millis();
        s_haveWrite = true;
    }
}

// Fold the latest gateway reading into the current minute; flush when the minute changes.
void sampleMinute() {
    time_t now = time(nullptr);
    if (!tu::clockValid(now)) return; // clock not synced yet
    struct tm tm;
    localtime_r(&now, &tm);
    int key = tm.tm_yday * 1440 + tm.tm_hour * 60 + tm.tm_min;
    if (key != s_acc.minuteKey) {
        flushMinute();
        s_acc = MinuteAcc();
        s_acc.minuteKey = key;
        s_acc.tm = tm;
    }
    PwData d = powerwallData();
    if (isStale(d.valid, d.updatedMs, PW_STALE_MS) || d.updatedMs == s_acc.lastUpdatedMs) return;
    s_acc.lastUpdatedMs = d.updatedMs;
    s_acc.n++;
    s_acc.solar += d.solarW;
    s_acc.home += d.loadW;
    s_acc.grid += d.siteW;
    s_acc.batt += d.batteryW;
    s_acc.soc += d.soc;
    s_acc.volt += d.voltageV;
    if (!d.gridConnected) s_acc.gridDown = true;
}

void refreshStatus(bool withSpace) {
    sdcard::Info in = sdcard::info();
    LockGuard g(s_lock);
    s_status.mounted = in.state == sdcard::State::Mounted;
    strlcpy(s_status.cardType, s_status.mounted ? sdcard::cardTypeName(in.cardType) : "", sizeof(s_status.cardType));
    s_status.clockMHz = in.freqHz / 1000000;
    if (withSpace || !s_status.mounted) {
        s_status.totalBytes = in.totalBytes;
        s_status.usedBytes = in.usedBytes;
    }
    s_status.ioErrors = in.ioErrors;
    s_status.cardLost = in.cardLost;
    s_status.clockStepDowns = in.degrades;
    s_status.lastWriteAgeS = s_haveWrite ? (millis() - s_lastWriteMs) / 1000 : UINT32_MAX;
}

void sdTask(void *) {
    bool mounted = sdcard::begin(20000000);
    Serial.printf("SD: %s\n", mounted ? "card mounted" : "no card (polling)");
    if (mounted) loadSettings();
    refreshStatus(true);
    {
        LockGuard g(s_lock);
        s_status.firstMountDone = true;
    }
    uint32_t lastPoll = millis(), lastInfo = millis();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
        uint32_t now = millis();
        if (now - lastPoll >= POLL_MS) {
            lastPoll = now;
            bool m = sdcard::poll();
            if (m != mounted) {
                mounted = m;
                Serial.printf("SD: card %s\n", m ? "mounted" : "removed");
                if (m) {
                    if (!s_settingsLoaded) loadSettings();
                    LockGuard g(s_lock);
                    if (s_baselineLen) s_baselineDirty = true; // a swapped card gets today's baseline too
                }
                refreshStatus(true);
                lastInfo = now;
            }
        }
        sampleMinute();
        if (s_cacheStatusReq.exchange(false)) cache::printStatus();
        dayhist::dayHistorySdTick(mounted); // history screen: saves due day records, serves reads
        if (mounted) {
            saveBaselineIfDirty();
            saveSettingsIfDue();
        }
        if (now - lastInfo >= INFO_MS) {
            lastInfo = now;
            refreshStatus(true);
        } else {
            LockGuard g(s_lock);
            s_status.lastWriteAgeS = s_haveWrite ? (millis() - s_lastWriteMs) / 1000 : UINT32_MAX;
        }
    }
}

} // namespace

void historyStart() {
    s_started = true;
    // Idle priority like the network tasks; 6 KB covers FatFs path handling and snprintf.
    xTaskCreatePinnedToCore(sdTask, "sd", 6144, nullptr, tskIDLE_PRIORITY, &s_sdTask, 0);
}

void historyRequestCacheStatus() { s_cacheStatusReq.store(true); }

uint32_t historyTaskStackFree() { return s_sdTask ? uxTaskGetStackHighWaterMark(s_sdTask) : 0; }

SdStatus historyStatus() {
    LockGuard g(s_lock);
    return s_status;
}

void historySaveBaseline(const void *data, size_t len) {
    if (len > BASELINE_MAX) return;
    LockGuard g(s_lock);
    memcpy(s_baseline, data, len);
    s_baselineLen = len;
    s_baselineDirty = true;
}

bool historyLoadBaseline(void *data, size_t len) {
    if (!s_started) return false; // no SD task, no card access
    return sdcard::readAtomic(BASELINE_PATH, data, len);
}

AppSettings settingsGet() {
    LockGuard g(s_lock);
    return s_settings;
}

void settingsApplyBacklight() {
    int pct;
    {
        LockGuard g(s_lock);
        pct = s_settings.brightnessPct;
    }
    applyBacklight(pct);
}

void settingsSetBrightness(int pct) {
    pct = blcurve::clampPct(pct);
    applyBacklight(pct);
    LockGuard g(s_lock);
    s_settings.brightnessPct = static_cast<uint8_t>(pct);
    s_touchedBrightness = true;
    s_settingsDirty = true;
    s_settingsChangedMs = millis();
}

void settingsSetScreenEnabled(int screenIndex, bool on) {
    LockGuard g(s_lock);
    const uint16_t m = setcodec::withScreen(s_settings.screenMask, screenIndex, on);
    if (m == s_settings.screenMask) return;
    s_settings.screenMask = m;
    s_touchedMask = true;
    s_settingsDirty = true;
    s_settingsChangedMs = millis();
}
