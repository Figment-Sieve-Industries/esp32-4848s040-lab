// SD card task for project 05 - see history.h.
// One task on core 0 owns every card access: mount/hot-swap polling, the per-minute CSV log,
// the energy-baseline backup and the settings file. Other code hands data over under s_lock.
#include "history.h"

#include <Board4848S040.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sd_store.h>

#include "powerwall.h"

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

constexpr uint32_t SETTINGS_MAGIC = 0x53455431; // "SET1"
struct SettingsFile {
    uint32_t magic;
    AppSettings s;
};

SemaphoreHandle_t s_lock;
SdStatus s_status;
AppSettings s_settings;
bool s_settingsLoaded = false;   // read from a card (or none found) since boot
bool s_settingsDirty = false;
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

struct Guard {
    Guard() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_lock); }
};

void loadSettings() {
    SettingsFile f;
    bool ok = sdcard::readAtomic(SETTINGS_PATH, &f, sizeof(f)) && f.magic == SETTINGS_MAGIC;
    uint8_t level;
    {
        Guard g;
        s_settingsLoaded = true;
        if (!ok || s_settingsDirty) return; // keep defaults, or a change made before the card was read
        if (f.s.brightness < B4848_LCD_BL_MIN) f.s.brightness = B4848_LCD_BL_MIN;
        s_settings = f.s;
        level = s_settings.brightness;
    }
    board4848::setBacklight(level);
    Serial.printf("SD: settings loaded (brightness %u)\n", level);
}

void saveSettingsIfDue() {
    SettingsFile f;
    {
        Guard g;
        if (!s_settingsDirty || millis() - s_settingsChangedMs < SETTINGS_SETTLE_MS) return;
        s_settingsDirty = false;
        f.magic = SETTINGS_MAGIC;
        f.s = s_settings;
    }
    if (!sdcard::mkdirs(DIR) || !sdcard::writeAtomic(SETTINGS_PATH, &f, sizeof(f))) {
        Guard g;
        s_settingsDirty = true; // retry next round (e.g. no card yet)
    }
}

void saveBaselineIfDirty() {
    uint8_t copy[BASELINE_MAX];
    size_t len;
    {
        Guard g;
        if (!s_baselineDirty) return;
        len = s_baselineLen;
        memcpy(copy, s_baseline, len);
        s_baselineDirty = false;
    }
    bool ok = sdcard::mkdirs(DIR) && sdcard::writeAtomic(BASELINE_PATH, copy, len);
    Guard g;
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
    Guard g;
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
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 124) return; // clock not synced yet
    int key = tm.tm_yday * 1440 + tm.tm_hour * 60 + tm.tm_min;
    if (key != s_acc.minuteKey) {
        flushMinute();
        s_acc = MinuteAcc();
        s_acc.minuteKey = key;
        s_acc.tm = tm;
    }
    PwData d = powerwallData();
    if (!d.valid || millis() - d.updatedMs > PW_STALE_MS || d.updatedMs == s_acc.lastUpdatedMs) return;
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
    Guard g;
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
        Guard g;
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
                    Guard g;
                    if (s_baselineLen) s_baselineDirty = true; // a swapped card gets today's baseline too
                }
                refreshStatus(true);
                lastInfo = now;
            }
        }
        sampleMinute();
        if (mounted) {
            saveBaselineIfDirty();
            saveSettingsIfDue();
        }
        if (now - lastInfo >= INFO_MS) {
            lastInfo = now;
            refreshStatus(true);
        } else {
            Guard g;
            s_status.lastWriteAgeS = s_haveWrite ? (millis() - s_lastWriteMs) / 1000 : UINT32_MAX;
        }
    }
}

} // namespace

void historyStart() {
    s_lock = xSemaphoreCreateMutex();
    // Idle priority like the network tasks; 6 KB covers FatFs path handling and snprintf.
    xTaskCreatePinnedToCore(sdTask, "sd", 6144, nullptr, tskIDLE_PRIORITY, nullptr, 0);
}

SdStatus historyStatus() {
    if (!s_lock) return SdStatus();
    Guard g;
    return s_status;
}

void historySaveBaseline(const void *data, size_t len) {
    if (!s_lock || len > BASELINE_MAX) return;
    Guard g;
    memcpy(s_baseline, data, len);
    s_baselineLen = len;
    s_baselineDirty = true;
}

bool historyLoadBaseline(void *data, size_t len) {
    if (!s_lock) return false;
    return sdcard::readAtomic(BASELINE_PATH, data, len);
}

AppSettings settingsGet() {
    if (!s_lock) return AppSettings();
    Guard g;
    return s_settings;
}

void settingsSetBrightness(uint8_t level) {
    if (level < B4848_LCD_BL_MIN) level = B4848_LCD_BL_MIN;
    board4848::setBacklight(level);
    if (!s_lock) return;
    Guard g;
    s_settings.brightness = level;
    s_settingsDirty = true;
    s_settingsChangedMs = millis();
}
