#pragma once
// Host stand-in for the one ArduinoJson type mem_util.h uses.
#include <stddef.h>

namespace ArduinoJson {
struct Allocator {
    virtual void *allocate(size_t size) = 0;
    virtual void deallocate(void *ptr) = 0;
    virtual void *reallocate(void *ptr, size_t newSize) = 0;
    virtual ~Allocator() = default;
};
} // namespace ArduinoJson
