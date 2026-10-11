// Host tests for the plain-C++ parts of project 05: the HTTP body framing (net_body.h: de-chunker and
// read loop), the date parsers (time_util.cpp) and the ENSO page/file parsers (enso.cpp, ENSO_HOST_TEST).
// Build and run: hosttest\run.ps1 (MSVC). Exit code = number of failed checks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef _MSC_VER
#define localtime_r(t, tm) localtime_s(tm, t)
#define gmtime_r(t, tm) gmtime_s(tm, t)
#endif

#include "../src/net_body.h"
#include "../src/time_util.cpp"
#define ENSO_HOST_TEST 1
#include "../src/enso.cpp"

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

int main(int argc, char **argv) {
    if (argc > 1) g_dataDir = std::string(argv[1]) + "/";
    testDechunker();
    testReadLoop();
    testTime();
    testEnso();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
