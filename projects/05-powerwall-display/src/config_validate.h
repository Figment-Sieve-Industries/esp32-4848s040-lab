#pragma once
// Pure (no Arduino, no Preferences, no ArduinoJson) part of the user-config store: validation, canonical form,
// CRC-32 and the config.json <-> cfg::Config mapping. Header-only so hosttest/ builds it with MSVC.
//
// JSON schema (SD file /powerwall/config.json and the setup portal):
//   {"wifi":[{"ssid":"...","password":"..."}],
//    "powerwall":{"host":"...","password":"..."},
//    "location":{"lat":51.5,"lon":-0.1,"name":"...","tz":"GMT0BST,M3.5.0/1,M10.5.0"},
//    "auroraUk":true}
// - wifi: 1..3 entries; ssid 1..32 bytes; password empty (open network), 8..63 chars, or 64 hex digits.
// - powerwall.host: IPv4 address or host name (letters, digits, '-', '.'), 1..63 chars.
//   powerwall.password: 1..32 chars.
// - location: lat -90..90, lon -180..180, tz (POSIX TZ string) 1..63 chars, name 0..23 ASCII chars (the device font
//   has no other glyphs; non-ASCII characters in a parsed name are replaced by '?').
// - auroraUk optional, default true. Unknown keys are ignored (so a "passwordSet" flag round-trips harmlessly).
// toJson(includeSecrets=false) writes "password":"" and "passwordSet":true/false instead of the passwords.
// Error texts (the `why` strings) never contain field values, so passwords can't leak into logs.
#include "config_store.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cfgv {

using cfg::Config;

inline size_t strLen(const char *s, size_t cap) { return strnlen(s, cap); }
template <size_t N> inline size_t fieldLen(const char (&a)[N]) { return strnlen(a, N); }
template <size_t N> inline bool terminated(const char (&a)[N]) { return memchr(a, '\0', N) != nullptr; }

inline bool fail(char *why, size_t n, const char *text) {
    if (why && n) {
        strncpy(why, text, n - 1);
        why[n - 1] = '\0';
    }
    return false;
}

// CRC-32 (IEEE, reflected), bitwise: tiny and only run on a ~500 byte blob.
inline uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// Copies with everything after each string's NUL zeroed, so equal configs are byte-identical (CRC, memcmp).
inline void canonical(const Config &in, Config &out) {
    memset(&out, 0, sizeof out);
    for (int i = 0; i < cfg::MAX_WIFI; i++) {
        strncpy(out.wifi[i].ssid, in.wifi[i].ssid, sizeof out.wifi[i].ssid - 1);
        strncpy(out.wifi[i].pass, in.wifi[i].pass, sizeof out.wifi[i].pass - 1);
    }
    out.wifiCount = in.wifiCount;
    strncpy(out.pwHost, in.pwHost, sizeof out.pwHost - 1);
    strncpy(out.pwPassword, in.pwPassword, sizeof out.pwPassword - 1);
    out.lat = in.lat;
    out.lon = in.lon;
    strncpy(out.placeName, in.placeName, sizeof out.placeName - 1);
    strncpy(out.tz, in.tz, sizeof out.tz - 1);
    out.auroraUk = in.auroraUk;
}

// Replaces each non-ASCII UTF-8 sequence (and control characters) with one '?'. In place.
inline void sanitizeAscii(char *s) {
    char *w = s;
    for (const unsigned char *r = reinterpret_cast<const unsigned char *>(s); *r; r++) {
        if (*r >= 0x80 || *r < 0x20 || *r == 0x7F) { // non-ASCII (or a stray continuation byte) or a control char
            const bool lead = *r >= 0xC0; // read before writing: w may equal r
            *w++ = '?';
            if (lead)
                while ((r[1] & 0xC0) == 0x80) r++;
        } else {
            *w++ = static_cast<char>(*r);
        }
    }
    *w = '\0';
}

inline bool isHex64(const char *s, size_t n) {
    if (n != 64) return false;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F')))
            return false;
    return true;
}

// IPv4 dotted quad or a plausible host name. A string of only digits and dots must be a valid IPv4 address.
inline bool plausibleHost(const char *h, size_t n) {
    if (n == 0 || n > 63 || h[0] == '.' || h[0] == '-' || h[n - 1] == '.' || h[n - 1] == '-') return false;
    bool numeric = true;
    for (size_t i = 0; i < n; i++) {
        const char c = h[i];
        const bool digit = c >= '0' && c <= '9';
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!(digit || alpha || c == '-' || c == '.')) return false;
        if (!(digit || c == '.')) numeric = false;
        if (c == '.' && i + 1 < n && h[i + 1] == '.') return false;
    }
    if (!numeric) return true;
    int parts = 0;
    size_t i = 0;
    while (i < n) {
        int v = 0, digits = 0;
        while (i < n && h[i] != '.') {
            v = v * 10 + (h[i] - '0');
            digits++;
            i++;
            if (digits > 3) return false;
        }
        if (digits == 0 || v > 255) return false;
        parts++;
        if (i < n) i++; // the dot
    }
    return parts == 4;
}

inline bool validate(const Config &c, char *why, size_t whyLen) {
    if (c.wifiCount < 1 || c.wifiCount > cfg::MAX_WIFI) return fail(why, whyLen, "need 1 to 3 Wi-Fi networks");
    for (int i = 0; i < c.wifiCount; i++) {
        if (!terminated(c.wifi[i].ssid) || !terminated(c.wifi[i].pass)) return fail(why, whyLen, "Wi-Fi field not terminated");
        const size_t sl = fieldLen(c.wifi[i].ssid), pl = fieldLen(c.wifi[i].pass);
        if (sl < 1 || sl > 32) return fail(why, whyLen, "Wi-Fi name must be 1 to 32 bytes");
        if (!(pl == 0 || (pl >= 8 && pl <= 63) || isHex64(c.wifi[i].pass, pl)))
            return fail(why, whyLen, "Wi-Fi password must be 8 to 63 chars (or empty)");
    }
    if (!terminated(c.pwHost) || !terminated(c.pwPassword) || !terminated(c.tz) || !terminated(c.placeName))
        return fail(why, whyLen, "field not terminated");
    if (!plausibleHost(c.pwHost, fieldLen(c.pwHost))) return fail(why, whyLen, "gateway address missing or invalid");
    const size_t gl = fieldLen(c.pwPassword);
    if (gl < 1 || gl > 32) return fail(why, whyLen, "gateway password must be 1 to 32 chars");
    if (!(c.lat >= -90.0f && c.lat <= 90.0f)) return fail(why, whyLen, "latitude must be -90 to 90");
    if (!(c.lon >= -180.0f && c.lon <= 180.0f)) return fail(why, whyLen, "longitude must be -180 to 180");
    const size_t tl = fieldLen(c.tz);
    if (tl < 1 || tl > 63) return fail(why, whyLen, "time zone missing or too long");
    const size_t nl = fieldLen(c.placeName);
    if (nl > 23) return fail(why, whyLen, "place name max 23 chars");
    for (size_t i = 0; i < nl; i++) {
        const unsigned char ch = static_cast<unsigned char>(c.placeName[i]);
        if (ch < 0x20 || ch > 0x7E) return fail(why, whyLen, "place name must be plain ASCII");
    }
    return true;
}

// ---- JSON writer ---------------------------------------------------------------------------------
struct Out {
    char *b;
    size_t cap, len = 0;
    bool over = false;
    void put(const char *s, size_t n) {
        if (over || len + n + 1 > cap) {
            over = true;
            return;
        }
        memcpy(b + len, s, n);
        len += n;
        b[len] = '\0';
    }
    void put(const char *s) { put(s, strlen(s)); }
    void str(const char *s) {
        put("\"");
        for (; *s; s++) {
            char e[8];
            if (*s == '"') put("\\\"");
            else if (*s == '\\') put("\\\\");
            else if (static_cast<unsigned char>(*s) < 0x20) {
                snprintf(e, sizeof e, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(*s)));
                put(e);
            } else put(s, 1);
        }
        put("\"");
    }
    void num(float v) {
        char e[24];
        snprintf(e, sizeof e, "%.4f", static_cast<double>(v));
        put(e);
    }
};

// Returns the length written, or 0 (empty string) when the buffer is too small.
inline size_t writeJson(const Config &c, bool includeSecrets, char *buf, size_t len) {
    if (!buf || !len) return 0;
    buf[0] = '\0';
    Out o{buf, len};
    o.put("{\"wifi\":[");
    for (int i = 0; i < c.wifiCount && i < cfg::MAX_WIFI; i++) {
        if (i) o.put(",");
        o.put("{\"ssid\":");
        o.str(c.wifi[i].ssid);
        o.put(",\"password\":");
        o.str(includeSecrets ? c.wifi[i].pass : "");
        if (!includeSecrets) o.put(c.wifi[i].pass[0] ? ",\"passwordSet\":true" : ",\"passwordSet\":false");
        o.put("}");
    }
    o.put("],\"powerwall\":{\"host\":");
    o.str(c.pwHost);
    o.put(",\"password\":");
    o.str(includeSecrets ? c.pwPassword : "");
    if (!includeSecrets) o.put(c.pwPassword[0] ? ",\"passwordSet\":true" : ",\"passwordSet\":false");
    o.put("},\"location\":{\"lat\":");
    o.num(c.lat);
    o.put(",\"lon\":");
    o.num(c.lon);
    o.put(",\"name\":");
    o.str(c.placeName);
    o.put(",\"tz\":");
    o.str(c.tz);
    o.put("},\"auroraUk\":");
    o.put(c.auroraUk ? "true" : "false");
    o.put("}");
    if (o.over) {
        buf[0] = '\0';
        return 0;
    }
    return o.len;
}

// ---- JSON reader (small recursive descent; no allocation, no DOM) --------------------------------
struct Reader {
    const char *p, *e;
    char *why;
    size_t whyLen;

    bool bad(const char *t) { return fail(why, whyLen, t); }
    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    }
    bool eat(char c) {
        ws();
        if (p < e && *p == c) {
            p++;
            return true;
        }
        return false;
    }
    static int hexv(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    // Parses a string into out (cap bytes incl. NUL; out may be null = discard). overflow set if it didn't fit.
    bool str(char *out, size_t cap, bool &overflow) {
        overflow = false;
        if (!eat('"')) return bad("JSON syntax error");
        size_t n = 0;
        auto put = [&](char c) {
            if (out && n + 1 < cap) out[n++] = c;
            else overflow = true;
        };
        while (p < e && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (p >= e) break;
                c = *p++;
                switch (c) {
                case 'n': put('\n'); break;
                case 't': put('\t'); break;
                case 'r': put('\r'); break;
                case 'b': put('\b'); break;
                case 'f': put('\f'); break;
                case '"': case '\\': case '/': put(c); break;
                case 'u': {
                    if (e - p < 4) return bad("JSON syntax error");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        const int h = hexv(*p++);
                        if (h < 0) return bad("JSON syntax error");
                        cp = cp * 16 + static_cast<unsigned>(h);
                    }
                    if (cp < 0x80) put(static_cast<char>(cp));
                    else if (cp < 0x800) {
                        put(static_cast<char>(0xC0 | (cp >> 6)));
                        put(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        put(static_cast<char>(0xE0 | (cp >> 12)));
                        put(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        put(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return bad("JSON syntax error");
                }
            } else {
                put(c);
            }
        }
        if (p >= e) return bad("JSON syntax error");
        p++; // closing quote
        if (out && cap) out[n] = '\0';
        return true;
    }
    bool number(double &v) {
        ws();
        char tmp[40];
        size_t n = 0;
        while (p < e && n < sizeof tmp - 1 &&
               ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E'))
            tmp[n++] = *p++;
        tmp[n] = '\0';
        if (!n) return bad("expected a number");
        char *endp = nullptr;
        v = strtod(tmp, &endp);
        if (*endp || !isfinite(v)) return bad("expected a number");
        return true;
    }
    bool boolean(bool &v) {
        ws();
        if (e - p >= 4 && !strncmp(p, "true", 4)) { p += 4; v = true; return true; }
        if (e - p >= 5 && !strncmp(p, "false", 5)) { p += 5; v = false; return true; }
        return bad("expected true or false");
    }
    bool skip(int depth = 0) {
        if (depth > 8) return bad("JSON nested too deeply");
        ws();
        if (p >= e) return bad("JSON syntax error");
        if (*p == '"') {
            bool o;
            return str(nullptr, 0, o);
        }
        if (*p == '{') {
            return object([&](const char *) { return skip(depth + 1); });
        }
        if (*p == '[') {
            return array([&]() { return skip(depth + 1); });
        }
        if (!strncmp(p, "true", 4) && e - p >= 4) { p += 4; return true; }
        if (!strncmp(p, "false", 5) && e - p >= 5) { p += 5; return true; }
        if (!strncmp(p, "null", 4) && e - p >= 4) { p += 4; return true; }
        double d;
        return number(d);
    }
    // handler(key) must consume the value (return false on error); unknown keys: call skip().
    template <class F> bool object(F handler) {
        if (!eat('{')) return bad("expected an object");
        if (eat('}')) return true;
        for (;;) {
            char key[24];
            bool over;
            if (!str(key, sizeof key, over)) return false;
            if (over) key[0] = '\0'; // over-long key: matches nothing
            if (!eat(':')) return bad("JSON syntax error");
            if (!handler(key)) return false;
            if (eat(',')) continue;
            if (eat('}')) return true;
            return bad("JSON syntax error");
        }
    }
    template <class F> bool array(F handler) {
        if (!eat('[')) return bad("expected an array");
        if (eat(']')) return true;
        for (;;) {
            if (!handler()) return false;
            if (eat(',')) continue;
            if (eat(']')) return true;
            return bad("JSON syntax error");
        }
    }
};

// Parses and validates. `out` is only modified on success.
inline bool parseJson(const char *json, size_t len, Config &out, char *why, size_t whyLen) {
    if (!json) return fail(why, whyLen, "no data");
    Config c;
    memset(&c, 0, sizeof c);
    c.auroraUk = true;
    c.wifiCount = 0;
    Reader r{json, json + len, why, whyLen};
    bool haveWifi = false, haveHost = false, havePw = false, haveLat = false, haveLon = false, haveTz = false;
    char nameTmp[96] = "";
    bool over = false;

    r.ws();
    if (r.p < r.e && r.p[0] == '\xEF') r.p += (r.e - r.p >= 3) ? 3 : 1; // UTF-8 BOM (Windows editors)
    const bool ok = r.object([&](const char *k) -> bool {
        if (!strcmp(k, "wifi")) {
            haveWifi = true;
            return r.array([&]() -> bool {
                if (c.wifiCount >= cfg::MAX_WIFI) return r.bad("at most 3 Wi-Fi networks");
                cfg::WifiNet &w = c.wifi[c.wifiCount];
                bool haveSsid = false;
                const bool good = r.object([&](const char *kk) -> bool {
                    if (!strcmp(kk, "ssid")) {
                        haveSsid = true;
                        if (!r.str(w.ssid, sizeof w.ssid, over)) return false;
                        return !over || r.bad("Wi-Fi name must be 1 to 32 bytes");
                    }
                    if (!strcmp(kk, "password")) {
                        if (!r.str(w.pass, sizeof w.pass, over)) return false;
                        return !over || r.bad("Wi-Fi password must be 8 to 63 chars (or empty)");
                    }
                    return r.skip();
                });
                if (!good) return false;
                if (!haveSsid) return r.bad("Wi-Fi entry without ssid");
                c.wifiCount++;
                return true;
            });
        }
        if (!strcmp(k, "powerwall")) {
            return r.object([&](const char *kk) -> bool {
                if (!strcmp(kk, "host")) {
                    haveHost = true;
                    if (!r.str(c.pwHost, sizeof c.pwHost, over)) return false;
                    return !over || r.bad("gateway address missing or invalid");
                }
                if (!strcmp(kk, "password")) {
                    havePw = true;
                    if (!r.str(c.pwPassword, sizeof c.pwPassword, over)) return false;
                    return !over || r.bad("gateway password must be 1 to 32 chars");
                }
                return r.skip();
            });
        }
        if (!strcmp(k, "location")) {
            return r.object([&](const char *kk) -> bool {
                double d;
                if (!strcmp(kk, "lat")) {
                    haveLat = true;
                    if (!r.number(d)) return false;
                    c.lat = static_cast<float>(d);
                    return true;
                }
                if (!strcmp(kk, "lon")) {
                    haveLon = true;
                    if (!r.number(d)) return false;
                    c.lon = static_cast<float>(d);
                    return true;
                }
                if (!strcmp(kk, "name")) {
                    if (!r.str(nameTmp, sizeof nameTmp, over)) return false;
                    return !over || r.bad("place name max 23 chars");
                }
                if (!strcmp(kk, "tz")) {
                    haveTz = true;
                    if (!r.str(c.tz, sizeof c.tz, over)) return false;
                    return !over || r.bad("time zone missing or too long");
                }
                return r.skip();
            });
        }
        if (!strcmp(k, "auroraUk")) return r.boolean(c.auroraUk);
        return r.skip();
    });
    if (!ok) return false;
    r.ws();
    if (r.p != r.e) return r.bad("unexpected text after the JSON");
    if (!haveWifi) return r.bad("wifi list missing");
    if (!haveHost) return r.bad("powerwall host missing");
    if (!havePw) return r.bad("powerwall password missing");
    if (!haveLat || !haveLon) return r.bad("location lat/lon missing");
    if (!haveTz) return r.bad("time zone missing or too long");
    sanitizeAscii(nameTmp);
    if (strlen(nameTmp) > 23) return r.bad("place name max 23 chars");
    strcpy(c.placeName, nameTmp);
    if (!validate(c, why, whyLen)) return false;
    out = c;
    return true;
}

} // namespace cfgv
