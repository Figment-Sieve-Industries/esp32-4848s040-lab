#pragma once
// Pure helpers for the setup portal (setup_portal.cpp), host-tested in hosttest/test_setup.inc: the soft-AP name,
// the Wi-Fi QR payload, URL encoding for the geocoder query, host name validation and a bounded request-body buffer.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace setuputil {

// "PW-Display-XXXX" from the last two MAC bytes (upper-case hex). out needs 16 bytes.
inline void apSsid(const uint8_t mac[6], char out[16]) {
    static const char HEXDIG[] = "0123456789ABCDEF";
    memcpy(out, "PW-Display-", 11);
    out[11] = HEXDIG[mac[4] >> 4];
    out[12] = HEXDIG[mac[4] & 15];
    out[13] = HEXDIG[mac[5] >> 4];
    out[14] = HEXDIG[mac[5] & 15];
    out[15] = '\0';
}

// Appends src to out (cap incl. NUL) at *len, escaping \ ; , : " with a backslash (the Wi-Fi QR "MECARD-like"
// syntax). False if it does not fit (out is then left NUL-terminated at the old length).
inline bool appendQrEscaped(char *out, size_t cap, size_t *len, const char *src) {
    size_t n = *len;
    for (const char *p = src; *p; p++) {
        const bool esc = *p == '\\' || *p == ';' || *p == ',' || *p == ':' || *p == '"';
        if (n + (esc ? 2 : 1) >= cap) {
            out[*len] = '\0';
            return false;
        }
        if (esc) out[n++] = '\\';
        out[n++] = *p;
    }
    out[n] = '\0';
    *len = n;
    return true;
}

// "WIFI:T:nopass;S:<ssid>;;" for an open network (what phone cameras read to join a Wi-Fi). Returns the length,
// 0 if it does not fit.
inline size_t wifiQrOpen(const char *ssid, char *out, size_t cap) {
    static const char HEAD[] = "WIFI:T:nopass;S:";
    const size_t head = sizeof(HEAD) - 1;
    if (!out || cap < head + 3) return 0;
    memcpy(out, HEAD, head + 1);
    size_t len = head;
    if (!appendQrEscaped(out, cap, &len, ssid)) return 0;
    if (len + 3 > cap) return 0;
    memcpy(out + len, ";;", 3);
    return len + 2;
}

// Percent-encodes everything except unreserved characters (RFC 3986: A-Z a-z 0-9 - . _ ~). Returns the length,
// 0 if it does not fit (or src is empty).
inline size_t urlEncode(const char *src, char *out, size_t cap) {
    static const char HEXDIG[] = "0123456789ABCDEF";
    size_t n = 0;
    if (!out || !cap) return 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src); *p; p++) {
        const unsigned char c = *p;
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '.' || c == '_' || c == '~';
        if (n + (plain ? 1 : 3) >= cap) {
            out[0] = '\0';
            return 0;
        }
        if (plain) {
            out[n++] = static_cast<char>(c);
        } else {
            out[n++] = '%';
            out[n++] = HEXDIG[c >> 4];
            out[n++] = HEXDIG[c & 15];
        }
    }
    out[n] = '\0';
    return n;
}

// A gateway address the portal accepts: an IPv4 address or a DNS name (letters, digits, '-' and '.'), 1..63
// chars, no leading/trailing dot or dash. Anything else could add a path or port to the URL built from it.
inline bool validHost(const char *h) {
    const size_t n = h ? strnlen(h, 64) : 0;
    if (n == 0 || n > 63) return false;
    if (h[0] == '.' || h[0] == '-' || h[n - 1] == '.' || h[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++) {
        const char c = h[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok) return false;
        if (c == '.' && h[i + 1] == '.') return false;
    }
    return true;
}

// Printable ASCII only (POSIX TZ strings, place names): 0x20..0x7E, length 1..maxLen.
inline bool printableAscii(const char *s, size_t maxLen) {
    const size_t n = s ? strnlen(s, maxLen + 1) : 0;
    if (n == 0 || n > maxLen) return false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    }
    return true;
}

// Copies a UTF-8 place name to ASCII for the Montserrat fonts: common Latin-1 letters lose their accents, other
// multi-byte characters become '?'. Always NUL-terminates (cap incl. NUL).
inline void asciiFold(const char *src, char *out, size_t cap) {
    if (!out || !cap) return;
    size_t n = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(src ? src : "");
    // Latin-1 U+00C0..U+00FF -> base letter ('?' where there is none)
    static const char FOLD[] = "AAAAAAACEEEEIIIIDNOOOOO?OUUUUYTsaaaaaaaceeeeiiiidnooooo?ouuuuyty";
    while (*p && n + 1 < cap) {
        const unsigned char c = *p;
        if (c < 0x80) {
            out[n++] = (c < 0x20 || c == 0x7F) ? ' ' : static_cast<char>(c);
            p++;
        } else if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            out[n++] = FOLD[p[1] - 0x80];
            p += 2;
        } else {
            out[n++] = '?';
            p++;
            while ((*p & 0xC0) == 0x80) p++; // skip continuation bytes
        }
    }
    out[n] = '\0';
}

// Request body collected in chunks (the web server's raw-upload callback) into a fixed buffer. Anything past the
// capacity is dropped and flagged, so a large body can never allocate more than N bytes.
template <size_t N> struct BodyBuf {
    char buf[N + 1];
    size_t len = 0;
    bool overflow = false;
    void reset() {
        len = 0;
        overflow = false;
        buf[0] = '\0';
    }
    void add(const uint8_t *data, size_t n) {
        if (overflow) return;
        if (n > N - len) {
            overflow = true;
            return;
        }
        memcpy(buf + len, data, n);
        len += n;
        buf[len] = '\0';
    }
};

} // namespace setuputil
