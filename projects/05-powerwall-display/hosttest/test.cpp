// Host tests for the plain-C++ parts of project 05: the HTTP body framing (net_body.h: de-chunker and
// read loop), the date parsers (time_util.cpp), the ENSO page/file parsers (enso.cpp, ENSO_HOST_TEST),
// the SD cache (cache.cpp, CACHE_HOST_TEST, on an in-memory card) and the brightness curve (backlight_curve.h).
// Build and run: hosttest\run.ps1 (MSVC). Exit code = number of failed checks.
#include <map>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef _MSC_VER
#define localtime_r(t, tm) localtime_s(tm, t)
#define gmtime_r(t, tm) gmtime_s(tm, t)
#endif

#include "../src/backlight_curve.h"
#include "../src/net_body.h"
#include "../src/time_util.cpp"
#define ENSO_HOST_TEST 1
#include "../src/enso.cpp"
#define CACHE_HOST_TEST 1
#include "../src/cache.cpp"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (cond) g_pass++;                                                                                            \
        else {                                                                                                         \
            g_fail++;                                                                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                     \
        }                                                                                                              \
    } while (0)
#define CHECK_EQ_STR(a, b)                                                                                             \
    do {                                                                                                               \
        if (std::string(a) == std::string(b)) g_pass++;                                                                \
        else {                                                                                                         \
            g_fail++;                                                                                                  \
            printf("FAIL %s:%d  '%s' != '%s'\n", __FILE__, __LINE__, std::string(a).c_str(), std::string(b).c_str()); \
        }                                                                                                              \
    } while (0)

static std::string g_dataDir = "data/";

static std::string slurp(const char *name) {
    std::string path = g_dataDir + name;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        printf("missing %s\n", path.c_str());
        exit(100);
    }
    std::string s;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}

// ---- de-chunker ---------------------------------------------------------------------------------

struct DcResult {
    std::string out;
    bool done = false, last = false, failed = false, tooBig = false;
    size_t consumed = 0;
};

// Feeds `raw` split at the given offsets (sorted, each < raw.size())
static DcResult dechunk(const std::string &raw, const std::vector<size_t> &splits, size_t maxChunk = SIZE_MAX) {
    net::Dechunker dc(maxChunk);
    DcResult r;
    size_t pos = 0;
    std::vector<size_t> cuts = splits;
    cuts.push_back(raw.size());
    for (size_t cut : cuts) {
        if (cut < pos) continue;
        const size_t used =
            dc.feed(reinterpret_cast<const uint8_t *>(raw.data()) + pos, cut - pos, [&r](const uint8_t *p, size_t n) {
                r.out.append(reinterpret_cast<const char *>(p), n);
                return true;
            });
        r.consumed += used;
        pos = cut;
        if (dc.done() || dc.failed()) break;
    }
    r.done = dc.done();
    r.last = dc.lastChunk();
    r.failed = dc.failed();
    r.tooBig = dc.tooBig();
    return r;
}

static void testDechunker() {
    struct Case {
        const char *name;
        std::string raw;
        std::string body;
        bool ok;
    };
    std::string big(5000, 'x');
    for (size_t i = 0; i < big.size(); i++) big[i] = (char)('a' + i % 26);
    char hdr[16];
    snprintf(hdr, sizeof hdr, "%zx\r\n", big.size());
    const std::vector<Case> cases = {
        {"simple", "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n", "hello world", true},
        {"upper hex + ext", "A;name=val\r\n0123456789\r\n0\r\n\r\n", "0123456789", true},
        {"trailer", "3\r\nabc\r\n0\r\nX-Foo: bar\r\nX-Baz: 1\r\n\r\n", "abc", true},
        {"bare LF", "3\nabc\n0\n\n", "abc", true},
        {"size with spaces", "3  \r\nabc\r\n0\r\n\r\n", "abc", true},
        {"json-like", "1d\r\n{\"percentage\":81.234,\"x\":[1]}\r\n0\r\n\r\n", "{\"percentage\":81.234,\"x\":[1]}", true},
        {"big", std::string(hdr) + big + "\r\n0\r\n\r\n", big, true},
        {"binary CRLF in data", std::string("4\r\n\r\n\r\n\r\n0\r\n\r\n"), std::string("\r\n\r\n"), true},
        {"bad hex", "xyz\r\nabc\r\n0\r\n\r\n", "", false},
        {"missing CRLF after data", "3\r\nabcX\r\n0\r\n\r\n", "abc", false},
        {"empty size line", "\r\nabc\r\n", "", false},
        {"too many digits", "123456789\r\n", "", false},
    };
    for (const Case &c : cases) {
        // whole, then split at every single offset, then byte by byte
        DcResult whole = dechunk(c.raw, {});
        if (c.ok) {
            CHECK(whole.done && !whole.failed);
            CHECK_EQ_STR(whole.out, c.body);
            CHECK(whole.consumed == c.raw.size());
        } else {
            CHECK(whole.failed);
        }
        bool allSame = true;
        for (size_t k = 1; k < c.raw.size(); k++) {
            DcResult s = dechunk(c.raw, {k});
            if (s.done != whole.done || s.failed != whole.failed || (c.ok && s.out != whole.out)) {
                allSame = false;
                printf("  split %s at %zu differs\n", c.name, k);
                break;
            }
        }
        CHECK(allSame);
        std::vector<size_t> every;
        for (size_t k = 1; k < c.raw.size(); k++) every.push_back(k);
        DcResult b = dechunk(c.raw, every);
        CHECK(b.done == whole.done && b.failed == whole.failed && (!c.ok || b.out == whole.out));
        // two splits at every pair of offsets (short cases only)
        if (c.raw.size() < 60) {
            bool pairs = true;
            for (size_t i = 1; i < c.raw.size() && pairs; i++)
                for (size_t j = i + 1; j < c.raw.size() && pairs; j++) {
                    DcResult s = dechunk(c.raw, {i, j});
                    if (s.done != whole.done || s.failed != whole.failed || (c.ok && s.out != whole.out)) pairs = false;
                }
            CHECK(pairs);
        }
    }
    // stops at the end of the message: bytes of a following response are not consumed
    {
        std::string raw = "3\r\nabc\r\n0\r\n\r\nHTTP/1.1 200 OK\r\n";
        DcResult r = dechunk(raw, {});
        CHECK(r.done && r.out == "abc" && r.consumed == raw.size() - strlen("HTTP/1.1 200 OK\r\n"));
    }
    // truncated: payload incomplete / only the final CRLF missing
    {
        DcResult r = dechunk("5\r\nhel", {});
        CHECK(!r.done && !r.failed && !r.last && r.out == "hel");
        DcResult r2 = dechunk("3\r\nabc\r\n0\r\n", {});
        CHECK(!r2.done && r2.last && r2.out == "abc");
    }
    // chunk larger than the caller's limit
    {
        DcResult r = dechunk("100\r\n", {}, 255);
        CHECK(r.failed && r.tooBig);
        DcResult r2 = dechunk("ff\r\n", {}, 255);
        CHECK(!r2.failed);
    }
    // aborting sink
    {
        net::Dechunker dc;
        const char *raw = "5\r\nhello\r\n0\r\n\r\n";
        dc.feed(reinterpret_cast<const uint8_t *>(raw), strlen(raw), [](const uint8_t *, size_t) { return false; });
        CHECK(dc.failed() && dc.aborted());
    }
}

// ---- body read loop (readBodyFrom) against a fake socket and clock ------------------------------

struct FakeClock {
    uint32_t t = 0;
    int waits = 0;
    uint32_t now() { return t; }
    void wait() {
        t += 2;
        waits++;
    }
};

// Delivers `data` in segments: segment k (ending at cuts[k]) becomes readable at time k * stepMs.
// closes = the peer closes once everything was delivered; stallAt = nothing after this byte, ever.
struct FakeSock {
    FakeClock *clk;
    std::string data;
    std::vector<size_t> cuts;
    uint32_t stepMs = 10;
    bool closes = true;
    size_t pos = 0;

    size_t delivered() const {
        size_t upTo = 0;
        for (size_t k = 0; k < cuts.size(); k++)
            if (clk->t >= k * stepMs) upTo = cuts[k];
        return upTo;
    }
    int available() { return (int)(delivered() - pos); }
    int read(uint8_t *b, size_t n) {
        size_t a = delivered() - pos;
        if (n > a) n = a;
        memcpy(b, data.data() + pos, n);
        pos += n;
        return (int)n;
    }
    bool connected() { return !(closes && delivered() == data.size()); }
};

static FakeSock sock(FakeClock &c, const std::string &data, size_t seg, bool closes) {
    FakeSock s{&c, data, {}};
    for (size_t p = seg; p < data.size(); p += seg) s.cuts.push_back(p);
    s.cuts.push_back(data.size());
    s.closes = closes;
    return s;
}

static void testReadLoop() {
    using net::BodyStatus;
    const std::string body = "{\"site\":{\"instant_power\":-1234.5},\"load\":{\"instant_power\":987}}";
    std::string chunked;
    for (size_t i = 0; i < body.size(); i += 10) {
        const std::string part = body.substr(i, 10);
        char h[8];
        snprintf(h, sizeof h, "%zx\r\n", part.size());
        chunked += h + part + "\r\n";
    }
    chunked += "0\r\n\r\n";
    static char buf[256];
    for (size_t seg : {(size_t)1, (size_t)3, (size_t)7, (size_t)64, (size_t)1000}) {
        // Content-Length, keep-alive (connection stays open)
        {
            FakeClock c;
            FakeSock s = sock(c, body, seg, false);
            net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
            bool clean = false;
            CHECK(net::readBodyFrom(&s, false, (long)body.size(), t, 5000, c, clean) == BodyStatus::Ok);
            CHECK(clean && std::string(buf, t.len) == body);
        }
        // chunked, keep-alive
        {
            FakeClock c;
            FakeSock s = sock(c, chunked, seg, false);
            net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
            bool clean = false;
            CHECK(net::readBodyFrom(&s, true, -1, t, 5000, c, clean) == BodyStatus::Ok);
            CHECK(clean && std::string(buf, t.len) == body && s.pos == chunked.size());
        }
        // chunked with a (wrong) Content-Length header too: chunked wins
        {
            FakeClock c;
            FakeSock s = sock(c, chunked, seg, false);
            net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
            bool clean = false;
            CHECK(net::readBodyFrom(&s, true, 5, t, 5000, c, clean) == BodyStatus::Ok && std::string(buf, t.len) == body);
        }
        // until close (HTTP/1.0)
        {
            FakeClock c;
            FakeSock s = sock(c, body, seg, true);
            net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
            bool clean = true;
            CHECK(net::readBodyFrom(&s, false, -1, t, 5000, c, clean) == BodyStatus::Ok);
            CHECK(!clean && std::string(buf, t.len) == body);
        }
        // stream mode through a sink
        {
            FakeClock c;
            FakeSock s = sock(c, chunked, seg, false);
            std::string got;
            net::BodyTarget t;
            t.sink = [](void *ctx, const uint8_t *p, size_t n) {
                static_cast<std::string *>(ctx)->append(reinterpret_cast<const char *>(p), n);
                return true;
            };
            t.ctx = &got;
            t.max = 1000;
            bool clean = false;
            CHECK(net::readBodyFrom(&s, true, -1, t, 5000, c, clean) == BodyStatus::Ok && got == body);
        }
    }
    // closed early (Content-Length not reached) / chunked cut off
    {
        FakeClock c;
        FakeSock s = sock(c, body.substr(0, 20), 7, true);
        net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
        bool clean = true;
        CHECK(net::readBodyFrom(&s, false, (long)body.size(), t, 5000, c, clean) == BodyStatus::Incomplete && !clean);
        FakeClock c2;
        FakeSock s2 = sock(c2, chunked.substr(0, 30), 7, true);
        net::BodyTarget t2{buf, nullptr, nullptr, sizeof buf - 1};
        CHECK(net::readBodyFrom(&s2, true, -1, t2, 5000, c2, clean) == BodyStatus::Incomplete);
    }
    // stall: the server stops sending but keeps the connection -> deadline, waiting (not spinning)
    {
        FakeClock c;
        FakeSock s = sock(c, chunked.substr(0, 30), 7, false);
        net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
        bool clean = true;
        CHECK(net::readBodyFrom(&s, true, -1, t, 3000, c, clean) == BodyStatus::Incomplete && !clean);
        CHECK(c.t >= 3000 && c.t < 3010 && c.waits >= 1400);
        FakeClock c2;
        FakeSock s2 = sock(c2, body, 7, false); // until close, but it never closes
        net::BodyTarget t2{buf, nullptr, nullptr, sizeof buf - 1};
        CHECK(net::readBodyFrom(&s2, false, -1, t2, 2000, c2, clean) == BodyStatus::Incomplete && c2.t >= 2000);
    }
    // the final CRLF never arrives (server closes): payload complete -> Ok, not reusable
    {
        FakeClock c;
        FakeSock s = sock(c, chunked.substr(0, chunked.size() - 2), 7, true);
        net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
        bool clean = true;
        CHECK(net::readBodyFrom(&s, true, -1, t, 5000, c, clean) == BodyStatus::Ok && !clean);
    }
    // too big: by Content-Length, by data until close, by chunk size, by a refusing sink
    {
        static char small[16];
        bool clean;
        FakeClock c;
        FakeSock s = sock(c, body, 7, true);
        net::BodyTarget t{small, nullptr, nullptr, sizeof small - 1};
        CHECK(net::readBodyFrom(&s, false, (long)body.size(), t, 5000, c, clean) == BodyStatus::TooBig);
        FakeClock c2;
        FakeSock s2 = sock(c2, body, 7, true);
        net::BodyTarget t2{small, nullptr, nullptr, sizeof small - 1};
        CHECK(net::readBodyFrom(&s2, false, -1, t2, 5000, c2, clean) == BodyStatus::TooBig && t2.len == 15);
        FakeClock c3;
        FakeSock s3 = sock(c3, chunked, 7, false);
        net::BodyTarget t3{small, nullptr, nullptr, sizeof small - 1};
        CHECK(net::readBodyFrom(&s3, true, -1, t3, 5000, c3, clean) == BodyStatus::TooBig && t3.len <= 15);
        FakeClock c4;
        FakeSock s4 = sock(c4, chunked, 7, false);
        net::BodyTarget t4;
        t4.sink = [](void *, const uint8_t *, size_t) { return false; };
        t4.max = 1000;
        CHECK(net::readBodyFrom(&s4, true, -1, t4, 5000, c4, clean) == BodyStatus::TooBig);
    }
    // no stream (HTTPClient: closed, nothing buffered)
    {
        FakeClock c;
        net::BodyTarget t{buf, nullptr, nullptr, sizeof buf - 1};
        bool clean;
        CHECK(net::readBodyFrom<FakeSock>(nullptr, false, 0, t, 100, c, clean) == BodyStatus::Ok);
        CHECK(net::readBodyFrom<FakeSock>(nullptr, false, -1, t, 100, c, clean) == BodyStatus::Ok);
        CHECK(net::readBodyFrom<FakeSock>(nullptr, false, 10, t, 100, c, clean) == BodyStatus::Incomplete);
        CHECK(net::readBodyFrom<FakeSock>(nullptr, true, -1, t, 100, c, clean) == BodyStatus::Incomplete);
    }
}

// ---- time_util ----------------------------------------------------------------------------------

static void testTime() {
    // 2026-10-10T17:00:00Z = 1791651600
    const time_t base = (time_t)tu::daysFromCivil(2026, 10, 10) * 86400 + 17 * 3600;
    CHECK(base == 1791651600);
    CHECK(tu::daysFromCivil(1970, 1, 1) == 0);
    CHECK(tu::daysFromCivil(2000, 3, 1) == 11017);
    CHECK(tu::daysFromCivil(2024, 1, 1) * 86400 == tu::MIN_VALID_UTC);

    CHECK(tu::parseIso8601("2026-10-10T17:00:00+0000") == base);             // AuroraWatch
    CHECK(tu::parseIso8601("2026-10-10T18:00:00+01:00") == base);            // offset with colon
    CHECK(tu::parseIso8601("2026-10-10T18:00+01:00") == base);               // MET Norway: no seconds
    CHECK(tu::parseIso8601("2026-10-10T12:00:00-05:00") == base);            // negative offset
    CHECK(tu::parseIso8601("2026-10-10T17:00:00Z") == base);                 // SWPC
    CHECK(tu::parseIso8601("2026-10-10T17:00:00") == base);                  // SWPC Kp (UTC, no offset)
    CHECK(tu::parseIso8601("2026-10-10T17:00:00.000Z") == base);             // fractional seconds
    CHECK(tu::parseIso8601("2026-10-10 17:00:00") == base);                  // space separator
    CHECK(tu::parseIso8601("2026-10-04T00:00:00") == (time_t)tu::daysFromCivil(2026, 10, 4) * 86400);
    CHECK(tu::parseIso8601("2026-10-11T08:23+01:00") ==
          (time_t)tu::daysFromCivil(2026, 10, 11) * 86400 + 7 * 3600 + 23 * 60); // sky.cpp sample
    CHECK(tu::parseIso8601("2026-10-11T10:38:00Z") ==
          (time_t)tu::daysFromCivil(2026, 10, 11) * 86400 + 10 * 3600 + 38 * 60); // space.cpp sample
    CHECK(tu::parseIso8601("2026-10-10T17:00:05+0000") == base + 5);
    CHECK(tu::parseIso8601("") == 0);
    CHECK(tu::parseIso8601(nullptr) == 0);
    CHECK(tu::parseIso8601("2026-13-10T17:00:00") == 0);
    CHECK(tu::parseIso8601("2026-10-10") == 0);
    CHECK(tu::parseIso8601("garbage") == 0);
    CHECK(tu::parseIso8601("2026-10-10T17") == 0);

    CHECK(tu::parseHttpDate("Sat, 10 Oct 2026 17:00:00 GMT") == base);
    CHECK(tu::parseHttpDate("10 Oct 2026 17:00:00 GMT") == base);
    CHECK(tu::parseHttpDate("Sun, 11 Oct 2026 10:56:05 GMT") ==
          (time_t)tu::daysFromCivil(2026, 10, 11) * 86400 + 10 * 3600 + 56 * 60 + 5);
    CHECK(tu::parseHttpDate("Sat, 10 oct 2026 17:00:00 GMT") == base);
    CHECK(tu::parseHttpDate("Sat, 10 Foo 2026 17:00:00 GMT") == 0);
    CHECK(tu::parseHttpDate("") == 0);
    CHECK(tu::parseHttpDate(nullptr) == 0);

    CHECK(tu::monthFromName("Sep", 3) == 8);
    CHECK(tu::monthFromName("september", 9) == 8);
    CHECK(tu::monthFromName("SEPTEMBER", 9) == 8);
    CHECK(tu::monthFromName("Sept", 4) == -1);
    CHECK(tu::monthFromName("Ma", 2) == -1);
    CHECK(tu::monthFromName("May", 3) == 4);
    CHECK(tu::monthFromName("Novemberx", 8) == 10); // first n chars only

    CHECK(tu::clockValid(tu::MIN_VALID_UTC));
    CHECK(!tu::clockValid(tu::MIN_VALID_UTC - 1));
    CHECK(!tu::clockValid(0));

    char b[8];
    tu::fmtHHMM(b, sizeof b, 0);
    CHECK_EQ_STR(b, "--");
    tu::fmtHHMM(b, sizeof b, base);
    CHECK(strlen(b) == 5 && b[2] == ':');
    const int off = tu::utcOffsetMinutes(base);
    CHECK(off >= -14 * 60 && off <= 14 * 60);
}

// ---- ENSO parsers (real CPC payloads from 2026-10-11) -------------------------------------------

static std::vector<char> cbuf(const std::string &s) {
    std::vector<char> b(s.begin(), s.end());
    b.push_back(0);
    return b;
}

static void testEnso() {
    const std::string page = slurp("ensodisc.html");
    for (size_t chunk : {(size_t)1, (size_t)37, (size_t)4096, page.size()}) {
        static char text[8192];
        HtmlText ht;
        ht.init(text, sizeof text);
        for (size_t i = 0; i < page.size(); i += chunk) {
            const size_t n = page.size() - i < chunk ? page.size() - i : chunk;
            htmlSink(&ht, reinterpret_cast<const uint8_t *>(page.data()) + i, n);
        }
        ht.finish();
        EnsoData d;
        CHECK(parseAlert(text, d) && parseSynopsis(text, d) && parseProbs(text, d) && parseNextUpdate(text, d));
        CHECK_EQ_STR(d.alertText, "El Nino Advisory");
        CHECK(d.alert == EnsoAlert::ElNinoAdvisory);
        CHECK_EQ_STR(d.synopsis, "El Nino continues to strengthen, with a strong-to-very strong El Nino likely through "
                                 "January-March 2027 (remaining greater than an 83% chance).");
        CHECK(d.probCount == 3 && d.probPct[0] == 54 && d.probPct[1] == 83 && d.probPct[2] == 70);
        CHECK_EQ_STR(d.probSeason[0], "SON");
        CHECK_EQ_STR(d.probSeason[2], "NDJ");
        CHECK_EQ_STR(d.probWhat, "historic (+2.5)");
        CHECK_EQ_STR(d.nextUpdate, "12 Nov");
    }
    EnsoData d;
    {
        auto b = cbuf(slurp("wksst9120.tail"));
        CHECK(parseWeekly(b.data(), d));
        CHECK_EQ_STR(d.weeklyDate, "30SEP2026");
        CHECK(d.weeklyAnom > 3.19f && d.weeklyAnom < 3.21f);
    }
    {
        auto b = cbuf(slurp("sstoi.tail"));
        CHECK(parseMonthly(b.data(), d));
        CHECK(d.monthCount == 24 && d.lastYear == 2026 && d.lastMonth == 9);
        CHECK(d.monthAnom[23] > 2.83f && d.monthAnom[23] < 2.85f);
    }
    {
        auto b = cbuf(slurp("oni.tail"));
        CHECK(parseSeasonRow(b.data(), d.oniSeason, d.oni));
        CHECK_EQ_STR(d.oniSeason, "JAS");
        CHECK(d.oni > 2.15f && d.oni < 2.17f);
    }
    {
        auto b = cbuf(slurp("roni.tail"));
        CHECK(parseSeasonRow(b.data(), d.roniSeason, d.roni));
        CHECK_EQ_STR(d.roniSeason, "JAS");
        CHECK(d.roni > 1.68f && d.roni < 1.70f);
    }
    // tail sink: a whole file (Range ignored) ends up as just its tail, fed in any split
    {
        const std::string file = slurp("sstoi.tail");
        for (size_t chunk : {(size_t)1, (size_t)100, file.size()}) {
            static char tb[64];
            Tail t{tb, sizeof tb};
            for (size_t i = 0; i < file.size(); i += chunk) {
                const size_t n = file.size() - i < chunk ? file.size() - i : chunk;
                tailSink(&t, reinterpret_cast<const uint8_t *>(file.data()) + i, n);
            }
            t.finish();
            CHECK_EQ_STR(tb, file.substr(file.size() - 63));
        }
    }
    // HTML stripper edge cases, one byte at a time
    {
        const char *h = "<html><head><style>a{x:y>z}</style><script>var a='</b>';if(1<2){}</script><!-- c > d --></head>"
                        "<body>A&amp;B&nbsp;&nbsp;C &#241;x &Ntilde; &deg;C &bogus; AT&T <b>bold</b>\xC3\xB1 \xF1 end</body>";
        static char t[256];
        HtmlText ht;
        ht.init(t, sizeof t);
        for (const char *q = h; *q; q++) ht.feed(reinterpret_cast<const uint8_t *>(q), 1);
        ht.finish();
        CHECK_EQ_STR(t, "A&B C nx N C AT&T bold n n end");
    }
}

// ---- SD cache: in-memory card for sdcard::* (stubs/sd_store.h) ---------------------------------

namespace fakesd {
bool present = true;
bool failWrites = false;
std::set<std::string> dirs;
std::map<std::string, std::string> files;
void reset() {
    present = true;
    failWrites = false;
    dirs.clear();
    files.clear();
}
std::string parentOf(const std::string &p) { return p.substr(0, p.rfind('/')); }
} // namespace fakesd

namespace sdcard {
bool mounted() { return fakesd::present; }
bool mkdirs(const char *path) {
    if (!fakesd::present) return false;
    const std::string p = path;
    for (size_t i = 1; i <= p.size(); i++)
        if (i == p.size() || p[i] == '/') fakesd::dirs.insert(p.substr(0, i));
    return true;
}
bool writeFile(const char *path, const void *data, size_t len) {
    // like FatFs: no card, an I/O error, or a missing directory all fail
    if (!fakesd::present || fakesd::failWrites || !fakesd::dirs.count(fakesd::parentOf(path))) return false;
    fakesd::files[path] = std::string(static_cast<const char *>(data), len);
    return true;
}
bool readFile(const char *path, void *buf, size_t cap, size_t *outLen) {
    *outLen = 0;
    if (!fakesd::present) return false;
    auto it = fakesd::files.find(path);
    if (it == fakesd::files.end()) return false;
    const size_t n = it->second.size() < cap ? it->second.size() : cap;
    memcpy(buf, it->second.data(), n);
    *outLen = n;
    return true;
}
} // namespace sdcard

static bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

static void testCacheHelpers() {
    using namespace cache;
    const time_t now = 1791651600; // 2026-10-10 17:00 UTC
    CHECK(crc32("123456789", 9) == 0xCBF43926u); // the standard check value
    CHECK(crc32("", 0) == 0);

    // header round trip and every rejection path
    Header h;
    makeHeader(h, 3, 100, now - 60);
    CHECK(memcmp(h.magic, "PWC1", 4) == 0 && h.reserved == 0 && h.reserved2 == 0 && h.size == 100 && h.version == 3);
    time_t t = -1;
    CHECK(checkHeader(h, 3, 100, now, t) && t == now - 60);
    {
        Header b = h;
        b.magic[3] = '2';
        t = -1;
        CHECK(!checkHeader(b, 3, 100, now, t) && t == -1); // untouched on failure
    }
    CHECK(!checkHeader(h, 4, 100, now, t));  // version
    CHECK(!checkHeader(h, 2, 100, now, t));
    CHECK(!checkHeader(h, 3, 101, now, t));  // size
    CHECK(!checkHeader(h, 3, 99, now, t));
    {
        Header b = h;
        b.reserved = 7; // reserved for later: not checked
        CHECK(checkHeader(b, 3, 100, now, t));
    }
    makeHeader(h, 3, 100, 0); // zero time
    CHECK(!checkHeader(h, 3, 100, now, t));
    makeHeader(h, 3, 100, tu::MIN_VALID_UTC - 1); // before the clock can be valid
    CHECK(!checkHeader(h, 3, 100, now, t));
    makeHeader(h, 3, 100, tu::MIN_VALID_UTC);
    CHECK(checkHeader(h, 3, 100, now, t) && t == tu::MIN_VALID_UTC);
    makeHeader(h, 3, 100, -5);
    CHECK(!checkHeader(h, 3, 100, now, t));
    makeHeader(h, 3, 100, now + 300); // 5 min clock skew is fine
    CHECK(checkHeader(h, 3, 100, now, t));
    makeHeader(h, 3, 100, now + 301); // further in the future is not
    CHECK(!checkHeader(h, 3, 100, now, t));
    CHECK(checkHeader(h, 3, 100, 0, t) && t == now + 301); // unknown clock: can't tell, accepted
    makeHeader(h, 3, 100, (time_t)4102444800LL); // 2100: never a real fetch time
    CHECK(!checkHeader(h, 3, 100, 0, t));
    makeHeader(h, 3, 100, now);
    CHECK(checkHeader(h, 3, 100, now, t) && t == now);

    // msFromUtc: millis() - result == age, also across the 2^32 wrap
    auto age = [](uint32_t nowMs, uint32_t atMs) { return (uint32_t)(nowMs - atMs); };
    CHECK(msFromUtc(now - 600, now, 1000000) == 1000000 - 600000);
    CHECK(msFromUtc(now - 600, 0, 12345) == 12345);                       // unknown clock: just fetched
    CHECK(msFromUtc(now - 600, tu::MIN_VALID_UTC - 1, 12345) == 12345);
    CHECK(msFromUtc(now + 10, now, 5000) == 5000);                        // future: just fetched
    CHECK(msFromUtc(now, now, 5000) == 5000);
    CHECK(age(20000, msFromUtc(now - 3600, now, 20000)) == 3600000);      // 20 s after boot, 1 h old: wraps
    CHECK(age(20000, msFromUtc(now - 1, now, 20000)) == 1000);
    {
        const uint32_t nearWrap = 0xFFFFFF00u;
        const uint32_t at = msFromUtc(now - 10, now, nearWrap);
        CHECK(age(nearWrap, at) == 10000);
        CHECK(age(nearWrap + 1000u, at) == 11000); // millis() has wrapped past 0
        CHECK(age(0x00000100u, msFromUtc(now - 10, now, 0x00000100u)) == 10000);
    }
    CHECK(age(5000, msFromUtc(now - 2147482, now, 5000)) == 2147482000u); // just below the clamp: exact
    CHECK(age(5000, msFromUtc(now - 2147483, now, 5000)) == 0x7FFFFFFFu); // clamped (~24.8 days)
    CHECK(age(5000, msFromUtc(tu::MIN_VALID_UTC, now, 5000)) == 0x7FFFFFFFu); // years old
    CHECK(age(5000, msFromUtc(0, now, 5000)) == 0x7FFFFFFFu);
    CHECK(age(5000, msFromUtc(-100, now, 5000)) == 0x7FFFFFFFu);
    // with the staleness rule (millis() - fetchedMs > limit): 3 h old is stale for a 2 h limit, 1 h is not
    CHECK(age(20000, msFromUtc(now - 3 * 3600, now, 20000)) > 2u * 3600u * 1000u);
    CHECK(!(age(20000, msFromUtc(now - 3600, now, 20000)) > 2u * 3600u * 1000u));
    CHECK(age(20000, msFromUtc(now - 30 * 86400, now, 20000)) > 3u * 86400u * 1000u); // ENSO 3-day limit

    // utcFromMs
    CHECK(utcFromMs(1000, 0, 6000) == 0);
    CHECK(utcFromMs(1000, now, 6000) == now - 5);
    CHECK(utcFromMs(0xFFFFF000u, now, 1000) == now - 5); // fetched before the wrap: 5096 ms ago
    CHECK(utcFromMs(6000, now, 6000) == now);
    for (uint32_t nowMs : {0u, 1u, 20000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0xFFFFFF00u}) {
        CHECK(utcFromMs(msFromUtc(now - 600, now, nowMs), now, nowMs) == now - 600); // round trip
    }

    // dueInMs
    constexpr uint32_t MIN = 60000;
    CHECK(dueInMs(0, now, 30 * MIN) == 0);
    CHECK(dueInMs(now - 300, 0, 30 * MIN) == 0);              // unknown clock: due now
    CHECK(dueInMs(now - 300, now, 30 * MIN) == 25 * MIN);
    CHECK(dueInMs(now - 1799, now, 30 * MIN) == 1000);
    CHECK(dueInMs(now - 1800, now, 30 * MIN) == 0);           // exactly one period
    CHECK(dueInMs(now - 1801, now, 30 * MIN) == 0);
    CHECK(dueInMs(now - 11 * 3600, now, 12 * 60 * MIN) == 60 * MIN);
    CHECK(dueInMs(now, now, 30 * MIN) == 30 * MIN);
    CHECK(dueInMs(now + 100, now, 30 * MIN) == 30 * MIN);     // future: a full period
    CHECK(dueInMs(tu::MIN_VALID_UTC, now, 30 * MIN) == 0);    // years old
    CHECK(dueInMs(now - 100, now, 0) == 0);
    CHECK(dueInMs(now - 100, (time_t)4000000000000000000LL, 30 * MIN) == 0); // absurd now: no overflow
    CHECK(dueInMs(now - 1, now, 0xFFFFFFFFu) == 0xFFFFFFFFu - 1000);

    // fmtAge
    char b[16];
    const struct {
        int64_t s;
        const char *want;
    } ages[] = {{0, "0 s"},       {89, "89 s"},       {90, "1 min"},     {5399, "89 min"},       {5400, "1 h"},
                {47 * 3600 + 3599, "47 h"}, {48 * 3600, "2 d"}, {-5, "0 s"}, {(int64_t)1e15, "99999 d"}};
    for (const auto &a : ages) {
        fmtAge(b, sizeof b, a.s);
        CHECK_EQ_STR(b, a.want);
    }
}

struct CacheRec {
    int32_t a;
    float b;
    char s[12];
    int64_t t;
};
struct CacheRecBig {
    int32_t a;
    float b;
    char s[12];
    int64_t t;
    int32_t extra;
};
struct CacheRecPub { // a record published through a Shared<T>-like object, with a millis() stamp
    int32_t v;
    uint32_t fetchedMs;
};
struct FakeShared {
    CacheRecPub v{};
    int sets = 0;
    void set(const CacheRecPub &x) {
        v = x;
        sets++;
    }
};

static void testCacheIo() {
    fakesd::reset();
    Serial.clear();
    const time_t now = time(nullptr); // loadRaw checks against the real clock
    CHECK(tu::clockValid(now));
    const std::string path = "/powerwall/cache/test.bin";
    const CacheRec rec = {42, 1.5f, "hello", 0x1122334455667788LL};
    CacheRec out;
    time_t t = 0;
    auto sentinel = [&out] { memset(&out, 0xAB, sizeof out); };
    auto untouched = [&out] {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(&out);
        for (size_t i = 0; i < sizeof out; i++)
            if (p[i] != 0xAB) return false;
        return true;
    };

    // no card: nothing written, nothing logged
    fakesd::present = false;
    CHECK(!cache::save("test", 1, rec, now - 60));
    CHECK(!cache::load("test", 1, out, t));
    CHECK(fakesd::files.empty() && Serial.out.empty());
    fakesd::present = true;

    // no real time (clock not set): not saved
    CHECK(!cache::save("test", 1, rec, 0));
    CHECK(!cache::save("test", 1, rec, tu::MIN_VALID_UTC - 1));
    CHECK(fakesd::files.empty());

    // round trip (the directory is created: the fake card fails writes into a missing one)
    CHECK(cache::save("test", 1, rec, now - 60));
    CHECK(fakesd::files.count(path) && fakesd::files[path].size() == sizeof(cache::Header) + sizeof(CacheRec) + 4);
    CHECK(fakesd::dirs.count("/powerwall") && fakesd::dirs.count("/powerwall/cache"));
    sentinel();
    CHECK(cache::load("test", 1, out, t) && t == now - 60 && memcmp(&out, &rec, sizeof rec) == 0);
    CHECK(Serial.out.empty()); // success is silent (the restore summary reports it)

    // missing file: silent
    sentinel();
    t = 123;
    CHECK(!cache::load("other", 1, out, t) && untouched() && t == 123 && Serial.out.empty());

    // wrong version / size (bigger and smaller struct)
    sentinel();
    CHECK(!cache::load("test", 2, out, t) && untouched() && has(Serial.out, "test ignored (version 1 != 2)"));
    Serial.clear();
    CacheRecBig big;
    CHECK(!cache::load("test", 1, big, t) && has(Serial.out, "test ignored (size 32 != 40 B)"));
    Serial.clear();
    int32_t small = 0;
    CHECK(!cache::load("test", 1, small, t) && has(Serial.out, "(size 32 != 4 B)"));
    Serial.clear();

    // damaged files; each case starts from a good copy
    const std::string good = fakesd::files[path];
    auto damaged = [&](const std::string &file, const char *why) {
        fakesd::files[path] = file;
        Serial.clear();
        sentinel();
        const bool rejected = !cache::load("test", 1, out, t) && untouched() && has(Serial.out, why);
        if (!rejected) printf("  cache: expected '%s', got '%s'\n", why, Serial.out.c_str());
        return rejected;
    };
    CHECK(damaged(good.substr(0, good.size() - 1), "file 59 B, expected 60"));     // CRC byte missing
    CHECK(damaged(good.substr(0, 30), "file 30 B, expected 60"));                  // cut in the payload
    CHECK(damaged(good.substr(0, 10), "file 10 B, expected 60"));                  // cut in the header
    CHECK(damaged("", "file 0 B, expected 60"));
    CHECK(damaged(good + "x", "file 61 B, expected 60"));                          // too long
    std::string bad = good;
    bad[24 + 5] ^= 0x01; // payload bit flip
    CHECK(damaged(bad, "CRC mismatch"));
    bad = good;
    bad[16] ^= 0x01; // fetchedUtc bit flip: the CRC covers the header too
    CHECK(damaged(bad, "CRC mismatch"));
    bad = good;
    bad[good.size() - 1] ^= 0x80; // the CRC itself
    CHECK(damaged(bad, "CRC mismatch"));
    bad = good;
    bad[0] = 'X';
    CHECK(damaged(bad, "not a cache record"));
    bad = std::string(60, '\0'); // all zeros (e.g. a preallocated file)
    CHECK(damaged(bad, "not a cache record"));
    fakesd::files[path] = good;
    CHECK(cache::load("test", 1, out, t) && memcmp(&out, &rec, sizeof rec) == 0); // the good copy still loads

    // implausible times: an hour in the future (a valid record otherwise)
    Serial.clear();
    CHECK(cache::save("test", 1, rec, now + 3600));
    sentinel();
    CHECK(!cache::load("test", 1, out, t) && untouched() && has(Serial.out, "implausible fetch time"));
    {
        std::vector<uint8_t> r(cache::OVERHEAD + sizeof rec); // valid CRC around a zero time
        cache::buildRecord(r.data(), 1, &rec, sizeof rec, 0);
        Serial.clear();
        CHECK(damaged(std::string(r.begin(), r.end()), "implausible fetch time"));
    }

    // overwrite with newer data
    CacheRec rec2 = rec;
    rec2.a = 7;
    CHECK(cache::save("test", 1, rec2, now - 5));
    CHECK(cache::load("test", 1, out, t) && out.a == 7 && t == now - 5);

    // write failure is logged; a too-long name is refused
    Serial.clear();
    fakesd::failWrites = true;
    CHECK(!cache::save("test", 1, rec, now) && has(Serial.out, "Cache: saving test failed"));
    fakesd::failWrites = false;
    CHECK(!cache::save("a-very-long-record-name-that-does-not-fit", 1, rec, now));
    CHECK(cache::load("test", 1, out, t) && out.a == 7); // the failed save left the old record

    // a big record (the sun image: 8 + 200 x 200 x 2 bytes) in one piece
    {
        const size_t n = 8 + 200 * 200 * 2;
        std::vector<uint8_t> img(n), back(n, 0);
        for (size_t i = 0; i < n; i++) img[i] = (uint8_t)(i * 7 + (i >> 8));
        CHECK(cache::saveRaw("sunimg", 1, img.data(), n, now - 1800));
        CHECK(fakesd::files["/powerwall/cache/sunimg.bin"].size() == n + 28);
        CHECK(cache::loadRaw("sunimg", 1, back.data(), n, t) && back == img && t == now - 1800);
        CHECK(!cache::loadRaw("sunimg", 1, back.data(), n - 2, t)); // size mismatch
    }

    // restoreInto: publishes with fetchedMs rebuilt from the age, returns the fetch time
    {
        Serial.clear();
        const CacheRecPub p = {99, 0};
        CHECK(cache::save("pub", 4, p, now - 120));
        g_hostMillis = 50000;
        FakeShared sh;
        const time_t got = cache::restoreInto("pub", 4, sh, &CacheRecPub::fetchedMs);
        const uint32_t ageMs = (uint32_t)(g_hostMillis - sh.v.fetchedMs);
        CHECK(got == now - 120 && sh.sets == 1 && sh.v.v == 99);
        CHECK(ageMs >= 120000 && ageMs <= 122000); // the host clock may tick meanwhile
        FakeShared none;
        CHECK(cache::restoreInto("nothing", 4, none, &CacheRecPub::fetchedMs) == 0 && none.sets == 0);
        CHECK(cache::restoreInto("pub", 5, none, &CacheRecPub::fetchedMs) == 0 && none.sets == 0); // old version
        fakesd::present = false;
        CHECK(cache::restoreInto("pub", 4, none, &CacheRecPub::fetchedMs) == 0 && none.sets == 0);
        fakesd::present = true;
    }

    // a swapped card has no cache directory: the next save creates it again
    fakesd::reset();
    CHECK(cache::save("test", 1, rec, now - 1) && cache::load("test", 1, out, t));
}

// ---- backlight_curve.h (brightness slider gamma curve) ------------------------------------------

static void testBacklightCurve() {
    using namespace blcurve;
    // The ranges the board produces: 100/255 of 8 bits (100..255, 100 kHz) and of 10 bits (402..1023, 20-25 kHz),
    // plus finer 9/11-bit ranges, a tiny one and a zero minimum.
    struct Range {
        uint32_t lo, hi;
    };
    const Range ranges[] = {{100, 255}, {402, 1023}, {201, 511}, {803, 2047}, {1, 3}, {0, 1000}};

    for (const Range &r : ranges) {
        // endpoints, and 0 % is the floor (lit), not off
        CHECK(dutyForPercent(0, r.lo, r.hi) == r.lo);
        CHECK(dutyForPercent(100, r.lo, r.hi) == r.hi);
        // clamping of the input
        CHECK(dutyForPercent(-7, r.lo, r.hi) == r.lo);
        CHECK(dutyForPercent(1000, r.lo, r.hi) == r.hi);
        // monotonic non-decreasing and always inside [lo, hi]
        uint32_t prev = r.lo;
        for (int p = 0; p <= 100; p++) {
            const uint32_t d = dutyForPercent(p, r.lo, r.hi);
            CHECK(d >= prev);
            CHECK(d >= r.lo && d <= r.hi);
            prev = d;
        }
    }
    CHECK(dutyForPercent(0, 201, 511) > 0);     // min-but-on: never zero while the minimum is non-zero
    CHECK(dutyForPercent(0, 100, 255) == 100);  // B4848_LCD_BL_MIN at the old 8-bit resolution

    // known values (gamma 2.2): 0.5^2.2 = 0.2176, 0.1^2.2 = 0.00631
    CHECK(dutyForPercent(50, 201, 511) == 268);  // 201 + 67.47 -> 268
    CHECK(dutyForPercent(10, 201, 511) == 203);  // 201 + 1.96 -> 203
    CHECK(dutyForPercent(1, 201, 511) == 201);   // below half a step: stays at the floor
    CHECK(dutyForPercent(50, 803, 2047) == 1074); // 803 + 270.7 -> 1074

    // rounding is to nearest, not truncation: 0.2176 * 1000 = 217.6 -> 218, 0.2176 * 10 = 2.18 -> 2
    CHECK(dutyForPercent(50, 0, 1000) == 218);
    CHECK(dutyForPercent(50, 0, 10) == 2);
    CHECK(dutyForPercent(99, 0, 1000) == 978);   // 0.99^2.2 = 0.97814

    // finer steps at the bright end: strictly increasing from 15 % up with 11 bits
    for (int p = 15; p < 100; p++) CHECK(dutyForPercent(p + 1, 803, 2047) > dutyForPercent(p, 803, 2047));

    // the curve is below the straight line in the middle (that is the point of the gamma)
    CHECK(dutyForPercent(50, 201, 511) < 201 + (511 - 201) / 2);

    // empty or inverted range: the minimum
    CHECK(dutyForPercent(80, 300, 300) == 300);
    CHECK(dutyForPercent(80, 300, 200) == 300);
    CHECK(dutyForPercent(100, 300, 300) == 300);

    // inverse: exact where the curve is steep enough (11 bits, from 10 %), bounded elsewhere
    for (int p = 10; p <= 100; p++) CHECK(percentForDuty(dutyForPercent(p, 803, 2047), 803, 2047) == p);
    for (int p = 0; p < 10; p++) CHECK(percentForDuty(dutyForPercent(p, 803, 2047), 803, 2047) <= p + 1);
    CHECK(percentForDuty(0, 201, 511) == 0);     // below the minimum
    CHECK(percentForDuty(201, 201, 511) == 0);
    CHECK(percentForDuty(511, 201, 511) == 100);
    CHECK(percentForDuty(9999, 201, 511) == 100);
    CHECK(percentForDuty(5, 7, 7) == 0);         // empty range

    // saved settings: V2 holds the percentage
    int pct = -1;
    CHECK(percentFromSaved(SETTINGS_MAGIC_V2, 55, 100, pct) && pct == 55);
    CHECK(percentFromSaved(SETTINGS_MAGIC_V2, 0, 100, pct) && pct == 0);
    CHECK(percentFromSaved(SETTINGS_MAGIC_V2, 100, 100, pct) && pct == 100);
    pct = -1;
    CHECK(!percentFromSaved(SETTINGS_MAGIC_V2, 101, 100, pct) && pct == -1); // out of range: rejected, untouched
    CHECK(!percentFromSaved(SETTINGS_MAGIC_V2, 255, 100, pct));
    // V1 (older firmware) holds the raw 8-bit duty 100..255 of the linear slider: same brightness, new position
    CHECK(percentFromSaved(SETTINGS_MAGIC_V1, 255, 100, pct) && pct == 100);
    CHECK(percentFromSaved(SETTINGS_MAGIC_V1, 100, 100, pct) && pct == 0);
    CHECK(percentFromSaved(SETTINGS_MAGIC_V1, 40, 100, pct) && pct == 0);  // below the old minimum
    CHECK(percentFromSaved(SETTINGS_MAGIC_V1, 178, 100, pct) && pct >= 72 && pct <= 74); // (78/155)^(1/2.2) = 0.73
    int last = 0;
    for (int d = 0; d <= 255; d++) {
        CHECK(percentFromSaved(SETTINGS_MAGIC_V1, (uint8_t)d, 100, pct) && pct >= last && pct <= 100);
        last = pct;
    }
    // unknown or garbled magic is rejected
    CHECK(!percentFromSaved(0, 50, 100, pct));
    CHECK(!percentFromSaved(0x53455433, 50, 100, pct));
    CHECK(!percentFromSaved(0xFFFFFFFFu, 50, 100, pct));
    CHECK(SETTINGS_MAGIC_V1 == 0x53455431 && SETTINGS_MAGIC_V2 == 0x53455432); // on-disk values must not change
}

int main(int argc, char **argv) {
    if (argc > 1) g_dataDir = std::string(argv[1]) + "/";
    testDechunker();
    testReadLoop();
    testTime();
    testEnso();
    testCacheHelpers();
    testCacheIo();
    testBacklightCurve();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
