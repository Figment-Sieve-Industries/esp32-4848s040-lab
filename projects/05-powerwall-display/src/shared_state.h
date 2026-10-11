#pragma once
// Cross-task state helpers: a FreeRTOS mutex with RAII locking, and Shared<T> - a value copied in and
// out under its own mutex. Namespace-scope instances are created at static init, so they are usable
// before any task starts (the SD task reads powerwallData() before powerwallStart()).
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Mutex {
public:
    Mutex() : h_(xSemaphoreCreateMutex()) {}
    void lock() { xSemaphoreTake(h_, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(h_); }
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;

private:
    SemaphoreHandle_t h_;
};

class LockGuard {
public:
    explicit LockGuard(Mutex &m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    Mutex &m_;
};

template <class T> class Shared {
public:
    T get() const {
        LockGuard g(m_);
        return v_;
    }
    void set(const T &v) {
        LockGuard g(m_);
        v_ = v;
    }
    // Read-modify-write under the lock. f must not call back into this object.
    template <class F> auto with(F f) {
        LockGuard g(m_);
        return f(v_);
    }

private:
    mutable Mutex m_;
    T v_{};
};

// True when data is missing or older than maxAgeMs (millis() based, wrap-safe).
inline bool isStale(bool valid, uint32_t atMs, uint32_t maxAgeMs) { return !valid || millis() - atMs > maxAgeMs; }
