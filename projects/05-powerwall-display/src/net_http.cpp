/// One HTTP(S) client for the background fetches - see net_http.h.
// HTTPClient does the connection, request and headers; the body is read by net_body.h (Content-Length,
// chunked or until close) in a loop that yields while waiting and has a hard deadline. HTTPClient's own
// chunk reader is never used: on a kept-alive TLS connection it once stalled in a non-yielding loop.
#include "net_http.h"
#include "net_body.h"
#include "time_util.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <memory>

namespace net {

namespace {

Stats s_stats; // written by the network task only; read (racy but word-sized) by the heartbeat

constexpr uint32_t MIN_PHASE_MS = 1000; // a budget never squeezes a connect/read/body below this

struct ArduinoClock {
    uint32_t now() { return millis(); }
    void wait() { delay(2); }
};

Result toResult(BodyStatus b) {
    switch (b) {
    case BodyStatus::Ok: return Result::Ok;
    case BodyStatus::TooBig: return Result::TooBig;
    default: return Result::Incomplete;
    }
}

// Reads the body of the response HTTPClient has just parsed the headers of
Result readInto(HTTPClient &http, BodyTarget &t, uint32_t deadlineMs, bool &clean) {
    ArduinoClock clock;
    const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    return toResult(readBodyFrom(http.getStreamPtr(), chunked, http.getSize(), t, deadlineMs, clock, clean));
}

// "https://host/path?query" -> "/path" (for log lines)
void pathOf(const char *url, char *out, size_t n) {
    const char *p = strstr(url, "://");
    p = p ? strchr(p + 3, '/') : nullptr;
    if (!p) p = "/";
    size_t len = strcspn(p, "?");
    if (len > n - 1) len = n - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

uint32_t clampMs(uint32_t v, uint32_t rem) {
    const uint32_t r = rem < MIN_PHASE_MS ? MIN_PHASE_MS : rem;
    return v < r ? v : r;
}

} // namespace

const char *resultName(Result r) {
    switch (r) {
    case Result::Ok: return "ok";
    case Result::Http: return "http error";
    case Result::Net: return "network error";
    case Result::TooBig: return "too big";
    case Result::Incomplete: return "incomplete";
    case Result::Skipped: return "skipped";
    }
    return "?";
}

struct Session::Impl {
    Options o;
    std::unique_ptr<NetworkClient> client;
    HTTPClient http;
    uint32_t startMs;
    bool usable = true;
    mutable bool budgetLogged = false;

    explicit Impl(const Options &opt) : o(opt), startMs(millis()) {
        if (o.ca) {
            auto *tls = new NetworkClientSecure();
            tls->setCACert(o.ca);
            client.reset(tls);
        } else {
            client.reset(new NetworkClient());
        }
        const char *keys[] = {"Transfer-Encoding", "Last-Modified"}; // copied by HTTPClient
        http.collectHeaders(keys, 2);
        http.setUserAgent(USER_AGENT);
        http.useHTTP10(!o.keepAlive); // also sets reuse = keepAlive
        http.setReuse(o.keepAlive);
    }

    // Remaining session budget in ms (UINT32_MAX = no budget)
    uint32_t remaining() const {
        if (!o.budgetMs) return UINT32_MAX;
        const uint32_t used = millis() - startMs;
        return used >= o.budgetMs ? 0 : o.budgetMs - used;
    }

    // False once the budget is spent (logged once per session)
    bool budgetOk() const {
        if (remaining() > 0) return true;
        if (!budgetLogged) {
            Serial.printf("%s: time budget (%lu ms) spent, skipping the rest\n", o.tag, (unsigned long)o.budgetMs);
            budgetLogged = true;
        }
        return false;
    }

    void stop() {
        http.end();
        client->stop();
    }

    Result run(const char *url, BodyTarget &t, Response *resp, const Request &q);
};

Result Session::Impl::run(const char *url, BodyTarget &t, Response *resp, const Request &q) {
    Response r;
    char path[96];
    pathOf(url, path, sizeof(path));
    if (!usable || !budgetOk()) return Result::Skipped;
    s_stats.requests++;
    int code = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        const uint32_t rem = remaining();
        const bool reused = client->connected();
        if (!reused && o.ca) {
            static_cast<NetworkClientSecure *>(client.get())
                ->setHandshakeTimeout(rem == UINT32_MAX ? o.handshakeS : clampMs(o.handshakeS * 1000, rem) / 1000);
            s_stats.tlsConnects++;
        }
        if (!http.begin(*client, url)) {
            Serial.printf("%s: %s -> bad URL\n", o.tag, path);
            s_stats.failures++;
            return Result::Net;
        }
        http.useHTTP10(!o.keepAlive);
        http.setReuse(o.keepAlive);
        http.setConnectTimeout((int32_t)clampMs(o.connectMs, rem));
        const uint32_t readMs = clampMs(o.readMs, rem);
        http.setTimeout((uint16_t)(readMs > 65535 ? 65535 : readMs));
        if (q.range) http.addHeader("Range", q.range);
        code = http.GET();
        if (code < 0 && reused && o.retryOnReuseError && attempt == 0) {
            // usually the server closed our idle keep-alive connection: reconnect once
            stop();
            continue;
        }
        break;
    }
    r.code = code;
    if (code < 0) {
        Serial.printf("%s: %s -> %s\n", o.tag, path, HTTPClient::errorToString(code).c_str());
        stop();
        usable = false;
        s_stats.failures++;
        if (resp) *resp = r;
        return Result::Net;
    }
    if (code != HTTP_CODE_OK && code != HTTP_CODE_PARTIAL_CONTENT) {
        Serial.printf("%s: %s -> HTTP %d\n", o.tag, path, code);
        stop(); // an unread body must never be taken for the next response
        s_stats.failures++;
        if (resp) *resp = r;
        return Result::Http;
    }
    r.contentLength = http.getSize();
    if (q.lastModified) r.lastModified = tu::parseHttpDate(http.header("Last-Modified").c_str());
    bool clean = false;
    const Result res = readInto(http, t, clampMs(o.bodyDeadlineMs, remaining()), clean);
    r.len = t.len;
    if (t.buf) t.buf[t.len] = '\0';
    if (res == Result::TooBig) {
        Serial.printf("%s: %s too big (over %u B)\n", o.tag, path, (unsigned)t.max);
    } else if (res != Result::Ok) {
        Serial.printf("%s: %s body incomplete (%u of %d B)\n", o.tag, path, (unsigned)t.len, r.contentLength);
    }
    if (res == Result::Ok && clean && o.keepAlive) http.end(); // keeps the connection if the server allows
    else stop();
    if (res != Result::Ok) s_stats.failures++;
    if (resp) *resp = r;
    return res;
}

Session::Session(const Options &o) : d_(new Impl(o)) {}

Session::~Session() {
    d_->stop();
    delete d_;
}

Result Session::get(const char *url, char *buf, size_t cap, Response *r, const Request &q) {
    if (!buf || cap == 0) return Result::TooBig;
    buf[0] = '\0';
    BodyTarget t;
    t.buf = buf;
    t.max = cap - 1;
    return d_->run(url, t, r, q);
}

Result Session::stream(const char *url, Sink sink, void *ctx, size_t maxBytes, Response *r, const Request &q) {
    BodyTarget t;
    t.sink = sink;
    t.ctx = ctx;
    t.max = maxBytes;
    return d_->run(url, t, r, q);
}

bool Session::usable() const { return d_->usable && d_->budgetOk(); }

void Session::close() { d_->stop(); }

Result readBody(HTTPClient &http, char *buf, size_t cap, size_t &len, uint32_t deadlineMs) {
    len = 0;
    if (!buf || cap == 0) return Result::TooBig;
    BodyTarget t;
    t.buf = buf;
    t.max = cap - 1;
    bool clean = false;
    const Result res = readInto(http, t, deadlineMs, clean);
    len = t.len;
    buf[len] = '\0';
    if (!clean) {
        NetworkClient *s = http.getStreamPtr();
        if (s) s->stop(); // unknown position in the stream: never reuse this connection
    }
    if (res != Result::Ok) s_stats.failures++;
    s_stats.requests++;
    return res;
}

Stats stats() { return s_stats; }

void noteTlsConnect() { s_stats.tlsConnects++; }

} // namespace net
