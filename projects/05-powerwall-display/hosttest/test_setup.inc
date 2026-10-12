// Host tests for the setup portal's pure helpers (setup_tz.h, setup_util.h). Included by test.cpp after its
// CHECK macros and time_util.cpp. The POSIX TZ evaluator below is test-only: it checks the table's strings give the
// expected UTC offsets around real DST transitions (newlib's tzset does the same work on the device).
#include "../src/setup_tz.h"
#include "../src/setup_util.h"

namespace tztest {

struct Rule {
    int m = 0, w = 0, d = 0;
    long t = 7200; // seconds after local midnight
};
struct Tz {
    long stdOff = 0, dstOff = 0; // seconds to ADD to local time to get UTC (POSIX sign)
    bool dst = false;
    Rule start, end;
};

static bool name(const char *&p) {
    if (*p == '<') {
        const char *e = strchr(p, '>');
        if (!e) return false;
        p = e + 1;
        return true;
    }
    const char *s = p;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
    return p - s >= 3;
}
static bool num(const char *&p, long &v) {
    if (*p < '0' || *p > '9') return false;
    v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return true;
}
// [+-]h[:mm[:ss]] -> seconds
static bool offset(const char *&p, long &sec) {
    int sign = 1;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    long h, m = 0, s = 0;
    if (!num(p, h)) return false;
    if (*p == ':') {
        p++;
        if (!num(p, m)) return false;
        if (*p == ':') {
            p++;
            if (!num(p, s)) return false;
        }
    }
    sec = sign * (h * 3600 + m * 60 + s);
    return true;
}
static bool rule(const char *&p, Rule &r) {
    if (*p++ != 'M') return false;
    long m, w, d;
    if (!num(p, m) || *p++ != '.' || !num(p, w) || *p++ != '.' || !num(p, d)) return false;
    if (m < 1 || m > 12 || w < 1 || w > 5 || d > 6) return false;
    r.m = (int)m, r.w = (int)w, r.d = (int)d;
    if (*p == '/') {
        p++;
        if (!offset(p, r.t)) return false;
    }
    return true;
}
static bool parse(const char *s, Tz &tz) {
    const char *p = s;
    if (!name(p) || !offset(p, tz.stdOff)) return false;
    if (!*p) return true;
    if (!name(p)) return false;
    tz.dst = true;
    tz.dstOff = tz.stdOff - 3600;
    if (*p != ',' && !offset(p, tz.dstOff)) return false;
    if (*p++ != ',' || !rule(p, tz.start) || *p++ != ',' || !rule(p, tz.end)) return false;
    return *p == '\0';
}
// Local midnight (as seconds since the epoch, "local clock") of the rule's day in year y
static long long ruleDay(int y, const Rule &r) {
    const long long first = tu::daysFromCivil(y, (unsigned)r.m, 1);
    const int wdFirst = (int)(((first % 7) + 7 + 4) % 7); // 1970-01-01 was a Thursday (4)
    long long day = first + ((r.d - wdFirst + 7) % 7) + 7LL * (r.w - 1);
    const unsigned nextM = r.m == 12 ? 1u : (unsigned)r.m + 1;
    const long long monthEnd = tu::daysFromCivil(r.m == 12 ? y + 1 : y, nextM, 1);
    while (day >= monthEnd) day -= 7; // week 5 = last
    return day * 86400;
}
static int yearOf(long long t) {
    for (int y = 1970; y < 2100; y++)
        if (tu::daysFromCivil(y + 1, 1, 1) * 86400 > t) return y;
    return 2100;
}
// UTC offset in seconds (local = utc + result)
static long utcOffset(const Tz &tz, long long utc) {
    if (!tz.dst) return -tz.stdOff;
    const int y = yearOf(utc - tz.stdOff);
    const long long start = ruleDay(y, tz.start) + tz.start.t + tz.stdOff; // switch to DST, given in standard time
    const long long end = ruleDay(y, tz.end) + tz.end.t + tz.dstOff;       // back, given in DST
    const bool inDst = start < end ? (utc >= start && utc < end) : (utc >= start || utc < end);
    return -(inDst ? tz.dstOff : tz.stdOff);
}
static long long utcAt(int y, unsigned mo, unsigned d, int h, int mi) {
    return tu::daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60;
}
static long offsetFor(const char *iana, long long utc) {
    Tz tz;
    const char *posix = setuptz::posixFor(iana);
    if (!posix || !parse(posix, tz)) return 99999;
    return utcOffset(tz, utc);
}

} // namespace tztest

static void testSetupTz() {
    using namespace tztest;
    // Table: sorted, unique, every string parses; aliases point at entries
    for (size_t i = 0; i < setuptz::ZONE_COUNT; i++) {
        Tz tz;
        const bool ok = parse(setuptz::ZONES[i].posix, tz);
        if (!ok) printf("  bad POSIX TZ for %s: %s\n", setuptz::ZONES[i].iana, setuptz::ZONES[i].posix);
        CHECK(ok);
        if (i) CHECK(strcmp(setuptz::ZONES[i - 1].iana, setuptz::ZONES[i].iana) < 0);
    }
    for (const setuptz::Zone &a : setuptz::ALIASES) CHECK(setuptz::posixFor(a.iana) != nullptr);
    CHECK_EQ_STR(setuptz::posixFor("Europe/London"), "GMT0BST,M3.5.0/1,M10.5.0");
    CHECK_EQ_STR(setuptz::posixFor("Asia/Calcutta"), setuptz::posixFor("Asia/Kolkata"));
    CHECK(setuptz::posixFor("Mars/Olympus_Mons") == nullptr);
    CHECK(setuptz::posixFor("") == nullptr);
    CHECK(setuptz::posixFor(nullptr) == nullptr);
    // Every zone the portal must know
    static const char *const MUST[] = {
        "Europe/London", "Europe/Dublin", "Europe/Lisbon", "Europe/Paris", "Europe/Berlin", "Europe/Madrid",
        "Europe/Rome", "Europe/Amsterdam", "Europe/Brussels", "Europe/Stockholm", "Europe/Oslo", "Europe/Copenhagen",
        "Europe/Helsinki", "Europe/Athens", "Europe/Warsaw", "Europe/Prague", "Europe/Zurich", "Europe/Vienna",
        "America/New_York", "America/Chicago", "America/Denver", "America/Phoenix", "America/Los_Angeles",
        "America/Anchorage", "Pacific/Honolulu", "America/Toronto", "America/Vancouver", "America/Halifax",
        "America/St_Johns", "Australia/Sydney", "Australia/Melbourne", "Australia/Brisbane", "Australia/Adelaide",
        "Australia/Perth", "Australia/Darwin", "Australia/Hobart", "Pacific/Auckland", "Asia/Tokyo", "Asia/Singapore",
        "Asia/Hong_Kong", "Asia/Shanghai", "Asia/Kolkata", "Asia/Dubai", "Africa/Johannesburg", "America/Sao_Paulo",
        "America/Mexico_City"};
    for (const char *z : MUST) {
        if (!setuptz::posixFor(z)) printf("  missing zone %s\n", z);
        CHECK(setuptz::posixFor(z) != nullptr);
    }

    const long H = 3600;
    // London: BST from 01:00 UTC on the last Sunday of March to 01:00 UTC on the last Sunday of October
    CHECK(offsetFor("Europe/London", utcAt(2026, 1, 15, 12, 0)) == 0);
    CHECK(offsetFor("Europe/London", utcAt(2026, 7, 1, 12, 0)) == H);
    CHECK(offsetFor("Europe/London", utcAt(2026, 3, 29, 0, 59)) == 0);
    CHECK(offsetFor("Europe/London", utcAt(2026, 3, 29, 1, 0)) == H);
    CHECK(offsetFor("Europe/London", utcAt(2026, 10, 25, 0, 59)) == H);
    CHECK(offsetFor("Europe/London", utcAt(2026, 10, 25, 1, 0)) == 0);
    CHECK(offsetFor("Europe/Dublin", utcAt(2026, 7, 1, 12, 0)) == H);
    CHECK(offsetFor("Europe/Dublin", utcAt(2026, 12, 1, 12, 0)) == 0);
    // Central / Eastern Europe switch at the same instant (01:00 UTC)
    CHECK(offsetFor("Europe/Paris", utcAt(2026, 3, 29, 0, 59)) == H);
    CHECK(offsetFor("Europe/Paris", utcAt(2026, 3, 29, 1, 0)) == 2 * H);
    CHECK(offsetFor("Europe/Athens", utcAt(2026, 3, 29, 0, 59)) == 2 * H);
    CHECK(offsetFor("Europe/Athens", utcAt(2026, 3, 29, 1, 0)) == 3 * H);
    CHECK(offsetFor("Europe/Athens", utcAt(2026, 10, 25, 1, 0)) == 2 * H);
    // US: second Sunday of March 02:00 local, first Sunday of November 02:00 local
    CHECK(offsetFor("America/New_York", utcAt(2026, 3, 8, 6, 59)) == -5 * H);
    CHECK(offsetFor("America/New_York", utcAt(2026, 3, 8, 7, 0)) == -4 * H);
    CHECK(offsetFor("America/New_York", utcAt(2026, 11, 1, 5, 59)) == -4 * H);
    CHECK(offsetFor("America/New_York", utcAt(2026, 11, 1, 6, 0)) == -5 * H);
    CHECK(offsetFor("America/Los_Angeles", utcAt(2026, 7, 1, 12, 0)) == -7 * H);
    CHECK(offsetFor("America/Phoenix", utcAt(2026, 7, 1, 12, 0)) == -7 * H);
    CHECK(offsetFor("America/St_Johns", utcAt(2026, 1, 15, 12, 0)) == -(3 * H + 1800));
    CHECK(offsetFor("America/St_Johns", utcAt(2026, 7, 1, 12, 0)) == -(2 * H + 1800));
    CHECK(offsetFor("America/Mexico_City", utcAt(2026, 7, 1, 12, 0)) == -6 * H);
    CHECK(offsetFor("America/Sao_Paulo", utcAt(2026, 1, 15, 12, 0)) == -3 * H);
    // Southern hemisphere: Sydney DST ends 03:00 local on the first Sunday of April (2026-04-05 = 04-04 16:00 UTC)
    CHECK(offsetFor("Australia/Sydney", utcAt(2026, 1, 15, 12, 0)) == 11 * H);
    CHECK(offsetFor("Australia/Sydney", utcAt(2026, 7, 1, 12, 0)) == 10 * H);
    CHECK(offsetFor("Australia/Sydney", utcAt(2026, 4, 4, 15, 59)) == 11 * H);
    CHECK(offsetFor("Australia/Sydney", utcAt(2026, 4, 4, 16, 0)) == 10 * H);
    // ... and starts 02:00 local on the first Sunday of October (2026-10-04 = 10-03 16:00 UTC)
    CHECK(offsetFor("Australia/Sydney", utcAt(2026, 10, 3, 15, 59)) == 10 * H);
    CHECK(offsetFor("Australia/Sydney", utcAt(2026, 10, 3, 16, 0)) == 11 * H);
    CHECK(offsetFor("Australia/Brisbane", utcAt(2026, 1, 15, 12, 0)) == 10 * H);
    CHECK(offsetFor("Australia/Adelaide", utcAt(2026, 1, 15, 12, 0)) == 10 * H + 1800);
    CHECK(offsetFor("Australia/Darwin", utcAt(2026, 1, 15, 12, 0)) == 9 * H + 1800);
    CHECK(offsetFor("Australia/Perth", utcAt(2026, 1, 15, 12, 0)) == 8 * H);
    CHECK(offsetFor("Pacific/Auckland", utcAt(2026, 1, 15, 12, 0)) == 13 * H);
    CHECK(offsetFor("Pacific/Auckland", utcAt(2026, 7, 1, 12, 0)) == 12 * H);
    CHECK(offsetFor("Asia/Kolkata", utcAt(2026, 7, 1, 12, 0)) == 5 * H + 1800);
    CHECK(offsetFor("Asia/Kathmandu", utcAt(2026, 7, 1, 12, 0)) == 5 * H + 2700);
    CHECK(offsetFor("Asia/Tokyo", utcAt(2026, 7, 1, 12, 0)) == 9 * H);
    CHECK(offsetFor("Africa/Johannesburg", utcAt(2026, 7, 1, 12, 0)) == 2 * H);
}

static void testSetupUtil() {
    char buf[64];
    const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0x01, 0x4F, 0x2A};
    char ap[16];
    setuputil::apSsid(mac, ap);
    CHECK_EQ_STR(ap, "PW-Display-4F2A");

    CHECK(setuputil::wifiQrOpen("PW-Display-4F2A", buf, sizeof(buf)) == strlen("WIFI:T:nopass;S:PW-Display-4F2A;;"));
    CHECK_EQ_STR(buf, "WIFI:T:nopass;S:PW-Display-4F2A;;");
    CHECK(setuputil::wifiQrOpen("a;b,c:d\\e\"f", buf, sizeof(buf)) > 0);
    CHECK_EQ_STR(buf, "WIFI:T:nopass;S:a\\;b\\,c\\:d\\\\e\\\"f;;");
    char tiny[20];
    CHECK(setuputil::wifiQrOpen("PW-Display-4F2A", tiny, sizeof(tiny)) == 0);
    CHECK(setuputil::wifiQrOpen("", buf, sizeof(buf)) > 0); // degenerate but well-formed

    CHECK(setuputil::urlEncode("Saint-Malo \xC3\xA9", buf, sizeof(buf)) > 0);
    CHECK_EQ_STR(buf, "Saint-Malo%20%C3%A9");
    CHECK(setuputil::urlEncode("a&b=c/d?", buf, sizeof(buf)) > 0);
    CHECK_EQ_STR(buf, "a%26b%3Dc%2Fd%3F");
    CHECK(setuputil::urlEncode("\xFF\xFF\xFF", tiny, 9) == 0);

    CHECK(setuputil::validHost("192.168.1.50"));
    CHECK(setuputil::validHost("powerwall.lan"));
    CHECK(!setuputil::validHost(""));
    CHECK(!setuputil::validHost(nullptr));
    CHECK(!setuputil::validHost("1.2.3.4:443"));
    CHECK(!setuputil::validHost("1.2.3.4/api"));
    CHECK(!setuputil::validHost("a..b"));
    CHECK(!setuputil::validHost("-gw"));
    CHECK(!setuputil::validHost("gw."));
    CHECK(!setuputil::validHost("gw x"));

    CHECK(setuputil::printableAscii("GMT0BST,M3.5.0/1,M10.5.0", 63));
    CHECK(!setuputil::printableAscii("", 63));
    CHECK(!setuputil::printableAscii("a\nb", 63));
    CHECK(!setuputil::printableAscii("abcd", 3));

    setuputil::asciiFold("Z\xC3\xBCrich", buf, sizeof(buf));
    CHECK_EQ_STR(buf, "Zurich");
    setuputil::asciiFold("S\xC3\xA3o Paulo", buf, sizeof(buf));
    CHECK_EQ_STR(buf, "Sao Paulo");
    setuputil::asciiFold("\xE6\x9D\xB1\xE4\xBA\xAC", buf, sizeof(buf)); // Tokyo in kanji
    CHECK_EQ_STR(buf, "??");
    setuputil::asciiFold("Llanfairpwllgwyngyll", tiny, 8);
    CHECK_EQ_STR(tiny, "Llanfai");

    setuputil::BodyBuf<8> b;
    b.reset();
    b.add(reinterpret_cast<const uint8_t *>("abcde"), 5);
    b.add(reinterpret_cast<const uint8_t *>("fgh"), 3);
    CHECK(!b.overflow && b.len == 8);
    CHECK_EQ_STR(b.buf, "abcdefgh");
    b.add(reinterpret_cast<const uint8_t *>("i"), 1);
    CHECK(b.overflow);
    b.reset();
    CHECK(!b.overflow && b.len == 0);
}
