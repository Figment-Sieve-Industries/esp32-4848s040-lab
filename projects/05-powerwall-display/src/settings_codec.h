#pragma once
// Saved-settings record and screen-enable mask for project 05 (pure functions, no Arduino: host-tested in
// hosttest/).
//
// settings.bin on the SD card is 8 bytes: { uint32 magic; uint8 value; uint8 reserved; uint16 screenMask }.
//   "SET1" (oldest): value = raw 8-bit duty of the old linear slider; mask bytes unused  -> all screens on
//   "SET2":          value = slider percent 0..100; mask bytes unused                   -> all screens on
//   "SET3":          value = slider percent 0..100; screenMask = one bit per ui::ScreenIndex
// The record size never changed, so older files still read.
#include <stdint.h>
#include "backlight_curve.h"

namespace setcodec {

constexpr uint32_t SETTINGS_MAGIC_V3 = 0x53455433; // "SET3"

// Bit i of a screen mask = ui::ScreenIndex i (house, today, history, health, weather, moon, aurora, sun, enso,
// settings, about). Kept free of ui_common.h so the host test can include this file.
constexpr int SCREEN_COUNT = 11;
constexpr uint16_t ALL_SCREENS = (1u << SCREEN_COUNT) - 1;
constexpr uint16_t ALWAYS_ON = (1u << 0) | (1u << 1) | (1u << 9) | (1u << 10); // house, today, settings, about

// Drops unknown bits and forces the always-on screens on.
inline uint16_t normalizeMask(uint16_t mask) { return static_cast<uint16_t>((mask & ALL_SCREENS) | ALWAYS_ON); }

inline bool screenOn(uint16_t mask, int index) { return index >= 0 && index < SCREEN_COUNT && ((mask >> index) & 1); }

// Mask with screen `index` switched; an always-on or out-of-range screen is left as it is.
inline uint16_t withScreen(uint16_t mask, int index, bool on) {
    if (index < 0 || index >= SCREEN_COUNT) return normalizeMask(mask);
    const uint16_t bit = static_cast<uint16_t>(1u << index);
    return normalizeMask(on ? (mask | bit) : (mask & static_cast<uint16_t>(~bit)));
}

// Next enabled screen from `current` in direction +1 / -1, wrapping. `current` itself is returned only if it
// is the single enabled screen (the mask always has Settings and About on, so there are at least two).
inline int nextScreen(int current, int direction, uint16_t mask) {
    const int step = direction >= 0 ? 1 : SCREEN_COUNT - 1;
    int i = current;
    for (int n = 0; n < SCREEN_COUNT; n++) {
        i = (i + step) % SCREEN_COUNT;
        if (screenOn(mask, i)) return i;
    }
    return current;
}

struct Decoded {
    int brightnessPct = blcurve::PCT_DEFAULT;
    uint16_t screenMask = ALL_SCREENS;
};

// Decodes a saved record. False (out untouched) for an unknown magic or an out-of-range V2/V3 value.
inline bool decodeSettings(uint32_t magic, uint8_t value, uint16_t mask, int legacyMin, Decoded &out) {
    if (magic == SETTINGS_MAGIC_V3) {
        if (value > blcurve::PCT_MAX) return false;
        out.brightnessPct = value;
        out.screenMask = normalizeMask(mask);
        return true;
    }
    int pct = 0;
    if (!blcurve::percentFromSaved(magic, value, legacyMin, pct)) return false;
    out.brightnessPct = pct;
    out.screenMask = ALL_SCREENS; // SET1 / SET2 predate the toggles
    return true;
}

} // namespace setcodec
