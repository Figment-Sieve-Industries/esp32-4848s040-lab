#pragma once
// Pure helpers for day_history.cpp (no Arduino/ESP headers, so hosttest/test.cpp can include it):
// civil-date arithmetic on yyyymmdd integers, week/month periods and their ASCII labels, CRC-32,
// and the rules that turn lifetime-counter readings into hourly bins.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace dayhist {
namespace calc {

// ---- CRC-32 (IEEE, reflected, same result as zlib crc32 / esp_rom_crc32_le(0, ...)) -----------------
inline uint32_t crc32(const void *data, size_t len, uint32_t crc = 0) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// ---- Civil dates as yyyymmdd ints ------------------------------------------------------------------
inline int32_t ymdPack(int y, int m, int d) { return y * 10000 + m * 100 + d; }
inline int ymdYear(int32_t ymd) { return ymd / 10000; }
inline int ymdMonth(int32_t ymd) { return ymd / 100 % 100; }
inline int ymdDay(int32_t ymd) { return ymd % 100; }

inline bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int daysInMonth(int y, int m) {
    static const int8_t N[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    return (m == 2 && isLeap(y)) ? 29 : N[m - 1];
}

// Days since 1970-01-01 (proleptic Gregorian, Hinnant's algorithm)
inline int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = static_cast<int>(y - era * 400);
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
inline int32_t civilFromDays(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = static_cast<int>(z - era * 146097);
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y = static_cast<int>(yoe + era * 400);
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int d = doy - (153 * mp + 2) / 5 + 1;
    const int m = mp + (mp < 10 ? 3 : -9);
    return ymdPack(y + (m <= 2), m, d);
}
inline int32_t addDays(int32_t ymd, int n) {
    return civilFromDays(daysFromCivil(ymdYear(ymd), ymdMonth(ymd), ymdDay(ymd)) + n);
}
// 0 = Sunday .. 6 = Saturday
inline int weekday(int32_t ymd) {
    int64_t z = daysFromCivil(ymdYear(ymd), ymdMonth(ymd), ymdDay(ymd));
    int w = static_cast<int>((z + 4) % 7); // 1970-01-01 was a Thursday
    return w < 0 ? w + 7 : w;
}
inline int32_t mondayOf(int32_t ymd) { return addDays(ymd, -((weekday(ymd) + 6) % 7)); }

// yyyymm arithmetic
inline int addMonths(int ym, int n) {
    int idx = (ym / 100) * 12 + (ym % 100 - 1) + n;
    return (idx / 12) * 100 + idx % 12 + 1;
}
inline int monthOf(int32_t ymd) { return ymd / 100; } // yyyymm
inline int slotOfDate(int32_t ymd) { return ymdDay(ymd) - 1; } // slot in the month file

// ---- Periods -----------------------------------------------------------------------------------------
enum Span : uint8_t { SpanDay = 0, SpanWeek = 1, SpanMonth = 2 };
constexpr int MAX_DAYS = 31;

// Offsets are never positive and limited to a sane history depth.
inline int clampOffset(int span, int offset) {
    const int lim = span == SpanDay ? -400 : span == SpanWeek ? -60 : -24;
    if (offset > 0) return 0;
    return offset < lim ? lim : offset;
}

// Slot dates of a period. Day: 1 date (24 hourly slots, dates[0] = the day). Week: Monday..Sunday
// containing today + 7 * offset. Month: the calendar month today is in, shifted by offset months.
// Returns the slot count (Day 24, Week 7, Month 28..31); dates[] has up to 31 entries (Day: 1).
inline int periodDates(int span, int32_t today, int offset, int32_t *dates) {
    offset = clampOffset(span, offset);
    if (span == SpanDay) {
        dates[0] = addDays(today, offset);
        return 24;
    }
    if (span == SpanWeek) {
        int32_t mon = mondayOf(addDays(today, 7 * offset));
        for (int i = 0; i < 7; i++) dates[i] = addDays(mon, i);
        return 7;
    }
    int ym = addMonths(monthOf(today), offset);
    int n = daysInMonth(ym / 100, ym % 100);
    for (int i = 0; i < n; i++) dates[i] = ymdPack(ym / 100, ym % 100, i + 1);
    return n;
}

// The distinct months (yyyymm) holding the period's days that are before today (those are read from the
// card; today comes from memory). out has room for 2 (a Day / Week touches at most 2, a Month 1).
inline int monthsNeeded(int span, const int32_t *dates, int count, int32_t today, int *out) {
    int n = 0;
    int slots = span == SpanDay ? 1 : count;
    for (int i = 0; i < slots; i++) {
        if (dates[i] >= today) continue;
        int ym = monthOf(dates[i]);
        bool seen = false;
        for (int k = 0; k < n; k++) seen = seen || out[k] == ym;
        if (!seen && n < 2) out[n++] = ym;
    }
    return n;
}

inline const char *wdayAbbr(int w) {
    static const char *const N[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return N[(w % 7 + 7) % 7];
}
inline const char *monthAbbr(int m) {
    static const char *const N[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return N[((m - 1) % 12 + 12) % 12];
}
inline const char *monthName(int m) {
    static const char *const N[12] = {"January", "February", "March",     "April",   "May",      "June",
                                      "July",    "August",   "September", "October", "November", "December"};
    return N[((m - 1) % 12 + 12) % 12];
}

// ASCII only (the device font has no middle dot / en dash). dates/count as from periodDates().
inline void periodLabel(int span, int offset, const int32_t *dates, int count, char *buf, size_t n) {
    if (!n) return;
    offset = clampOffset(span, offset);
    if (span == SpanDay) {
        if (offset == 0) snprintf(buf, n, "Today");
        else if (offset == -1) snprintf(buf, n, "Yesterday");
        else snprintf(buf, n, "%s %d %s", wdayAbbr(weekday(dates[0])), ymdDay(dates[0]), monthAbbr(ymdMonth(dates[0])));
    } else if (span == SpanWeek) {
        if (offset == 0) {
            snprintf(buf, n, "This week");
        } else {
            int32_t a = dates[0], b = dates[count - 1];
            if (ymdMonth(a) == ymdMonth(b))
                snprintf(buf, n, "%d-%d %s", ymdDay(a), ymdDay(b), monthAbbr(ymdMonth(b)));
            else
                snprintf(buf, n, "%d %s-%d %s", ymdDay(a), monthAbbr(ymdMonth(a)), ymdDay(b), monthAbbr(ymdMonth(b)));
        }
    } else {
        snprintf(buf, n, "%s %d", monthName(ymdMonth(dates[0])), ymdYear(dates[0]));
    }
}

// ---- Hourly binning ----------------------------------------------------------------------------------
// Channels: 0 solar, 1 home (load), 2 import (site imported), 3 export (site exported); counters in Wh.
constexpr int CHANNELS = 4;
constexpr int64_t MAX_GAP_S = 30 * 60;  // longer gaps are not placed into bins (day totals still count them)
constexpr double MAX_DELTA_WH = 50000;  // more than this between two readings = counter reset, ignore

struct Prev {
    bool have;
    int64_t t;      // time of the previous reading
    double c[CHANNELS];
};

// Folds one fresh reading. The delta since the previous reading goes into bins[ch][hour] (kWh) when
// the gap is <= 30 min and 0 <= delta <= MAX_DELTA_WH (per channel). Always remembers this reading.
// `hour` is the local tm_hour of `now` (DST: a repeated hour adds into the same bin, a skipped hour is
// never visited). Returns true if any energy was added.
inline bool foldReading(Prev &p, int64_t now, int hour, const double cur[CHANNELS], float *const bins[CHANNELS]) {
    bool added = false;
    if (p.have && now >= p.t && now - p.t <= MAX_GAP_S && hour >= 0 && hour < 24) {
        for (int i = 0; i < CHANNELS; i++) {
            double d = cur[i] - p.c[i];
            if (d >= 0 && d <= MAX_DELTA_WH) {
                bins[i][hour] += static_cast<float>(d / 1000.0);
                added = added || d > 0;
            }
        }
    }
    p.have = true;
    p.t = now;
    for (int i = 0; i < CHANNELS; i++) p.c[i] = cur[i];
    return added;
}

} // namespace calc
} // namespace dayhist
