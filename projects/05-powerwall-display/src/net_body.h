#pragma once
// HTTP body framing for net_http.cpp, in plain C++ with no Arduino types so it is host-tested
// (hosttest/) against a fake socket and clock:
// - Dechunker: incremental decoder for "Transfer-Encoding: chunked". Feed it raw socket bytes in any
//   split; it hands the payload bytes to a callback and stops at the terminating zero-size chunk.
//     chunk = hex-size [ ";" ext | " " ... ] CRLF data CRLF     (a bare LF is accepted for CRLF)
//     last  = "0" [ext] CRLF *(trailer-line CRLF) CRLF
// - readBodyFrom(): the read loop (Content-Length, chunked or until close) with a hard deadline, which
//   waits through the clock (a yield on the device) whenever no bytes are available.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace net {

class Dechunker {
public:
    // maxChunk: a chunk-size above this is rejected (tooBig()), e.g. the caller's buffer capacity.
    explicit Dechunker(size_t maxChunk = SIZE_MAX) : max_(maxChunk) {}

    // Consumes raw bytes. out(const uint8_t *p, size_t n) -> bool receives payload bytes; returning
    // false aborts (aborted()). Returns the bytes consumed: less than n only once done() or failed().
    template <class F> size_t feed(const uint8_t *in, size_t n, F &&out) {
        size_t i = 0;
        while (i < n && st_ != St::Done && st_ != St::Error) {
            if (st_ == St::Data) {
                size_t k = n - i < left_ ? n - i : left_;
                if (!out(in + i, k)) {
                    aborted_ = true;
                    st_ = St::Error;
                    return i;
                }
                i += k;
                left_ -= k;
                if (left_ == 0) st_ = St::DataCr;
                continue;
            }
            step((char)in[i++]);
        }
        return i;
    }

    bool done() const { return st_ == St::Done; }       // the whole body (incl. trailer) was read
    bool lastChunk() const { return last_; }             // the zero-size chunk was seen (payload complete)
    bool failed() const { return st_ == St::Error; }     // bad framing, too big or aborted
    bool tooBig() const { return tooBig_; }
    bool aborted() const { return aborted_; }

private:
    enum class St : uint8_t { Size, Ext, SizeLf, Data, DataCr, DataLf, Trailer, TrailerLine, TrailerLf, Done, Error };
    static constexpr int MAX_LINE = 64;     // chunk-size line incl. extensions
    static constexpr int MAX_TRAILER = 1024;

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    void fail() { st_ = St::Error; }

    void endSizeLine() {
        if (digits_ == 0) return fail();
        if (size_ == 0) {
            last_ = true;
            st_ = St::Trailer;
        } else if (size_ > max_) {
            tooBig_ = true;
            fail();
        } else {
            left_ = size_;
            st_ = St::Data;
        }
    }

    void step(char c) {
        switch (st_) {
        case St::Size: {
            const int v = hexVal(c);
            if (v >= 0) {
                if (++digits_ > 8) return fail(); // > 4 GB: nonsense
                size_ = size_ * 16 + (size_t)v;
            } else if (c == ';' || c == ' ' || c == '\t') {
                st_ = St::Ext;
            } else if (c == '\r') {
                st_ = St::SizeLf;
            } else if (c == '\n') {
                endSizeLine();
            } else {
                fail();
            }
            if (++line_ > MAX_LINE) fail();
            break;
        }
        case St::Ext:
            if (c == '\r') st_ = St::SizeLf;
            else if (c == '\n') endSizeLine();
            if (++line_ > MAX_LINE) fail();
            break;
        case St::SizeLf:
            if (c == '\n') endSizeLine();
            else fail();
            break;
        case St::DataCr:
            if (c == '\r') st_ = St::DataLf;
            else if (c == '\n') nextChunk();
            else fail();
            break;
        case St::DataLf:
            if (c == '\n') nextChunk();
            else fail();
            break;
        case St::Trailer: // at the start of a trailer line; an empty line ends the body
            if (c == '\r') st_ = St::TrailerLf;
            else if (c == '\n') st_ = St::Done;
            else st_ = St::TrailerLine;
            if (++trailer_ > MAX_TRAILER) fail();
            break;
        case St::TrailerLine:
            if (c == '\n') st_ = St::Trailer;
            if (++trailer_ > MAX_TRAILER) fail();
            break;
        case St::TrailerLf:
            if (c == '\n') st_ = St::Done;
            else fail();
            break;
        default:
            break;
        }
    }

    void nextChunk() {
        st_ = St::Size;
        size_ = 0;
        digits_ = 0;
        line_ = 0;
    }

    St st_ = St::Size;
    size_t max_;
    size_t size_ = 0, left_ = 0;
    int digits_ = 0, line_ = 0, trailer_ = 0;
    bool last_ = false, tooBig_ = false, aborted_ = false;
};

enum class BodyStatus : uint8_t { Ok, TooBig, Incomplete };

// Where body bytes go: a buffer (buf, filled up to max bytes; the caller adds the NUL), or a sink.
struct BodyTarget {
    char *buf = nullptr;
    bool (*sink)(void *ctx, const uint8_t *data, size_t n) = nullptr; // false = refuse (TooBig)
    void *ctx = nullptr;
    size_t max = 0; // payload limit
    size_t len = 0;

    bool put(const uint8_t *p, size_t n) {
        if (n > max - len) return false;
        if (buf) memcpy(buf + len, p, n);
        else if (!sink(ctx, p, n)) return false;
        len += n;
        return true;
    }
};

// Reads one body from s into t.
//   S:     int available(); int read(uint8_t *, size_t); bool connected()   (s may be nullptr: closed, empty)
//   Clock: uint32_t now() (ms); void wait() (yield ~2 ms)
// contentLength < 0 = unknown: chunked, or until the server closes. clean = the stream stopped exactly at
// the end of this message, so a keep-alive connection can be reused.
template <class S, class Clock>
BodyStatus readBodyFrom(S *s, bool chunked, long contentLength, BodyTarget &t, uint32_t deadlineMs, Clock &clock,
                    bool &clean) {
    constexpr size_t CHUNK = 512; // stack buffer for chunked or streamed bodies
    clean = false;
    if (chunked) contentLength = -1; // Transfer-Encoding wins over Content-Length (RFC 9112 6.3)
    if (contentLength >= 0 && (size_t)contentLength > t.max) return BodyStatus::TooBig;
    if (!s) return (contentLength == 0 || (contentLength < 0 && !chunked)) ? BodyStatus::Ok : BodyStatus::Incomplete;
    const uint32_t t0 = clock.now();
    Dechunker dc(t.max);
    uint8_t tmp[CHUNK];
    for (;;) {
        if (chunked ? dc.done() : (contentLength >= 0 && t.len >= (size_t)contentLength)) {
            clean = true;
            return BodyStatus::Ok;
        }
        if (clock.now() - t0 >= deadlineMs) break;
        const int avail = s->available();
        if (avail <= 0) {
            if (!s->connected()) break;
            clock.wait(); // never spin: the network task shares core 0 with the idle task
            continue;
        }
        size_t want = (size_t)avail;
        if (contentLength >= 0 && want > (size_t)contentLength - t.len) want = (size_t)contentLength - t.len;
        if (!chunked && t.buf) { // plain body into a buffer: read in place
            if (t.len >= t.max) return BodyStatus::TooBig;
            if (want > t.max - t.len) want = t.max - t.len;
            const int r = s->read(reinterpret_cast<uint8_t *>(t.buf) + t.len, want);
            if (r > 0) t.len += (size_t)r;
            else clock.wait();
            continue;
        }
        if (want > CHUNK) want = CHUNK;
        const int r = s->read(tmp, want);
        if (r <= 0) {
            clock.wait();
            continue;
        }
        if (!chunked) {
            if (!t.put(tmp, (size_t)r)) return BodyStatus::TooBig;
            continue;
        }
        bool refused = false;
        dc.feed(tmp, (size_t)r, [&t, &refused](const uint8_t *p, size_t n) {
            refused = !t.put(p, n);
            return !refused;
        });
        if (dc.failed()) return (dc.tooBig() || refused) ? BodyStatus::TooBig : BodyStatus::Incomplete;
    }
    // deadline, or the connection closed
    if (chunked) return dc.lastChunk() ? BodyStatus::Ok : BodyStatus::Incomplete; // only the final CRLF missing: fine
    if (contentLength >= 0) return t.len == (size_t)contentLength ? BodyStatus::Ok : BodyStatus::Incomplete;
    return s->connected() ? BodyStatus::Incomplete : BodyStatus::Ok; // until close: complete once closed
}

} // namespace net
