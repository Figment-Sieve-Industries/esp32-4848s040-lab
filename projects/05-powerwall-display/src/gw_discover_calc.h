#pragma once
// Pure helpers for gateway discovery (no Arduino dependencies, host-tested): IPv4 text/number conversion,
// the private-range check, /24 enumeration and the response scoring. Addresses are host-order uint32_t.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace gwdisc {

// Dotted IPv4 -> host-order value. Strict: four decimal parts 0..255, no leading/trailing junk.
inline bool parseIPv4(const char *s, uint32_t &out) {
    if (!s) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return false;
        uint32_t n = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (uint32_t)(*s - '0');
            if (++digits > 3 || n > 255) return false;
            s++;
        }
        v = (v << 8) | n;
        if (i < 3) {
            if (*s != '.') return false;
            s++;
        }
    }
    if (*s != '\0') return false;
    out = v;
    return true;
}

inline void formatIPv4(uint32_t ip, char *buf, size_t cap) {
    snprintf(buf, cap, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 255), (unsigned)((ip >> 8) & 255),
             (unsigned)(ip & 255));
}

// RFC 1918: 10/8, 172.16/12, 192.168/16.
inline bool isPrivate(uint32_t ip) {
    return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 /*172.16/12*/ || (ip >> 16) == 0xC0A8;
}

// Candidate hosts of the /24 containing `self`: .1 to .254 without `self`. Writes up to cap addresses, returns the
// count (253 normally). 0 when `self` is not a private address.
inline int enumerate24(uint32_t self, uint32_t *out, int cap) {
    if (!isPrivate(self)) return 0;
    const uint32_t base = self & 0xFFFFFF00u;
    int n = 0;
    for (uint32_t h = 1; h <= 254 && n < cap; h++) {
        if ((base | h) == self) continue;
        out[n++] = base | h;
    }
    return n;
}

// ---- Response scoring ------------------------------------------------------------------------------------------
// Unauthenticated GETs of one HTTPS host (never a login). What a Powerwall 3 returns is only partly verified
// (ADR-38), so several signals are combined:
//   strong (3): GET /api/status is 200 JSON with "din" and a Tesla gateway marker ("device_type", "up_time_seconds",
//               "git_hash" or "commission_count") - the long-standing unauthenticated status document of Tesla
//               Energy Gateways.
//   weak   (1): GET /api/status answers 401/403 with a JSON body (auth required, not a generic server's 404/HTML).
//   weak   (1): GET /api/system_status/soe answers 401/403 with a JSON body (an endpoint this gateway has and
//               a generic server has not).
//   weak   (1): GET / answers 2xx/3xx and the body mentions "tesla", "powerwall" or "gateway".
// Accepted at ACCEPT_SCORE: the strong signal alone, or any two weak ones.
struct Probe {
    int code = 0;          // HTTP status, <= 0 when the request failed or was not made
    const char *body = ""; // NUL-terminated, may be truncated
};

inline bool containsI(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return false;
    const size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i]) {
            char a = hay[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (i == n) return true;
    }
    return false;
}

inline bool looksJson(const char *b) {
    while (b && (*b == ' ' || *b == '\r' || *b == '\n' || *b == '\t')) b++;
    return b && *b == '{';
}

inline bool isStatusDoc(const Probe &p) {
    if (p.code != 200 || !looksJson(p.body) || !strstr(p.body, "\"din\"")) return false;
    return strstr(p.body, "\"device_type\"") || strstr(p.body, "\"up_time_seconds\"") || strstr(p.body, "\"git_hash\"") ||
           strstr(p.body, "\"commission_count\"");
}

inline bool isAuthJson(const Probe &p) { return (p.code == 401 || p.code == 403) && looksJson(p.body); }

inline bool mentionsTesla(const Probe &p) {
    if (p.code < 200 || p.code >= 400) return false;
    return containsI(p.body, "tesla") || containsI(p.body, "powerwall") || containsI(p.body, "gateway");
}

inline int score(const Probe &status, const Probe &soe, const Probe &root) {
    int s = 0;
    if (isStatusDoc(status)) s += 3;
    else if (isAuthJson(status)) s += 1;
    if (isAuthJson(soe)) s += 1;
    // Verified on unit 1's Powerwall 3 (firmware 26.26.4 basic LAN API, 2026-10-12): /api/status 404,
    // /api/system_status/soe 403 with a JSON body, / 404. "status 404 + soe auth-JSON" is that gateway's pattern:
    // a generic HTTPS server doesn't answer a Tesla API path with an auth error, so it counts as a second signal.
    if (status.code == 404 && isAuthJson(soe)) s += 1;
    if (mentionsTesla(root)) s += 1;
    return s;
}

constexpr int ACCEPT_SCORE = 2;

} // namespace gwdisc
