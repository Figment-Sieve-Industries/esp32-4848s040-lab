#pragma once
// Brightness slider curve for project 05 (pure functions, no Arduino: host-tested in hosttest/).
//
// The slider position is a percentage 0..100 and is what gets saved. It maps through a gamma curve onto
// the usable duty range [minDuty .. maxDuty] of the backlight PWM:
//
//     duty = minDuty + round((maxDuty - minDuty) * (pct / 100) ^ GAMMA)
//
// Equal slider steps then look like roughly equal brightness steps (perceived brightness is close to
// logarithmic in light output). The usable minimum is the lowest duty at which the boost driver stays on
// (board4848::backlightMinDuty(), derived from B4848_LCD_BL_MIN at the active PWM resolution), so the
// slider never switches the panel off. 0 % is "dimmest that is still lit", not "off"; turning the
// backlight fully off is not offered on the slider (serial `b 0` still does it).
//
// The first few positions can share one duty when the range is coarse (9 bits at 100 kHz leaves about 310
// steps; the lowest ~5 positions round to minDuty). More resolution at a lower PWM frequency fixes that.
#include <math.h>
#include <stdint.h>

namespace blcurve {

constexpr float GAMMA = 2.2f;
constexpr int PCT_MIN = 0;
constexpr int PCT_MAX = 100;
constexpr int PCT_DEFAULT = 100;

inline int clampPct(int pct) { return pct < PCT_MIN ? PCT_MIN : (pct > PCT_MAX ? PCT_MAX : pct); }

// Slider percentage (clamped to 0..100) -> raw duty in [minDuty .. maxDuty]. 0 -> minDuty, 100 -> maxDuty.
// Monotonic non-decreasing. If maxDuty <= minDuty the range is empty and minDuty is returned.
inline uint32_t dutyForPercent(int pct, uint32_t minDuty, uint32_t maxDuty) {
    pct = clampPct(pct);
    if (maxDuty <= minDuty || pct == PCT_MIN) return minDuty;
    if (pct == PCT_MAX) return maxDuty;
    const float frac = powf(static_cast<float>(pct) / 100.0f, GAMMA);
    const uint32_t d = minDuty + static_cast<uint32_t>(lroundf(frac * static_cast<float>(maxDuty - minDuty)));
    return d > maxDuty ? maxDuty : d;
}

// Inverse: the slider percentage whose duty is closest to `duty` (clamped into [minDuty .. maxDuty]).
inline int percentForDuty(uint32_t duty, uint32_t minDuty, uint32_t maxDuty) {
    if (maxDuty <= minDuty || duty <= minDuty) return PCT_MIN;
    if (duty >= maxDuty) return PCT_MAX;
    const float frac = static_cast<float>(duty - minDuty) / static_cast<float>(maxDuty - minDuty);
    return clampPct(static_cast<int>(lroundf(powf(frac, 1.0f / GAMMA) * 100.0f)));
}

// ---- Saved settings record (settings.bin on the SD card): { uint32 magic; uint8 value; } ----
// V1 stored the raw 8-bit duty (B4848_LCD_BL_MIN..255) of the old linear slider. V2 stores the slider
// percentage. Anything else is rejected (the caller keeps its defaults).
constexpr uint32_t SETTINGS_MAGIC_V1 = 0x53455431; // "SET1": value = 8-bit duty, linear slider
constexpr uint32_t SETTINGS_MAGIC_V2 = 0x53455432; // "SET2": value = slider percent 0..100

// Slider percentage from a saved record. A V1 duty keeps its physical brightness: it is mapped back
// through the curve over the 8-bit range [legacyMin .. 255]. Returns false for an unknown magic or an
// out-of-range V2 value.
inline bool percentFromSaved(uint32_t magic, uint8_t value, int legacyMin, int &pct) {
    if (magic == SETTINGS_MAGIC_V2) {
        if (value > PCT_MAX) return false;
        pct = value;
        return true;
    }
    if (magic == SETTINGS_MAGIC_V1) {
        pct = percentForDuty(value, static_cast<uint32_t>(legacyMin), 255);
        return true;
    }
    return false;
}

} // namespace blcurve
