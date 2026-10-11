#pragma once
// Host stand-in for common/lib/SdStore: the four calls cache.cpp makes. test.cpp implements them on an
// in-memory card that behaves like SdStore (no card = false, writing into a missing directory fails,
// readFile returns at most cap bytes of a file).
#include <stddef.h>

namespace sdcard {
bool mounted();
bool mkdirs(const char *path);
bool writeFile(const char *path, const void *data, size_t len);
bool readFile(const char *path, void *buf, size_t cap, size_t *outLen);
} // namespace sdcard
