// Per-day energy history - see day_history.h for the contract and day_history_calc.h for the pure logic.
//
// UI thread (core 1): dayHistoryUpdate folds fresh readings into today's record (RTC memory), queues
// copies for saving; dayHistoryRequest / dayHistoryView pick and copy the view.
// SD task (core 0): dayHistorySdTick saves queued records into /powerwall/days/YYYY-MM.bin, reads month
// files into a small PSRAM cache and rebuilds the "base" view (everything except today's live slot).
// Shared state is guarded by s_mu; the card is only touched from the SD task.
//
// Month file: 31 slots x sizeof(DayRecord) = 13,888 bytes, raw, no file header. Each slot carries its own
// CRC, so a damaged slot only loses that day. The file is replaced by writing path.tmp whole, then
// remove + rename (sdcard::writeAtomic is limited to 4 KB, so it cannot be used here); a cut between the
// remove and the rename leaves a complete .tmp that the reader uses.
#include "day_history.h"

#include <Arduino.h>
#include <Board4848S040_pins.h>
#include <SD.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include <atomic>
#include <esp_attr.h>
#include <math.h>
#include <new>
#include <sd_store.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <type_traits>

#include "day_history_calc.h"
#include "mem_util.h"
#include "shared_state.h"
#include "time_util.h"

namespace dayhist {

namespace {

using calc::Prev;

constexpr size_t REC_SZ = sizeof(DayRecord);
constexpr size_t MONTH_BYTES = MAX_SLOTS * REC_SZ;
constexpr uint32_t RTC_MAGIC = 0x44485432; // "DHT2"
constexpr uint32_t SAVE_EVERY_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t RESTORE_WAIT_MS = 20000; // wait this long for a card after a power cut
constexpr uint32_t SAVE_RETRY_MS = 10000;
constexpr int PEND_MAX = 3;
constexpr int CACHE_MONTHS = 3;
constexpr int LOAD_FAIL_LIMIT = 5; // read errors on one month before it is treated as empty
constexpr const char *DIR = "/powerwall/days";

// ---- Record helpers -------------------------------------------------------------------------------------
bool validDate(int32_t d) {
    int y = calc::ymdYear(d), m = calc::ymdMonth(d), dd = calc::ymdDay(d);
    return y >= 2024 && y < 2200 && m >= 1 && m <= 12 && dd >= 1 && dd <= calc::daysInMonth(y, m);
}
uint32_t recCrc(const DayRecord &r) { return calc::crc32(&r, offsetof(DayRecord, crc)); }
void sealRec(DayRecord &r) { r.crc = recCrc(r); }
bool recOk(const DayRecord &r) {
    return r.magic == RECORD_MAGIC && (r.flags & DAY_PRESENT) && validDate(r.date) && r.crc == recCrc(r);
}

// ---- Today's record in RTC memory ---------------------------------------------------------------------
// Hashed as raw bytes including padding, like energy.cpp's Baseline (guide Q-FW-05): startDayRec()
// memset()s the whole struct, later updates assign members only, loaded records are byte-copied in.
struct TodayRtc {
    uint32_t magic;
    Prev prev;      // previous reading (survives the date change: the first reading after midnight has a delta)
    DayRecord rec;
    uint32_t check;
};
static_assert(std::is_standard_layout<TodayRtc>::value, "TodayRtc is hashed and zeroed as raw bytes");
static_assert(std::is_trivially_copyable<TodayRtc>::value, "TodayRtc is hashed and zeroed as raw bytes");

RTC_NOINIT_ATTR TodayRtc rtc;

uint32_t rtcCheck() {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&rtc);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(TodayRtc, check); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
bool rtcValid() { return rtc.magic == RTC_MAGIC && rtc.check == rtcCheck() && rtc.rec.magic == RECORD_MAGIC; }
void rtcSeal() {
    sealRec(rtc.rec);
    rtc.check = rtcCheck();
}

void startDayRec(int32_t ymd, bool keepPrev) {
    Prev keep = rtc.prev;
    memset(&rtc, 0, sizeof(rtc));
    rtc.magic = RTC_MAGIC;
    rtc.rec.magic = RECORD_MAGIC;
    rtc.rec.date = ymd;
    memset(rtc.rec.soc, SOC_NONE, sizeof(rtc.rec.soc));
    if (keepPrev) {
        rtc.prev.have = keep.have;
        rtc.prev.t = keep.t;
        for (int i = 0; i < calc::CHANNELS; i++) rtc.prev.c[i] = keep.c[i];
    }
    rtcSeal();
}

// ---- State shared between the UI thread and the SD task (s_mu) ----------------------------------------
Mutex s_mu;

struct Pending {
    DayRecord rec;
    uint32_t ver;
};
Pending s_pend[PEND_MAX];
int s_pendN = 0;
uint32_t s_verCtr = 0;
DayRecord s_lastDone; // the day that just ended, until its save has landed in the cache
bool s_haveLastDone = false;

enum class Restore : uint8_t { None, Wanted, Done, GaveUp };
Restore s_restore = Restore::None;
int32_t s_restoreDate = 0;
uint32_t s_restoreStartMs = 0;
bool s_restoreHave = false;
DayRecord s_restoreRec;

bool s_reqValid = false;
Mode s_reqMode = Mode::Day;
int s_reqOffset = 0;
View s_base;            // the request's view without today's live slot
int32_t s_baseToday = 0; // the date s_base was built for
bool s_awaiting = false; // s_base is waiting for month files

struct MonthEntry {
    int ym = 0;          // yyyymm, 0 = free
    uint32_t tick = 0;
    DayRecord *recs = nullptr; // PSRAM, MAX_SLOTS records, invalid slots zeroed
};
MonthEntry s_cache[CACHE_MONTHS];
uint32_t s_tick = 0;

uint32_t s_saves = 0, s_loads = 0, s_errors = 0, s_lastSaveMs = 0;
bool s_haveSave = false;
std::atomic_bool s_mounted{false};

// ---- UI-thread-only state --------------------------------------------------------------------------------
const char *s_rtcState = "init";
bool s_haveUpd = false;
uint32_t s_lastUpd = 0;
int s_lastQueueHour = -1;
uint32_t s_lastQueueMs = 0;
uint32_t s_viewSeq = 1, s_lastSig = 0;
bool s_haveSig = false;

// ---- SD-task-only state ----------------------------------------------------------------------------------
uint8_t *s_work = nullptr; // MONTH_BYTES scratch for reads
bool s_prevMounted = false;
uint32_t s_retryAt = 0;
int s_failYm = 0, s_failN = 0;
int s_restoreFails = 0;

// ---- Time -----------------------------------------------------------------------------------------------
bool localNow(int32_t &ymd, int &hour, time_t &now) {
    now = time(nullptr);
    if (!tu::clockValid(now)) return false;
    struct tm tm;
    localtime_r(&now, &tm);
    ymd = calc::ymdPack(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    hour = tm.tm_hour;
    return true;
}

// ---- Cache (call with s_mu held, except where noted) ---------------------------------------------------
MonthEntry *findMonth(int ym) {
    for (MonthEntry &e : s_cache)
        if (e.ym == ym && e.recs) return &e;
    return nullptr;
}

const DayRecord *findRec(int32_t ymd) {
    MonthEntry *e = findMonth(calc::monthOf(ymd));
    if (!e) return nullptr;
    e->tick = ++s_tick;
    const DayRecord &r = e->recs[calc::slotOfDate(ymd)];
    return (r.magic == RECORD_MAGIC && r.date == ymd && (r.flags & DAY_PRESENT)) ? &r : nullptr;
}

// ---- Building the base view (s_mu held) ------------------------------------------------------------------
void fillFromRecord(View &v, int i, const DayRecord &r) {
    v.solar[i] = r.solar;
    v.home[i] = r.home;
    v.imp[i] = r.imp;
    v.exp[i] = r.exp;
    v.have[i] = true;
}

void buildBaseLocked(bool loading, bool noCard) {
    View &v = s_base;
    new (&v) View();
    v.mode = s_reqMode;
    v.offset = calc::clampOffset(static_cast<int>(s_reqMode), s_reqOffset);
    v.canNext = v.offset < 0;
    v.loading = loading;
    v.noCard = noCard;
    memset(v.soc, SOC_NONE, sizeof(v.soc));
    s_awaiting = loading;
    int32_t today;
    int hour;
    time_t now;
    if (!localNow(today, hour, now)) { // clock not synced: an empty skeleton
        s_baseToday = 0;
        v.count = s_reqMode == Mode::Day ? 24 : s_reqMode == Mode::Week ? 7 : 31;
        v.loading = false;
        s_awaiting = false;
        snprintf(v.label, sizeof(v.label), "%s",
                 s_reqMode == Mode::Day ? "Today" : s_reqMode == Mode::Week ? "This week" : "This month");
        return;
    }
    s_baseToday = today;
    const int span = static_cast<int>(s_reqMode);
    v.count = calc::periodDates(span, today, v.offset, v.dates);
    calc::periodLabel(span, v.offset, v.dates, v.count, v.label, sizeof(v.label));
    v.firstDay = calc::ymdDay(v.dates[0]);
    if (s_reqMode == Mode::Day) {
        const DayRecord *r = v.dates[0] < today ? findRec(v.dates[0]) : nullptr;
        if (r) {
            for (int h = 0; h < 24; h++) {
                v.solar[h] = r->hSolar[h];
                v.home[h] = r->hHome[h];
                v.imp[h] = r->hImp[h];
                v.exp[h] = r->hExp[h];
                v.soc[h] = r->soc[h];
                v.have[h] = true;
            }
            v.partial = (r->flags & DAY_PARTIAL) != 0;
            v.totSolar = r->solar;
            v.totHome = r->home;
            v.totExp = r->exp;
            v.totImp = r->imp;
            v.haveTotals = true;
        }
    } else {
        for (int i = 0; i < v.count; i++) {
            const DayRecord *r = v.dates[i] < today ? findRec(v.dates[i]) : nullptr;
            if (r) fillFromRecord(v, i, *r);
        }
    }
}

// Re-evaluates the current request: instant when every month it needs is cached, else a loading skeleton.
void applyRequestLocked() {
    int32_t today;
    int hour;
    time_t now;
    bool loading = false;
    if (localNow(today, hour, now)) {
        int32_t dates[MAX_SLOTS];
        const int span = static_cast<int>(s_reqMode);
        int cnt = calc::periodDates(span, today, s_reqOffset, dates);
        int need[2];
        int n = calc::monthsNeeded(span, dates, cnt, today, need);
        for (int k = 0; k < n; k++)
            if (!findMonth(need[k])) loading = true;
    }
    buildBaseLocked(loading, false);
}

// ---- Final view (UI thread, no lock) ---------------------------------------------------------------------
void overlay(View &v, const DayRecord &r, int32_t today, int hour) {
    if (v.mode == Mode::Day) {
        if (v.dates[0] != r.date) return;
        const bool isToday = r.date == today;
        for (int h = 0; h < 24; h++) {
            v.solar[h] = r.hSolar[h];
            v.home[h] = r.hHome[h];
            v.imp[h] = r.hImp[h];
            v.exp[h] = r.hExp[h];
            v.soc[h] = r.soc[h];
            v.have[h] = !isToday || h <= hour;
        }
        v.partial = (r.flags & DAY_PARTIAL) != 0;
        v.totSolar = r.solar;
        v.totHome = r.home;
        v.totExp = r.exp;
        v.totImp = r.imp;
        v.haveTotals = true;
        return;
    }
    for (int i = 0; i < v.count; i++)
        if (v.dates[i] == r.date) fillFromRecord(v, i, r);
}

void finalizeView(View &v, const DayRecord *todayRec, const DayRecord *done, int32_t today, int hour) {
    if (today == 0) return;
    if (v.mode == Mode::Day) {
        if (v.dates[0] == today) {
            v.nowSlot = hour;
            for (int h = 0; h < 24; h++) v.future[h] = h > hour;
        }
    } else {
        for (int i = 0; i < v.count; i++) {
            v.future[i] = v.dates[i] > today;
            if (v.dates[i] == today) v.nowSlot = i;
        }
    }
    if (done) overlay(v, *done, today, hour);
    if (todayRec) overlay(v, *todayRec, today, hour);
    if (v.mode != Mode::Day) {
        v.totSolar = v.totHome = v.totExp = v.totImp = 0;
        v.haveTotals = false;
        for (int i = 0; i < v.count; i++) {
            if (!v.have[i]) continue;
            v.totSolar += v.solar[i];
            v.totHome += v.home[i];
            v.totExp += v.exp[i];
            v.totImp += v.imp[i];
            v.haveTotals = true;
        }
    }
}

uint32_t mix(uint32_t h, int32_t x) {
    for (int k = 0; k < 4; k++) h = (h ^ (static_cast<uint32_t>(x) >> (8 * k) & 0xFF)) * 16777619u;
    return h;
}
int32_t tenth(float x) { return static_cast<int32_t>(lroundf(x * 10.0f)); }

// What the screen can show, at display precision (0.1 kWh): changes here bump View::seq.
uint32_t viewSignature(const View &v) {
    uint32_t h = 2166136261u;
    h = mix(h, static_cast<int32_t>(v.mode));
    h = mix(h, v.offset);
    h = mix(h, v.count);
    h = mix(h, v.nowSlot);
    h = mix(h, (v.loading ? 1 : 0) | (v.noCard ? 2 : 0) | (v.canNext ? 4 : 0) | (v.partial ? 8 : 0) |
                   (v.haveTotals ? 16 : 0));
    for (const char *c = v.label; *c; c++) h = mix(h, *c);
    h = mix(h, v.dates[0]);
    for (int i = 0; i < v.count; i++) {
        h = mix(h, (v.have[i] ? 1 : 0) | (v.future[i] ? 2 : 0));
        h = mix(h, tenth(v.solar[i]));
        h = mix(h, tenth(v.home[i]));
        h = mix(h, tenth(v.imp[i]));
        h = mix(h, tenth(v.exp[i]));
    }
    for (int i = 0; i < 24; i++) h = mix(h, v.soc[i]);
    h = mix(h, tenth(v.totSolar));
    h = mix(h, tenth(v.totHome));
    h = mix(h, tenth(v.totExp));
    h = mix(h, tenth(v.totImp));
    return h;
}

// ---- Saving queue (s_mu held) ----------------------------------------------------------------------------
void queueSaveLocked(const DayRecord &r) {
    for (int i = 0; i < s_pendN; i++) {
        if (s_pend[i].rec.date == r.date) {
            memcpy(&s_pend[i].rec, &r, REC_SZ);
            s_pend[i].ver = ++s_verCtr;
            return;
        }
    }
    if (s_pendN == PEND_MAX) { // card absent for long: drop the oldest
        for (int i = 1; i < PEND_MAX; i++) s_pend[i - 1] = s_pend[i];
        s_pendN--;
    }
    memcpy(&s_pend[s_pendN].rec, &r, REC_SZ);
    s_pend[s_pendN].ver = ++s_verCtr;
    s_pendN++;
}

void queueToday() {
    LockGuard g(s_mu);
    queueSaveLocked(rtc.rec);
}

// ---- Merging a record loaded from the card after a power cut ---------------------------------------------
void mergeLoaded(const DayRecord &l) {
    DayRecord &r = rtc.rec;
    for (int h = 0; h < 24; h++) {
        // the live bins start after the reboot, the loaded ones end at the last save: they don't overlap
        r.hSolar[h] += l.hSolar[h];
        r.hHome[h] += l.hHome[h];
        r.hImp[h] += l.hImp[h];
        r.hExp[h] += l.hExp[h];
        if (r.soc[h] == SOC_NONE) r.soc[h] = l.soc[h];
    }
    if (l.solar > r.solar) r.solar = l.solar;
    if (l.home > r.home) r.home = l.home;
    if (l.imp > r.imp) r.imp = l.imp;
    if (l.exp > r.exp) r.exp = l.exp;
    if (l.battIn > r.battIn) r.battIn = l.battIn;
    if (l.battOut > r.battOut) r.battOut = l.battOut;
    r.flags |= DAY_PRESENT | (l.flags & DAY_PARTIAL);
}

void fillTotalsIfEmpty(DayRecord &r) {
    if (r.solar != 0 || r.home != 0 || r.imp != 0 || r.exp != 0) return;
    for (int h = 0; h < 24; h++) {
        r.solar += r.hSolar[h];
        r.home += r.hHome[h];
        r.imp += r.hImp[h];
        r.exp += r.hExp[h];
    }
}

// ---- SD side ---------------------------------------------------------------------------------------------
enum class LoadStatus : uint8_t { Ok, Missing, Error };

void monthPath(int ym, bool tmp, char *buf, size_t n) {
    snprintf(buf, n, "%s/%04d-%02d.bin%s", DIR, ym / 100, ym % 100, tmp ? ".tmp" : "");
}

// Keeps only slots that are whole, CRC-valid and in the right place; returns how many days are valid.
int validateMonth(uint8_t *buf, size_t usedLen, int ym) {
    int days = 0;
    DayRecord *recs = reinterpret_cast<DayRecord *>(buf);
    for (int i = 0; i < MAX_SLOTS; i++) {
        DayRecord &r = recs[i];
        bool ok = (static_cast<size_t>(i + 1) * REC_SZ <= usedLen) && recOk(r) && calc::monthOf(r.date) == ym &&
                  calc::slotOfDate(r.date) == i;
        if (ok) days++;
        else memset(&r, 0, REC_SZ);
    }
    return days;
}

// Reads a month file into s_work. Ok / Missing (no file) / Error (a file exists but could not be read).
LoadStatus readMonth(int ym, int &days) {
    char path[48], tmp[56];
    monthPath(ym, false, path, sizeof(path));
    monthPath(ym, true, tmp, sizeof(tmp));
    size_t len = 0;
    bool ok = sdcard::readFile(path, s_work, MONTH_BYTES, &len);
    if (!ok) ok = sdcard::readFile(tmp, s_work, MONTH_BYTES, &len) && len == MONTH_BYTES; // cut during replace
    if (!ok) {
        bool exists;
        {
            sdcard::Lock lk;
            exists = SD.exists(path) || SD.exists(tmp);
        }
        days = 0;
        return exists ? LoadStatus::Error : LoadStatus::Missing;
    }
    days = validateMonth(s_work, len, ym);
    return LoadStatus::Ok;
}

// Makes sure `ym` is in the cache (loading it from the card if needed). `keep` lists months not to evict.
LoadStatus ensureMonth(int ym, const int *keep, int nkeep) {
    {
        LockGuard g(s_mu);
        if (findMonth(ym)) return LoadStatus::Ok;
    }
    if (!s_work) s_work = static_cast<uint8_t *>(psramAlloc(MONTH_BYTES));
    if (!s_work) return LoadStatus::Error;
    uint32_t t0 = millis();
    int days = 0;
    LoadStatus st = readMonth(ym, days);
    if (st == LoadStatus::Error) {
        s_errors++;
        if (s_failYm == ym) s_failN++;
        else {
            s_failYm = ym;
            s_failN = 1;
        }
        if (s_failN < LOAD_FAIL_LIMIT) return LoadStatus::Error;
        Serial.printf("dayhist: %04d-%02d unreadable %d times, starting it empty\n", ym / 100, ym % 100, s_failN);
        st = LoadStatus::Missing;
        days = 0;
    }
    if (st == LoadStatus::Missing) memset(s_work, 0, MONTH_BYTES);
    s_failN = 0;
    {
        LockGuard g(s_mu);
        MonthEntry *victim = nullptr;
        for (MonthEntry &e : s_cache) {
            if (e.ym == 0) { // free entries first
                victim = &e;
                break;
            }
        }
        if (!victim) {
            for (MonthEntry &e : s_cache) {
                bool kept = false;
                for (int k = 0; k < nkeep; k++) kept = kept || keep[k] == e.ym;
                if (!kept && (!victim || e.tick < victim->tick)) victim = &e;
            }
        }
        if (!victim) victim = &s_cache[0];
        if (!victim->recs) victim->recs = static_cast<DayRecord *>(psramAlloc(MONTH_BYTES));
        if (!victim->recs) return LoadStatus::Error;
        victim->ym = ym;
        victim->tick = ++s_tick;
        memcpy(victim->recs, s_work, MONTH_BYTES);
        s_loads++;
    }
    if (st == LoadStatus::Ok)
        Serial.printf("dayhist: loaded %04d-%02d (%d days, %lu ms)\n", ym / 100, ym % 100, days,
                      static_cast<unsigned long>(millis() - t0));
    else Serial.printf("dayhist: %04d-%02d has no file yet\n", ym / 100, ym % 100);
    return st;
}

bool saveRecord(const DayRecord &rec) {
    if (!validDate(rec.date)) { // never derive a path from a record that isn't one (e.g. RTC before the clock)
        Serial.printf("dayhist: refusing to save a record with date %ld\n", static_cast<long>(rec.date));
        return false;
    }
    const int ym = calc::monthOf(rec.date);
    if (ensureMonth(ym, &ym, 1) == LoadStatus::Error) return false;
    uint32_t t0 = millis();
    MonthEntry *e;
    {
        LockGuard g(s_mu);
        e = findMonth(ym);
        if (!e) return false;
        memcpy(&e->recs[calc::slotOfDate(rec.date)], &rec, REC_SZ);
    }
    char path[48], tmp[56];
    monthPath(ym, false, path, sizeof(path));
    monthPath(ym, true, tmp, sizeof(tmp));
    bool ok = sdcard::mkdirs(DIR) && sdcard::writeFile(tmp, e->recs, MONTH_BYTES);
    if (ok) {
        sdcard::Lock lk;
        ok = (!SD.exists(path) || SD.remove(path)) && SD.rename(tmp, path);
    }
    if (!ok) {
        s_errors++;
        Serial.printf("dayhist: save of %ld to %s failed\n", static_cast<long>(rec.date), path);
        return false;
    }
    s_saves++;
    s_lastSaveMs = millis();
    s_haveSave = true;
    Serial.printf("dayhist: saved %04d-%02d-%02d to %s (%lu ms)\n", calc::ymdYear(rec.date),
                  calc::ymdMonth(rec.date), calc::ymdDay(rec.date), path,
                  static_cast<unsigned long>(millis() - t0));
    return true;
}

// One unit of work per tick keeps the SD task responsive. Each returns true if it used this tick.
bool serveRestore() {
    int32_t date;
    {
        LockGuard g(s_mu);
        if (s_restore != Restore::Wanted) return false;
        date = s_restoreDate;
    }
    const int ym = calc::monthOf(date);
    LoadStatus st = ensureMonth(ym, &ym, 1);
    if (st == LoadStatus::Error && ++s_restoreFails < 3) return true;
    LockGuard g(s_mu);
    const DayRecord *r = st == LoadStatus::Error ? nullptr : findRec(date);
    s_restoreHave = r != nullptr;
    if (r) memcpy(&s_restoreRec, r, REC_SZ);
    s_restore = Restore::Done;
    return true;
}

bool servePending() {
    if (static_cast<int32_t>(millis() - s_retryAt) < 0) return false;
    DayRecord rec;
    uint32_t ver;
    {
        LockGuard g(s_mu);
        if (!s_pendN) return false;
        memcpy(&rec, &s_pend[0].rec, REC_SZ);
        ver = s_pend[0].ver;
    }
    if (!saveRecord(rec)) {
        s_retryAt = millis() + SAVE_RETRY_MS;
        return true;
    }
    LockGuard g(s_mu);
    for (int i = 0; i < s_pendN; i++) {
        if (s_pend[i].rec.date == rec.date && s_pend[i].ver == ver) {
            for (int k = i + 1; k < s_pendN; k++) s_pend[k - 1] = s_pend[k];
            s_pendN--;
            break;
        }
    }
    if (s_haveLastDone && s_lastDone.date == rec.date) s_haveLastDone = false; // now in the cache
    if (s_reqValid) applyRequestLocked(); // the saved day may be on screen
    return true;
}

void serveRequest(bool mounted) {
    int need[2];
    int n;
    int missing = 0;
    {
        LockGuard g(s_mu);
        if (!s_awaiting || !s_reqValid) return;
        int32_t today;
        int hour;
        time_t now;
        if (!localNow(today, hour, now)) return;
        int32_t dates[MAX_SLOTS];
        const int span = static_cast<int>(s_reqMode);
        int cnt = calc::periodDates(span, today, s_reqOffset, dates);
        n = calc::monthsNeeded(span, dates, cnt, today, need);
        for (int k = 0; k < n && !missing; k++)
            if (!findMonth(need[k])) missing = need[k];
        if (!missing || !mounted) {
            buildBaseLocked(false, missing != 0); // all cached, or no card for what is missing
            return;
        }
    }
    if (ensureMonth(missing, need, n) == LoadStatus::Error) return; // retried next tick
    LockGuard g(s_mu);
    if (s_reqValid) applyRequestLocked();
}

void dropCache() {
    LockGuard g(s_mu);
    for (MonthEntry &e : s_cache) e.ym = 0; // buffers stay allocated
}

} // namespace

// ---- Public API ------------------------------------------------------------------------------------------

void dayHistoryUpdate(const PwData &d, const TodayStats &t) {
    int32_t today;
    int hour;
    time_t now;
    if (!localNow(today, hour, now)) return;

    if (!rtcValid()) {
        startDayRec(today, false);
        s_rtcState = "new";
        s_haveUpd = false;
        s_lastQueueHour = -1;
        LockGuard g(s_mu);
        s_restore = Restore::Wanted;
        s_restoreDate = today;
        s_restoreStartMs = millis();
        s_restoreFails = 0;
    } else if (rtc.rec.date != today) {
        if (rtc.rec.date < today && (rtc.rec.flags & DAY_PRESENT)) { // the day ended: save its last state
            fillTotalsIfEmpty(rtc.rec);
            rtcSeal();
            LockGuard g(s_mu);
            queueSaveLocked(rtc.rec);
            memcpy(&s_lastDone, &rtc.rec, REC_SZ);
            s_haveLastDone = true;
            Serial.printf("dayhist: day %ld finished, queued for saving\n", static_cast<long>(rtc.rec.date));
        }
        startDayRec(today, true);
        s_lastQueueHour = -1;
    }
    if (s_rtcState[0] == 'i') s_rtcState = "ok"; // survived a restart

    // A power cut: take what the card has of today, once it answers (or give up after 20 s without it)
    bool adopt = false;
    {
        LockGuard g(s_mu);
        if (s_restore == Restore::Done) {
            adopt = s_restoreHave && s_restoreRec.date == today && recOk(s_restoreRec);
            if (adopt) {
                mergeLoaded(s_restoreRec);
                s_rtcState = "restored";
            } else {
                s_rtcState = "empty";
            }
            s_restore = Restore::None;
        } else if (s_restore == Restore::Wanted && !s_mounted.load() &&
                   millis() - s_restoreStartMs > RESTORE_WAIT_MS) {
            s_restore = Restore::None;
            s_rtcState = "nocard";
            Serial.println("dayhist: no card, today's earlier bins are lost");
        }
    }

    // A fresh reading: the counter deltas go into the current hour's bins
    const bool fresh = !isStale(d.valid, d.updatedMs, PW_STALE_MS) && (!s_haveUpd || d.updatedMs != s_lastUpd) &&
                       d.solarExportedWh > 0 && d.loadImportedWh > 0;
    if (fresh) {
        s_haveUpd = true;
        s_lastUpd = d.updatedMs;
        const double cur[calc::CHANNELS] = {d.solarExportedWh, d.loadImportedWh, d.siteImportedWh,
                                            d.siteExportedWh};
        float *const bins[calc::CHANNELS] = {rtc.rec.hSolar, rtc.rec.hHome, rtc.rec.hImp, rtc.rec.hExp};
        calc::foldReading(rtc.prev, now, hour, cur, bins);
        rtc.rec.flags |= DAY_PRESENT;
        if (d.socValid) {
            float s = d.soc < 0 ? 0 : d.soc > 100 ? 100 : d.soc;
            rtc.rec.soc[hour] = static_cast<uint8_t>(lroundf(s));
        }
    }
    if (rtc.rec.flags & DAY_PRESENT) {
        DayRecord &r = rtc.rec;
        if (t.valid) {
            r.solar = static_cast<float>(t.solarKWh);
            r.home = static_cast<float>(t.homeKWh);
            r.exp = static_cast<float>(t.exportKWh);
            r.imp = static_cast<float>(t.importKWh);
            r.battIn = static_cast<float>(t.battInKWh);
            r.battOut = static_cast<float>(t.battOutKWh);
            r.flags = static_cast<uint8_t>(t.partial ? (r.flags | DAY_PARTIAL) : (r.flags & ~DAY_PARTIAL));
            // energy.cpp bins solar per hour since its baseline: fill finished hours this record missed
            // (the first day after flashing, or bins lost in a power cut without a card)
            for (int h = 0; h < hour && h < 24; h++)
                if (r.hSolar[h] <= 0.0f && t.hourlySolarKWh[h] > 0.0f) r.hSolar[h] = t.hourlySolarKWh[h];
        } else {
            fillTotalsIfEmpty(r);
        }
    }
    rtcSeal();

    // Save when the hour changes and every 10 minutes
    if (rtc.rec.flags & DAY_PRESENT) {
        if (s_lastQueueHour < 0) {
            s_lastQueueHour = hour;
            s_lastQueueMs = millis();
        } else if (hour != s_lastQueueHour || millis() - s_lastQueueMs >= SAVE_EVERY_MS) {
            queueToday();
            s_lastQueueHour = hour;
            s_lastQueueMs = millis();
        }
    }
}

void dayHistoryRequest(Mode mode, int offset) {
    offset = calc::clampOffset(static_cast<int>(mode), offset);
    LockGuard g(s_mu);
    if (s_reqValid && s_reqMode == mode && s_reqOffset == offset) return;
    s_reqValid = true;
    s_reqMode = mode;
    s_reqOffset = offset;
    applyRequestLocked();
}

void dayHistoryView(View &out) {
    DayRecord done;
    bool haveDone;
    int32_t today = 0;
    int hour = 0;
    time_t now;
    const bool clock = localNow(today, hour, now);
    {
        LockGuard g(s_mu);
        if (!s_reqValid) {
            s_reqValid = true;
            s_reqMode = Mode::Day;
            s_reqOffset = 0;
            applyRequestLocked();
        } else if ((clock ? today : 0) != s_baseToday) {
            applyRequestLocked(); // midnight passed, or the clock just became valid
        }
        out = s_base;
        haveDone = s_haveLastDone;
        if (haveDone) memcpy(&done, &s_lastDone, REC_SZ);
    }
    const DayRecord *todayRec =
        (clock && rtcValid() && rtc.rec.date == today && (rtc.rec.flags & DAY_PRESENT)) ? &rtc.rec : nullptr;
    finalizeView(out, todayRec, haveDone ? &done : nullptr, clock ? today : 0, hour);
    uint32_t sig = viewSignature(out);
    if (!s_haveSig || sig != s_lastSig) {
        s_viewSeq++;
        s_lastSig = sig;
        s_haveSig = true;
    }
    out.seq = s_viewSeq;
}

// DIAG panel drift (serial "y s <n>"): save a copy of today's record n times, one per SD tick, logging the
// ST7701 CS pin (GPIO39, shared SCK/SDA with the card) around each save.
namespace {
DayRecord s_forceRec;
std::atomic_int s_forceN{0};

void logPanelCs(const char *when) {
    const uint32_t en = REG_READ(GPIO_ENABLE1_REG), out = REG_READ(GPIO_OUT1_REG);
    const int bit = B4848_LCD_CS - 32;
    Serial.printf("dayhist: diag %s: lcd cs gpio%d output-enable %d level %d (in %d)\n", when, B4848_LCD_CS,
                  (int)((en >> bit) & 1), (int)((out >> bit) & 1), gpio_get_level(static_cast<gpio_num_t>(B4848_LCD_CS)));
}

// The junk file the first diag run wrote (fired before the clock was valid, before saveRecord checked dates)
void removeDiagJunk() {
    static bool done = false;
    if (done) return;
    done = true;
    sdcard::Lock lk;
    if (SD.exists("/powerwall/days/-73918--60.bin") && SD.remove("/powerwall/days/-73918--60.bin"))
        Serial.println("dayhist: removed the junk file from the first diag run");
}

bool serveForce() {
    removeDiagJunk();
    if (s_forceN.load() <= 0) return false;
    DayRecord r;
    {
        LockGuard g(s_mu);
        memcpy(&r, &s_forceRec, REC_SZ);
    }
    const int left = --s_forceN;
    logPanelCs("before save");
    const bool ok = saveRecord(r);
    logPanelCs(ok ? "after save" : "after FAILED save");
    if (left == 0) Serial.println("dayhist: diag forced saves done");
    return true;
}
} // namespace

void dayHistoryForceSaves(int n) {
    {
        LockGuard g(s_mu);
        memcpy(&s_forceRec, &rtc.rec, REC_SZ);
    }
    s_forceN.store(n);
}

void dayHistorySdTick(bool mounted) {
    s_mounted.store(mounted);
    if (mounted && serveForce()) return;
    if (mounted != s_prevMounted) {
        s_prevMounted = mounted;
        if (mounted) { // new or swapped card: forget what the old one held
            dropCache();
            s_retryAt = millis();
            LockGuard g(s_mu);
            if (s_reqValid) applyRequestLocked();
        }
    }
    if (mounted && (serveRestore() || servePending())) return;
    serveRequest(mounted);
}

void dayHistoryStatusLine(char *buf, size_t n) {
    int bins = 0;
    char today[12] = "-";
    const char *state = "bad";
    if (rtcValid()) {
        const DayRecord &r = rtc.rec;
        snprintf(today, sizeof(today), "%04d-%02d-%02d", calc::ymdYear(r.date), calc::ymdMonth(r.date),
                 calc::ymdDay(r.date));
        for (int h = 0; h < 24; h++)
            if (r.soc[h] != SOC_NONE || r.hSolar[h] > 0 || r.hHome[h] > 0 || r.hImp[h] > 0 || r.hExp[h] > 0) bins++;
        state = s_rtcState;
    }
    char cache[40] = "";
    char ago[16] = "never";
    unsigned long saves, loads, errors;
    int pend;
    {
        LockGuard g(s_mu);
        for (const MonthEntry &e : s_cache) {
            if (!e.recs || !e.ym) continue;
            size_t l = strlen(cache);
            snprintf(cache + l, sizeof(cache) - l, "%s%04d-%02d", l ? "," : "", e.ym / 100, e.ym % 100);
        }
        saves = s_saves;
        loads = s_loads;
        errors = s_errors;
        pend = s_pendN;
        if (s_haveSave) snprintf(ago, sizeof(ago), "%lu s ago", static_cast<unsigned long>((millis() - s_lastSaveMs) / 1000));
    }
    snprintf(buf, n, "dayhist: today %s bins %d rtc %s | saves %lu (last %s) pending %d loads %lu errors %lu | cache %s",
             today, bins, state, saves, ago, pend, loads, errors, cache[0] ? cache : "none");
}

} // namespace dayhist
