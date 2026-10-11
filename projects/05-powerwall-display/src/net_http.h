#pragma once
// One HTTP(S) client for every background fetch (weather, sky, space, ENSO); implemented in
// net_http.cpp. Runs only on the network task (one TLS session at a time). The Powerwall gateway keeps
// its own persistent HTTPClient in powerwall.cpp and uses readBody() for its bodies.
#include <HTTPClient.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace net {

constexpr const char *USER_AGENT = "powerwall-display/1.0 (personal ESP32 display; Figment Sieve Industries)";

struct Options {
    const char *tag = "Net";        // log prefix: "Weather", "Sky", "Space", "ENSO"
    const char *ca = nullptr;       // PEM root(s); nullptr = plain http:// only
    bool keepAlive = false;         // false: HTTP/1.0 + close per request (no chunked bodies); true: HTTP/1.1 reuse
    uint32_t connectMs = 3000, readMs = 5000, handshakeS = 5;
    uint32_t bodyDeadlineMs = 8000; // whole body, per request
    uint32_t budgetMs = 0;          // whole session; 0 = none. Once spent, get()/stream() return Skipped
    bool retryOnReuseError = true;  // reconnect once if a reused keep-alive connection fails
};

enum class Result : int8_t { Ok, Http /*non-2xx*/, Net /*transport*/, TooBig, Incomplete, Skipped };
const char *resultName(Result r);

struct Request {
    const char *range = nullptr; // e.g. "bytes=-300"; 200 and 206 are both accepted
    bool lastModified = false;   // parse the Last-Modified header into Response::lastModified
};
struct Response {
    int code = 0;            // HTTP status, or HTTPClient's negative error
    size_t len = 0;          // body bytes delivered
    int contentLength = -1;  // -1 = unknown (chunked / until close)
    time_t lastModified = 0; // UTC, when requested and present
};

// Receives body bytes in order; return false to abort (-> TooBig).
using Sink = bool (*)(void *ctx, const uint8_t *data, size_t n);

class Session {
public:
    explicit Session(const Options &o);
    ~Session(); // ends the request and stops the client: frees the TLS buffers
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // Body into buf (NUL-terminated; cap includes the NUL). Accepts 200/206. Reads by Content-Length,
    // chunked (de-chunked here, never by HTTPClient) or until close; yields while waiting and obeys
    // bodyDeadlineMs. A body larger than cap-1 -> TooBig. Errors are logged as
    // "<tag>: <path> -> <HTTP n | error>" / "<tag>: <path> body incomplete (n of m B)".
    Result get(const char *url, char *buf, size_t cap, Response *r = nullptr, const Request &q = {});
    // Same, but streams the body through sink (at most maxBytes, else TooBig).
    Result stream(const char *url, Sink sink, void *ctx, size_t maxBytes, Response *r = nullptr, const Request &q = {});
    // False after a transport error or an exhausted budget: callers skip their remaining requests.
    bool usable() const;
    void close(); // stop the connection now (e.g. free TLS before a JPEG decode)

private:
    struct Impl;
    Impl *d_;
};

// Body reader for an HTTPClient already past GET()/POST() (the gateway's persistent connection):
// Content-Length, chunked or until close, with a deadline. buf NUL-terminated; len = bytes read.
Result readBody(HTTPClient &http, char *buf, size_t cap, size_t &len, uint32_t deadlineMs);

// Counters for the heartbeat.
struct Stats {
    uint32_t tlsConnects = 0, requests = 0, failures = 0;
};
Stats stats();
void noteTlsConnect(); // for TLS connections made outside Session (the gateway)

} // namespace net
