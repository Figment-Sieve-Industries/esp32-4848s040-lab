#pragma once
// Date/time helpers shared by the fetchers and screens (implemented in time_util.cpp). Pure C++ apart
// from fmtHHMM/utcOffsetMinutes (localtime_r), so the parsers are host-testable.
#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace tu {

constexpr time_t MIN_VALID_UTC = 1704067200; // 2024-01-01: the clock is NTP-synced once past this
bool clockValid(time_t t);
inline bool clockValid() { return clockValid(time(nullptr)); }

int64_t daysFromCivil(int y, unsigned m, unsigned d); // days since 1970-01-01 (proleptic Gregorian)
// "YYYY-MM-DDTHH:MM[:SS][.fff][Z|+hh:mm|+hhmm|-..]"; no offset = UTC. Returns 0 on error.
time_t parseIso8601(const char *s);
// RFC 1123 "Sun, 11 Oct 2026 10:56:05 GMT". Returns 0 on error.
time_t parseHttpDate(const char *s);
// "Sep" / "September" (any case, first n chars of w) -> 0..11, else -1.
int monthFromName(const char *w, size_t n);

extern const char *const MONTH_ABBR[12]; // "Jan"..
extern const char *const MONTH_NAME[12]; // "January"..
extern const char *const WDAY_ABBR[7];   // "Sun"..

int utcOffsetMinutes(time_t t);              // local time minus UTC at t (TZ from configTzTime)
void fmtHHMM(char *buf, size_t n, time_t t); // local "07:05"; "--" if t <= 0

} // namespace tu
