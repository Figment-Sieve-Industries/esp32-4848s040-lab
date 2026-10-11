// Date/time helpers - see time_util.h. Plain C/C++ (no Arduino types): the parsers are host-tested
// (hosttest/).
#include "time_util.h"
#include <stdio.h>
#include <string.h>

namespace tu {

const char *const MONTH_ABBR[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *const MONTH_NAME[12] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};
const char *const WDAY_ABBR[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

namespace {

inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
inline bool digit(char c) { return c >= '0' && c <= '9'; }

// Exactly n decimal digits at *p (advanced past them); false otherwise.
bool digits(const char *&p, int n, int &out) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!digit(p[i])) return false;
        v = v * 10 + (p[i] - '0');
    }
    p += n;
    out = v;
    return true;
}

// The character c at *p (advanced past it); false otherwise.
bool lit(const char *&p, char c) {
    if (*p != c) return false;
    ++p;
    return true;
}

time_t compose(int y, int mo, int d, int h, int mi, int s) {
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 24 || mi < 0 || mi > 59 || s < 0 || s > 60) return 0;
    return (time_t)(daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s);
}

} // namespace

bool clockValid(time_t t) { return t >= MIN_VALID_UTC; }

// H. Hinnant's days_from_civil
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// "2026-10-10T17:00:00+0000" (AuroraWatch), "2026-10-11T08:23+01:00" (MET Norway), "2026-10-11T10:38:00Z"
// (SWPC), "2026-10-04T00:00:00" (SWPC Kp, UTC). A space is accepted in place of the 'T'.
time_t parseIso8601(const char *s) {
    if (!s) return 0;
    const char *p = s;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (!digits(p, 4, y) || !lit(p, '-') || !digits(p, 2, mo) || !lit(p, '-') || !digits(p, 2, d)) return 0;
    if (!lit(p, 'T') && !lit(p, 't') && !lit(p, ' ')) return 0;
    if (!digits(p, 2, h) || !lit(p, ':') || !digits(p, 2, mi)) return 0;
    if (*p == ':') {
        ++p;
        if (!digits(p, 2, sec)) return 0;
        if (*p == '.' || *p == ',') { // fractional seconds: ignored
            ++p;
            while (digit(*p)) ++p;
        }
    }
    int off = 0; // seconds east of UTC
    if (*p == '+' || *p == '-') {
        const int sign = *p == '-' ? -1 : 1;
        ++p;
        int oh = 0, om = 0;
        if (!digits(p, 2, oh)) return 0;
        if (*p == ':') ++p;
        if (digit(*p) && !digits(p, 2, om)) return 0;
        if (oh > 23 || om > 59) return 0;
        off = sign * (oh * 3600 + om * 60);
    } // 'Z', nothing or anything else: UTC
    const time_t t = compose(y, mo, d, h, mi, sec);
    return t ? t - off : 0;
}

// "Sun, 11 Oct 2026 10:56:05 GMT" (the weekday is optional)
time_t parseHttpDate(const char *s) {
    if (!s || !*s) return 0;
    const char *comma = strchr(s, ',');
    const char *p = comma ? comma + 1 : s;
    int d = 0, y = 0, h = 0, mi = 0, sec = 0;
    char mon[4] = {};
    if (sscanf(p, " %2d %3s %4d %2d:%2d:%2d", &d, mon, &y, &h, &mi, &sec) != 6) return 0;
    const int m = monthFromName(mon, strlen(mon));
    return m < 0 ? 0 : compose(y, m + 1, d, h, mi, sec);
}

int monthFromName(const char *w, size_t n) {
    if (!w || n < 3) return -1;
    for (int m = 0; m < 12; m++) {
        const char *name = MONTH_NAME[m];
        if (n != 3 && n != strlen(name)) continue;
        size_t i = 0;
        while (i < n && lower(w[i]) == lower(name[i])) ++i;
        if (i == n) return m;
    }
    return -1;
}

int utcOffsetMinutes(time_t t) {
    struct tm lt, ut;
    localtime_r(&t, &lt);
    gmtime_r(&t, &ut);
    const int64_t local = daysFromCivil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1, (unsigned)lt.tm_mday) * 1440 +
                          lt.tm_hour * 60 + lt.tm_min;
    const int64_t utc = daysFromCivil(ut.tm_year + 1900, (unsigned)ut.tm_mon + 1, (unsigned)ut.tm_mday) * 1440 +
                        ut.tm_hour * 60 + ut.tm_min;
    return (int)(local - utc);
}

void fmtHHMM(char *buf, size_t n, time_t t) {
    if (!buf || !n) return;
    if (t <= 0) {
        snprintf(buf, n, "--");
        return;
    }
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(buf, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

} // namespace tu
