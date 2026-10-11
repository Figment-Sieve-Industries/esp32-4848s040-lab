// El Nino / ENSO from NOAA CPC - see enso.h and docs/11-space-weather-and-climate-data.md section 3.
// One WiFiClientSecure + HTTPClient with keep-alive for five requests:
//   ensodisc.shtml (whole page, streamed through a tag stripper; the odds and the next-update date sit
//   beyond byte 8192), then the tails of wksst9120.for, sstoi.indices, oni.ascii.txt and RONI.ascii.txt.
// Results are merged into a copy of the current data and swapped in under the lock, so a part that fails
// to download or parse keeps its previous value. The parsers are plain C++ (no Arduino types) and are
// tested on the real payloads on the host with -DENSO_HOST_TEST (the Arduino half is compiled out).
#include "enso.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ENSO_HOST_TEST
#include "ca_certs.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#endif

namespace {

// ---------------------------------------------------------------------------------------------
// Small ASCII helpers (locale and sign independent)
// ---------------------------------------------------------------------------------------------
inline char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
inline bool isDig(char c) { return c >= '0' && c <= '9'; }
inline bool isUp(char c) { return c >= 'A' && c <= 'Z'; }
inline bool isAlpha(char c) { return isUp(c) || (c >= 'a' && c <= 'z'); }

// Case-insensitive strstr within [h, end) (end == nullptr: up to the NUL)
const char *ciFind(const char *h, const char *needle, const char *end = nullptr) {
    if (!h) return nullptr;
    const size_t nl = strlen(needle);
    if (!end) end = h + strlen(h);
    for (; h + nl <= end; ++h) {
        size_t i = 0;
        while (i < nl && lc(h[i]) == lc(needle[i])) ++i;
        if (i == nl) return h;
    }
    return nullptr;
}

// Copy n chars of src into dst (capacity cap), NUL-terminated
void copyN(char *dst, size_t cap, const char *src, size_t n) {
    if (cap == 0) return;
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

const char *skipSp(const char *p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

constexpr const char *MONTH_NAMES[12] = {"January", "February", "March",     "April",   "May",      "June",
                                         "July",    "August",   "September", "October", "November", "December"};
constexpr const char *MONTH_LETTERS = "JFMAMJJASOND";
constexpr const char *SEASON_RING = "JFMAMJJASONDJFMAMJJASOND";

// "September", "sep", "Nov" -> 0..11, otherwise -1. Accepts the full name or the 3-letter abbreviation.
int monthFromWord(const char *w, size_t n) {
    if (n < 3) return -1;
    for (int m = 0; m < 12; m++) {
        const char *name = MONTH_NAMES[m];
        if (n != 3 && n != strlen(name)) continue;
        size_t i = 0;
        while (i < n && lc(w[i]) == lc(name[i])) ++i;
        if (i == n) return m;
    }
    return -1;
}

// "SON" or "September-November," or "Nov-Jan" -> 3-letter season code. Trailing/leading punctuation is ignored.
bool seasonWord(const char *w, size_t n, char out[4]) {
    while (n && strchr(",.;:)]\"'", w[n - 1])) --n;
    while (n && strchr("([\"'", *w)) {
        ++w;
        --n;
    }
    if (n == 3) {
        if (!isUp(w[0]) || !isUp(w[1]) || !isUp(w[2])) return false;
        for (int i = 0; i < 12; i++) {
            if (!strncmp(SEASON_RING + i, w, 3)) {
                copyN(out, 4, w, 3);
                return true;
            }
        }
        return false;
    }
    const char *dash = (const char *)memchr(w, '-', n);
    if (!dash) return false;
    const int a = monthFromWord(w, (size_t)(dash - w));
    const int b = monthFromWord(dash + 1, (size_t)(w + n - dash - 1));
    if (a < 0 || b < 0 || ((b - a + 12) % 12) != 2) return false; // exactly three months
    for (int k = 0; k < 3; k++) out[k] = MONTH_LETTERS[(a + k) % 12];
    out[3] = '\0';
    return true;
}

bool parseFloatTok(const char *s, float &out) {
    char *e = nullptr;
    const double v = strtod(s, &e);
    if (e == s || *e != '\0' || !isfinite(v) || fabs(v) > 100.0) return false;
    out = (float)v;
    return true;
}

// Split a line in place into whitespace-separated tokens. Returns the token count (at most maxTok).
int tokenize(char *line, char **tok, int maxTok) {
    int n = 0;
    char *p = line;
    while (*p && n < maxTok) {
        while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r') ++p;
        if (*p) *p++ = '\0';
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// HTML -> plain ASCII text, streaming (so the 13 KB page is never held)
// ---------------------------------------------------------------------------------------------
struct HtmlText {
    char *out = nullptr;
    size_t cap = 0, len = 0;

    void init(char *buf, size_t capacity) {
        out = buf;
        cap = capacity;
        len = 0;
        st = St::Text;
        space = false;
        utf8Lead = false;
        if (cap) out[0] = '\0';
    }

    void feed(const uint8_t *data, size_t n) {
        for (size_t i = 0; i < n; i++) feedChar((char)data[i]);
    }

    void finish() {
        if (cap) out[len] = '\0';
    }

  private:
    enum class St : uint8_t { Text, Tag, Comment, Skip, Entity };
    St st = St::Text;
    bool space = false;      // a whitespace run is pending
    bool utf8Lead = false;   // saw 0xC3 (Latin-1 supplement lead byte)
    bool closing = false;    // inside </tag>
    bool nameDone = false;
    char tag[8] = {};
    uint8_t tagLen = 0;
    const char *skipPat = nullptr;
    uint8_t skipIdx = 0;
    char ent[10] = {};
    uint8_t entLen = 0;
    char c1 = 0, c2 = 0;     // last two chars inside a comment

    void put(char c) {
        if (space) {
            if (len > 0 && len + 1 < cap) out[len++] = ' ';
            space = false;
        }
        if (len + 1 < cap) out[len++] = c;
    }

    // A Unicode code point from an entity or from Latin-1; keep ASCII, fold n-tilde, drop the rest
    void emitCp(unsigned cp) {
        if (cp == 0xA0 || cp == 0x09 || cp == 0x0A || cp == 0x0D || cp == 0x20) space = true;
        else if (cp == 0xF1) put('n');
        else if (cp == 0xD1) put('N');
        else if (cp == 0x2013 || cp == 0x2014) put('-');
        else if (cp == 0x2018 || cp == 0x2019) put('\'');
        else if (cp == 0x201C || cp == 0x201D) put('"');
        else if (cp >= 0x21 && cp < 0x7F) put((char)cp);
        // everything else (degree sign, symbols, other accents) is dropped
    }

    void decodeEntity() {
        ent[entLen] = '\0';
        if (ent[0] == '#') {
            const unsigned cp = (ent[1] == 'x' || ent[1] == 'X') ? (unsigned)strtoul(ent + 2, nullptr, 16)
                                                                 : (unsigned)strtoul(ent + 1, nullptr, 10);
            emitCp(cp);
            return;
        }
        struct E {
            const char *name;
            unsigned cp;
        };
        static constexpr E TABLE[] = {{"nbsp", 0xA0},   {"amp", '&'},     {"lt", '<'},      {"gt", '>'},      {"quot", '"'},
                                      {"apos", '\''},   {"ntilde", 0xF1}, {"Ntilde", 0xD1}, {"ndash", 0x2013}, {"mdash", 0x2014},
                                      {"rsquo", 0x2019}, {"lsquo", 0x2018}, {"ldquo", 0x201C}, {"rdquo", 0x201D}};
        for (const E &e : TABLE) {
            if (!strcmp(ent, e.name)) {
                emitCp(e.cp);
                return;
            }
        }
        // unknown entity (deg, middot, ...): dropped
    }

    void feedChar(char c) {
        switch (st) {
        case St::Text:
            if (c == '<') {
                st = St::Tag;
                tagLen = 0;
                closing = false;
                nameDone = false;
            } else if (c == '&') {
                st = St::Entity;
                entLen = 0;
                utf8Lead = false;
            } else if ((unsigned char)c >= 0x80) {
                const unsigned char u = (unsigned char)c;
                if (utf8Lead) { // second byte of C3 xx
                    utf8Lead = false;
                    if (u == 0xB1) put('n');
                    else if (u == 0x91) put('N');
                } else if (u == 0xC3) {
                    utf8Lead = true;
                } else if (u == 0xF1) { // Latin-1 n-tilde (this server sends it raw in the body text)
                    put('n');
                } else if (u == 0xD1) {
                    put('N');
                } else if (u == 0xA0) {
                    space = true;
                }
            } else {
                utf8Lead = false;
                if ((unsigned char)c <= 0x20) space = true;
                else put(c);
            }
            break;
        case St::Tag:
            if (c == '>') {
                tag[tagLen] = '\0';
                space = true;
                st = St::Text;
                if (!closing && !strcmp(tag, "script")) {
                    skipPat = "</script";
                    skipIdx = 0;
                    st = St::Skip;
                } else if (!closing && !strcmp(tag, "style")) {
                    skipPat = "</style";
                    skipIdx = 0;
                    st = St::Skip;
                }
            } else if (!nameDone) {
                if (tagLen == 0 && c == '/') {
                    closing = true;
                } else if ((isAlpha(c) || isDig(c) || c == '!' || c == '-') && tagLen < sizeof(tag) - 1) {
                    tag[tagLen++] = lc(c);
                    if (tagLen == 3 && !strncmp(tag, "!--", 3)) {
                        st = St::Comment;
                        c1 = c2 = 0;
                    }
                } else {
                    nameDone = true;
                }
            }
            break;
        case St::Comment:
            if (c == '>' && c1 == '-' && c2 == '-') {
                space = true;
                st = St::Text;
            }
            c2 = c1;
            c1 = c;
            break;
        case St::Skip: // inside <script>/<style>: look for the closing tag
            if (lc(c) == skipPat[skipIdx]) {
                skipIdx++;
                if (skipPat[skipIdx] == '\0') { // matched "</script": swallow the rest of the tag
                    st = St::Tag;
                    tagLen = 0;
                    closing = true;
                    nameDone = true;
                }
            } else {
                skipIdx = (c == '<') ? 1 : 0;
            }
            break;
        case St::Entity:
            if (c == ';') {
                decodeEntity();
                st = St::Text;
            } else if ((isAlpha(c) || isDig(c) || c == '#') && entLen < sizeof(ent) - 1) {
                ent[entLen++] = c;
            } else { // not an entity after all: emit the '&' and what followed, then reprocess c
                put('&');
                for (uint8_t i = 0; i < entLen; i++) put(ent[i]);
                st = St::Text;
                feedChar(c);
            }
            break;
        }
    }
};

// ---------------------------------------------------------------------------------------------
// Discussion text parsers (input: the stripped, whitespace-collapsed text)
// ---------------------------------------------------------------------------------------------

// First sentence of p: ends at '.' followed by a space and a capital letter (so "Fig. 1" does not cut it),
// at most outCap-1 chars, cut at a word boundary with "..." if longer.
void firstSentence(const char *p, char *out, size_t outCap) {
    p = skipSp(p);
    size_t n = 0;
    while (p[n]) {
        if (p[n] == '.' && (p[n + 1] == '\0' || (p[n + 1] == ' ' && isUp(p[n + 2])))) {
            n++;
            break;
        }
        n++;
    }
    if (n < outCap) {
        copyN(out, outCap, p, n);
        return;
    }
    size_t cut = outCap - 4; // room for "..."
    while (cut > 0 && p[cut] != ' ') --cut;
    if (cut == 0) cut = outCap - 4;
    while (cut > 0 && (p[cut - 1] == ' ' || p[cut - 1] == ',')) --cut;
    copyN(out, outCap, p, cut);
    memcpy(out + cut, "...", 4); // cut <= outCap - 4, so "..." and the NUL fit
}

// "ENSO Alert System Status: El Nino Advisory Synopsis: ..."
bool parseAlert(const char *t, EnsoData &d) {
    const char *s = ciFind(t, "ENSO Alert System Status:");
    if (!s) return false;
    s = skipSp(s + strlen("ENSO Alert System Status:"));
    const char *syn = ciFind(s, "Synopsis");
    size_t n = syn ? (size_t)(syn - s) : strlen(s);
    if (n > sizeof(d.alertText) - 1) n = sizeof(d.alertText) - 1;
    while (n > 0 && s[n - 1] == ' ') --n;
    if (n == 0) return false;
    copyN(d.alertText, sizeof(d.alertText), s, n);

    const char *end = d.alertText + strlen(d.alertText);
    const bool nino = ciFind(d.alertText, "nino", end) != nullptr;
    const bool nina = ciFind(d.alertText, "nina", end) != nullptr;
    if (ciFind(d.alertText, "final", end)) d.alert = EnsoAlert::FinalAdvisory;
    else if (ciFind(d.alertText, "not active", end)) d.alert = EnsoAlert::None;
    else if (ciFind(d.alertText, "watch", end)) d.alert = nina ? EnsoAlert::LaNinaWatch : nino ? EnsoAlert::ElNinoWatch : EnsoAlert::Unknown;
    else if (ciFind(d.alertText, "advisory", end)) d.alert = nina ? EnsoAlert::LaNinaAdvisory : nino ? EnsoAlert::ElNinoAdvisory : EnsoAlert::Unknown;
    else d.alert = EnsoAlert::Unknown;
    return true;
}

bool parseSynopsis(const char *t, EnsoData &d) {
    const char *s = ciFind(t, "Synopsis:");
    if (!s) return false;
    s += strlen("Synopsis:");
    char tmp[sizeof(d.synopsis)];
    firstSentence(s, tmp, sizeof(tmp));
    if (!tmp[0]) return false;
    memcpy(d.synopsis, tmp, sizeof(tmp));
    return true;
}

// "... the chance is 54% for September-November, 83% for October-December, and 70% for November-January."
// Also accepts "54% for SON". Up to three "NN%" each followed within three words by a season.
bool parseProbs(const char *t, EnsoData &d) {
    const char *start = ciFind(t, "Synopsis:");
    if (!start) start = t;
    int8_t pct[EnsoData::PROBS] = {-1, -1, -1};
    char seas[EnsoData::PROBS][4] = {};
    int count = 0;
    const char *firstNum = nullptr;
    const char *lastEnd = nullptr;
    for (const char *p = start; *p && count < EnsoData::PROBS; ++p) {
        if (*p != '%') continue;
        const char *q = p;
        while (q > start && isDig(q[-1])) --q;
        const size_t nd = (size_t)(p - q);
        if (nd == 0 || nd > 3) continue;
        const int v = atoi(q);
        if (v > 100) continue;
        const char *w = p + 1;
        const char *wordEnd = nullptr;
        char season[4] = {};
        for (int k = 0; k < 4 && !wordEnd; k++) {
            w = skipSp(w);
            const char *e = w;
            while (*e && *e != ' ') ++e;
            if (e == w) break;
            if (seasonWord(w, (size_t)(e - w), season)) wordEnd = e;
            else w = e;
        }
        if (!wordEnd) continue;
        if (count > 0 && q - lastEnd > 80) break; // a stray % later in the text
        if (count == 0) firstNum = q;
        pct[count] = (int8_t)v;
        memcpy(seas[count], season, 4);
        count++;
        lastEnd = wordEnd;
        p = wordEnd - 1;
    }
    if (count == 0) return false;

    // What are the odds for? Look at the sentence containing the first percentage.
    const char *sent = start;
    for (const char *i = firstNum - 1; i > start + 2 && firstNum - i < 700; --i) {
        if (*i == ' ' && i[-1] == '.' && isUp(i[1])) {
            sent = i + 1;
            break;
        }
    }
    char what[sizeof(d.probWhat)] = "";
    if (ciFind(sent, "historic", firstNum)) {
        copyN(what, sizeof(what), "historic", 8);
        for (const char *i = sent; i + 3 < firstNum; ++i) { // "+2.5" threshold, if quoted
            if (*i == '+' && isDig(i[1]) && i[2] == '.' && isDig(i[3])) {
                snprintf(what, sizeof(what), "historic (+%c.%c)", i[1], i[3]);
                break;
            }
        }
    } else if (ciFind(sent, "very strong", firstNum)) {
        copyN(what, sizeof(what), "very strong+", 12);
    } else if (ciFind(sent, "strong", firstNum)) {
        copyN(what, sizeof(what), "strong+", 7);
    } else if (ciFind(sent, "moderate", firstNum)) {
        copyN(what, sizeof(what), "moderate+", 9);
    }

    d.probCount = count;
    for (int i = 0; i < EnsoData::PROBS; i++) {
        d.probPct[i] = i < count ? pct[i] : (int8_t)-1;
        memcpy(d.probSeason[i], i < count ? seas[i] : "\0\0\0", 4);
    }
    memcpy(d.probWhat, what, sizeof(d.probWhat));
    return true;
}

// "The next ENSO Diagnostics Discussion is scheduled for 12 November 2026." -> "12 Nov"
bool parseNextUpdate(const char *t, EnsoData &d) {
    const char *s = ciFind(t, "next ENSO");
    if (!s) return false;
    const char *end = s + strlen(s);
    if (end - s > 160) end = s + 160;
    for (const char *p = s; p < end; ++p) {
        if (!isDig(*p) || (p > s && isDig(p[-1]))) continue;
        const char *q = p;
        while (isDig(*q)) ++q;
        if (q - p > 2 || *q != ' ') continue;
        const char *w = q + 1;
        const char *e = w;
        while (isAlpha(*e)) ++e;
        const int m = monthFromWord(w, (size_t)(e - w));
        if (m < 0) continue;
        snprintf(d.nextUpdate, sizeof(d.nextUpdate), "%d %.3s", atoi(p), MONTH_NAMES[m]);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Data-file tail parsers. buf is NUL-terminated and may start mid-line. Lines are split in place.
// ---------------------------------------------------------------------------------------------

// Calls fn(line) for every complete-looking line, newest first
template <typename F> bool forLinesNewestFirst(char *buf, F fn) {
    char *end = buf + strlen(buf);
    while (end > buf) {
        while (end > buf && (end[-1] == '\n' || end[-1] == '\r')) --end; // trailing newline(s)
        char *ls = end;
        while (ls > buf && ls[-1] != '\n') --ls;
        if (ls == buf) return false; // the first line may be partial: not used
        char saved = *end;
        *end = '\0';
        const bool done = fn(ls);
        *end = saved;
        if (done) return true;
        end = ls;
    }
    return false;
}

bool validDate9(const char *s) { // "30SEP2026"
    return strlen(s) == 9 && isDig(s[0]) && isDig(s[1]) && isUp(s[2]) && isUp(s[3]) && isUp(s[4]) && isDig(s[5]) &&
           isDig(s[6]) && isDig(s[7]) && isDig(s[8]);
}

// wksst9120.for: " 30SEP2026     26.1 5.3     29.0 4.0     29.9 3.2     29.9 1.2" (Nino1+2, Nino3, Nino34, Nino4: SST SSTA)
bool parseWeekly(char *buf, EnsoData &d) {
    return forLinesNewestFirst(buf, [&d](char *line) {
        char *tok[10];
        if (tokenize(line, tok, 10) != 9 || !validDate9(tok[0])) return false;
        float sst, ssta;
        if (!parseFloatTok(tok[5], sst) || !parseFloatTok(tok[6], ssta)) return false;
        d.weeklyAnom = ssta;
        copyN(d.weeklyDate, sizeof(d.weeklyDate), tok[0], strlen(tok[0]));
        return true;
    });
}

// sstoi.indices: "2026   9   25.27 4.69  28.65 3.64  29.62 0.95  29.60 2.84". Last column = Nino3.4 anomaly.
// Keeps the newest EnsoData::MONTHS rows, oldest first.
bool parseMonthly(char *buf, EnsoData &d) {
    float rows[EnsoData::MONTHS];
    int yrs[EnsoData::MONTHS], mons[EnsoData::MONTHS];
    int n = 0;
    // forward walk: skip the first (possibly partial) line
    char *p = strchr(buf, '\n');
    if (!p) return false;
    ++p;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        char *tok[12];
        const int nt = tokenize(p, tok, 12);
        if (nt == 10) {
            char *e1 = nullptr, *e2 = nullptr;
            const long y = strtol(tok[0], &e1, 10);
            const long m = strtol(tok[1], &e2, 10);
            float a;
            if (*e1 == '\0' && *e2 == '\0' && y > 1900 && y < 3000 && m >= 1 && m <= 12 && parseFloatTok(tok[9], a)) {
                if (n == EnsoData::MONTHS) { // keep the newest
                    memmove(rows, rows + 1, sizeof(rows) - sizeof(rows[0]));
                    memmove(yrs, yrs + 1, sizeof(yrs) - sizeof(yrs[0]));
                    memmove(mons, mons + 1, sizeof(mons) - sizeof(mons[0]));
                    n--;
                }
                rows[n] = a;
                yrs[n] = (int)y;
                mons[n] = (int)m;
                n++;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (n == 0) return false;
    d.monthCount = n;
    memcpy(d.monthAnom, rows, sizeof(float) * (size_t)n);
    for (int i = n; i < EnsoData::MONTHS; i++) d.monthAnom[i] = 0.0f;
    d.lastYear = yrs[n - 1];
    d.lastMonth = mons[n - 1];
    return true;
}

// oni.ascii.txt "  JAS 2026  29.12   2.16" / RONI.ascii.txt "JAS  2026 1.69": season, year, ..., anomaly last
bool parseSeasonRow(char *buf, char season[4], float &anom) {
    return forLinesNewestFirst(buf, [&](char *line) {
        char *tok[8];
        const int nt = tokenize(line, tok, 8);
        if (nt < 3 || nt > 5) return false;
        char s3[4];
        if (strlen(tok[0]) != 3 || !seasonWord(tok[0], 3, s3)) return false;
        char *e = nullptr;
        const long y = strtol(tok[1], &e, 10);
        if (*e != '\0' || y < 1900 || y > 3000) return false;
        float a;
        if (!parseFloatTok(tok[nt - 1], a)) return false;
        memcpy(season, s3, 4);
        anom = a;
        return true;
    });
}

// ---------------------------------------------------------------------------------------------
// Everything below is the Arduino half
// ---------------------------------------------------------------------------------------------
#ifndef ENSO_HOST_TEST

constexpr const char *CPC_BASE = "https://www.cpc.ncep.noaa.gov";
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t READ_TIMEOUT_MS = 6000;
constexpr uint32_t HANDSHAKE_TIMEOUT_S = 5;
constexpr const char *USER_AGENT = "powerwall-display/1.0 (personal ESP32 display; Figment Sieve Industries)";
constexpr size_t WORK_BYTES = 8192;        // one PSRAM buffer: discussion text, or a file tail
constexpr size_t TAIL_BYTES = 2304;        // largest tail we keep (24 sstoi rows of ~73 bytes + a partial line)
constexpr size_t MAX_BODY_BYTES = 262144;  // abort if a server ignores Range on the 148 KB weekly file and keeps going

EnsoData s_enso;

SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex(); // created on first use (thread-safe static init)
    return m;
}

struct Guard {
    Guard() { xSemaphoreTake(lock(), portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(lock()); }
};

// HTTPClient::writeToStream target. Either strips HTML into a text buffer, or keeps the last tailCap bytes
// (so a 200 reply with the whole file, when Range is ignored, still ends up as just the tail).
class Sink : public Stream {
  public:
    explicit Sink(HtmlText *h) : html_(h) {} // NOLINT(clang-analyzer-optin.cplusplus.UninitializedObject) Stream::_startMillis is the Arduino core's
    Sink(char *tail, size_t tailCap) : tail_(tail), tailCap_(tailCap) {} // NOLINT(clang-analyzer-optin.cplusplus.UninitializedObject) Stream::_startMillis is the Arduino core's

    size_t write(const uint8_t *b, size_t n) override {
        total_ += n;
        if (total_ > MAX_BODY_BYTES) return 0; // makes writeToStream fail
        if (html_) {
            html_->feed(b, n);
        } else if (n >= tailCap_ - 1) {
            memcpy(tail_, b + n - (tailCap_ - 1), tailCap_ - 1);
            tailLen_ = tailCap_ - 1;
        } else {
            if (tailLen_ + n > tailCap_ - 1) {
                const size_t drop = tailLen_ + n - (tailCap_ - 1);
                memmove(tail_, tail_ + drop, tailLen_ - drop);
                tailLen_ -= drop;
            }
            memcpy(tail_ + tailLen_, b, n);
            tailLen_ += n;
        }
        return n;
    }
    size_t write(uint8_t b) override { return write(&b, 1); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

    void finish() {
        if (html_) html_->finish();
        else tail_[tailLen_] = '\0';
    }
    size_t total() const { return total_; }

  private:
    HtmlText *html_ = nullptr;
    char *tail_ = nullptr;
    size_t tailCap_ = 0, tailLen_ = 0, total_ = 0;
};

// One GET with a Range header on the shared keep-alive connection. 200 and 206 are both fine.
bool fetchRange(HTTPClient &http, WiFiClientSecure &client, const char *path, const char *range, Sink &sink) {
    String url = CPC_BASE;
    url += path;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!http.begin(client, url)) return false;
        http.addHeader("Range", range);
        const int code = http.GET();
        if (code == HTTP_CODE_OK || code == HTTP_CODE_PARTIAL_CONTENT) {
            const int n = http.writeToStream(&sink);
            if (n >= 0) {
                sink.finish();
                return true;
            }
            Serial.printf("ENSO: %s body -> %s\n", path, HTTPClient::errorToString(n).c_str());
            client.stop();
            return false;
        }
        client.stop(); // unread body or dead connection: never reuse it
        if (code > 0) {
            Serial.printf("ENSO: %s -> HTTP %d\n", path, code);
            return false;
        }
        // negative: usually the server closed our idle keep-alive connection; reconnect once
        if (attempt == 1) Serial.printf("ENSO: %s -> %s\n", path, HTTPClient::errorToString(code).c_str());
    }
    return false;
}

#endif // !ENSO_HOST_TEST

} // namespace

#ifndef ENSO_HOST_TEST

EnsoData ensoData() {
    Guard g;
    return s_enso;
}

bool ensoFetch() {
    char *work = (char *)heap_caps_malloc(WORK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!work) work = (char *)malloc(WORK_BYTES);
    if (!work) {
        Serial.println("ENSO: no memory for the work buffer");
        return false;
    }

    EnsoData d = ensoData(); // merge into a copy of what we have; failed parts keep their old values
    bool ok = true;
    bool any = false;
    {
        WiFiClientSecure client;
        client.setCACert(CPC_CA);
        client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
        HTTPClient http;
        http.setReuse(true);
        http.setUserAgent(USER_AGENT);
        http.setConnectTimeout(CONNECT_TIMEOUT_MS);
        http.setTimeout(READ_TIMEOUT_MS);

        // 1. The discussion page: whole page (about 13.5 KB; the odds and next date are past byte 8192),
        // streamed through the tag stripper into an 8 KB text buffer
        {
            HtmlText ht;
            ht.init(work, WORK_BYTES);
            Sink sink(&ht);
            if (fetchRange(http, client, "/products/analysis_monitoring/enso_advisory/ensodisc.shtml", "bytes=0-16383", sink)) {
                const bool a = parseAlert(work, d);
                const bool s = parseSynopsis(work, d);
                const bool p = parseProbs(work, d);
                const bool n = parseNextUpdate(work, d);
                if (!a) Serial.println("ENSO: no alert status in the discussion text");
                if (!p) Serial.println("ENSO: no odds in the discussion text");
                ok = ok && a; // odds/date/synopsis are best effort: their wording changes with the season
                any = any || a || s || p || n;
            } else {
                ok = false;
            }
        }

        // 2. Weekly SST, last row
        {
            Sink sink(work, TAIL_BYTES);
            if (fetchRange(http, client, "/data/indices/wksst9120.for", "bytes=-300", sink) && parseWeekly(work, d)) {
                any = true;
            } else {
                Serial.println("ENSO: weekly Nino SST failed");
                ok = false;
            }
        }

        // 3. Monthly Nino3.4 anomaly, last 24 rows (73 bytes per row)
        {
            Sink sink(work, TAIL_BYTES);
            if (fetchRange(http, client, "/data/indices/sstoi.indices", "bytes=-1900", sink) && parseMonthly(work, d)) {
                any = true;
            } else {
                Serial.println("ENSO: monthly Nino SST failed");
                ok = false;
            }
        }

        // 4. ONI and RONI, last row each
        {
            Sink sink(work, TAIL_BYTES);
            float v = 0;
            char seas[4] = "";
            if (fetchRange(http, client, "/data/indices/oni.ascii.txt", "bytes=-200", sink) && parseSeasonRow(work, seas, v)) {
                d.oni = v;
                memcpy(d.oniSeason, seas, 4);
                any = true;
            } else {
                Serial.println("ENSO: ONI failed");
                ok = false;
            }
        }
        {
            Sink sink(work, TAIL_BYTES);
            float v = 0;
            char seas[4] = "";
            if (fetchRange(http, client, "/data/indices/RONI.ascii.txt", "bytes=-150", sink) && parseSeasonRow(work, seas, v)) {
                d.roni = v;
                memcpy(d.roniSeason, seas, 4);
                any = true;
            } else {
                Serial.println("ENSO: RONI failed");
                ok = false;
            }
        }
        http.end();
        client.stop();
    }
    heap_caps_free(work);

    if (!any) return false;
    d.valid = !isnan(d.weeklyAnom) || !isnan(d.oni);
    d.fetchedMs = millis();
    {
        Guard g;
        s_enso = d;
    }

    char odds[64] = "";
    size_t o = 0;
    for (int i = 0; i < d.probCount && o < sizeof(odds) - 1; i++)
        o += (size_t)snprintf(odds + o, sizeof(odds) - o, "%s%s %d", i ? " " : "", d.probSeason[i], (int)d.probPct[i]);
    Serial.printf("ENSO: %s, weekly Nino3.4 %+.1f (%s), ONI %s %+.2f, RONI %s %+.2f, odds %s%s%s, next %s%s\n",
                  d.alertText[0] ? d.alertText : "?", (double)d.weeklyAnom, d.weeklyDate, d.oniSeason, (double)d.oni,
                  d.roniSeason, (double)d.roni, odds[0] ? odds : "?", d.probWhat[0] ? " for " : "", d.probWhat,
                  d.nextUpdate[0] ? d.nextUpdate : "?", ok ? "" : " (partial)");
    return ok;
}

#endif // !ENSO_HOST_TEST
