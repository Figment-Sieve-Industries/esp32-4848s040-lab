#pragma once
// ArduinoJson allocator that puts documents in PSRAM, so JSON parsing doesn't fragment the
// internal RAM that Wi-Fi/TLS need. Use for every JsonDocument: JsonDocument doc(psramJson());
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

struct PsramJsonAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *ptr) override { heap_caps_free(ptr); }
    void *reallocate(void *ptr, size_t newSize) override {
        return heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
};

inline ArduinoJson::Allocator *psramJson() {
    static PsramJsonAllocator a;
    return &a;
}
