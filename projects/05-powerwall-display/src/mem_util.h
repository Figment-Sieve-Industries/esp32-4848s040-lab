#pragma once
// Memory helpers: explicit PSRAM / internal allocations, an RAII heap buffer, a bounded string copy and
// the ArduinoJson PSRAM allocator. Internal RAM is reserved for Wi-Fi/TLS/LVGL draw buffers: put
// bodies, JSON documents and images in PSRAM.
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline void *psramAlloc(size_t n, bool zero = false) {
    return zero ? heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                : heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
inline void *internalAlloc(size_t n, bool zero = false) {
    return zero ? heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// Owns one heap_caps allocation; freed when it goes out of scope.
struct HeapBuf {
    void *p = nullptr;
    size_t size = 0;
    bool psram(size_t n, bool zero = false) { return take(psramAlloc(n, zero), n); }
    bool internal(size_t n, bool zero = false) { return take(internalAlloc(n, zero), n); }
    template <class T> T *as() const { return static_cast<T *>(p); }
    void reset() {
        heap_caps_free(p);
        p = nullptr;
        size = 0;
    }
    HeapBuf() = default;
    ~HeapBuf() { heap_caps_free(p); }
    HeapBuf(const HeapBuf &) = delete;
    HeapBuf &operator=(const HeapBuf &) = delete;

private:
    bool take(void *q, size_t n) {
        reset();
        p = q;
        size = q ? n : 0;
        return q != nullptr;
    }
};

// 64-byte aligned PSRAM buffer for an LVGL RGB565 canvas/image; stride = bytes per row.
inline uint8_t *allocCanvas(int w, int h, size_t &stride) {
    stride = (size_t)w * 2;
    return static_cast<uint8_t *>(heap_caps_aligned_alloc(64, stride * (size_t)h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

// Copies at most n chars of src into dst (capacity cap incl. the NUL); always NUL-terminates.
inline void copyStr(char *dst, size_t cap, const char *src, size_t n = SIZE_MAX) {
    if (!cap) return;
    size_t len = src ? strnlen(src, n < cap - 1 ? n : cap - 1) : 0;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

// ArduinoJson allocator that keeps documents in PSRAM: JsonDocument doc(psramJson());
struct PsramJsonAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override { return psramAlloc(size); }
    void deallocate(void *ptr) override { heap_caps_free(ptr); }
    void *reallocate(void *ptr, size_t newSize) override {
        return heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
};

inline ArduinoJson::Allocator *psramJson() {
    static PsramJsonAllocator a;
    return &a;
}
