#pragma once
// Host stand-in for the parts of Arduino.h that cache.cpp uses: Serial (output kept in a string for the
// checks, not printed) and a settable millis().
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

struct HostSerial {
    std::string out; // everything printed since the last clear()
    int printf(const char *fmt, ...) {
        char b[512];
        va_list ap;
        va_start(ap, fmt);
        const int n = vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        out += b;
        return n;
    }
    void println(const char *s) {
        out += s;
        out += '\n';
    }
    void clear() { out.clear(); }
};
inline HostSerial Serial;

inline uint32_t g_hostMillis = 0;
inline uint32_t millis() { return g_hostMillis; }
