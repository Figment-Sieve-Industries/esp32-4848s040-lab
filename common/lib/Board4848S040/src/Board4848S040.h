#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "Board4848S040_pins.h"

// Display: Arduino_GFX RGB panel with the ST7701 init sequence for this board.
// The framebuffer lives in PSRAM, so PSRAM must be enabled (qio_opi).
namespace board4848 {

// Create the display object and run the ST7701 init.
// Returns nullptr if PSRAM is missing or too small for the framebuffer.
// (Other panel failures inside Arduino_GFX abort via ESP_ERROR_CHECK.)
Arduino_RGB_Display *initDisplay(uint8_t rotation = 0, size_t bouncePx = B4848_LCD_BOUNCE_PX);

// ---- Backlight: LEDC PWM on the EN pin of the boost driver ----
// The PWM starts at B4848_LCD_BL_FREQ_HZ (default 100 kHz) with the highest duty resolution LEDC allows at that
// frequency (capped at B4848_LCD_BL_MAX_RES_BITS): 9 bits at 100 kHz, 11 bits at 20-25 kHz.
// The first call of any setter attaches the pin. The duty is kept across frequency changes (rescaled).

// Brightness 0..255 (0 = off), mapped linearly onto the current duty resolution (255 = full duty).
// Below ~B4848_LCD_BL_MIN the driver cuts out, so 1..MIN is effectively off.
void setBacklight(uint8_t level);

// Brightness as a fraction of the period, 0.0 (off) .. 1.0 (always on); values outside are clamped.
void setBacklightLevel(float frac);

// Raw duty at the current resolution, 0..backlightDutyMax() (clamped). For tuning and sweeps.
void setBacklightDuty(uint32_t duty);

// Change the PWM frequency (100..1000000 Hz) and pick the highest resolution it allows. Keeps the brightness:
// the last duty is rescaled to the new resolution. Returns false (nothing changed) if LEDC refuses.
// Before any other backlight call this sets the starting frequency instead of B4848_LCD_BL_FREQ_HZ.
bool setBacklightFrequency(uint32_t hz);

uint32_t backlightFrequency();   // actual frequency in Hz (as LEDC reports it)
uint8_t backlightResolution();   // duty resolution in bits
uint32_t backlightDuty();        // current raw duty
uint32_t backlightDutyMax();     // 2^resolution - 1 = full duty
// B4848_LCD_BL_MIN (of 255) scaled to the current resolution, rounded up: the lowest duty that is assumed
// to keep the driver on. 100/255 -> 201 at 9 bits, 803 at 11 bits (the same fraction at any frequency).
uint32_t backlightMinDuty();

struct TouchPoint {
    uint16_t x;
    uint16_t y;
};

// Minimal GT911 driver (polling; INT and RST are not wired to the ESP32 on this board).
class Touch {
public:
    // Probes 0x5D then 0x14 and checks the product ID reads "911".
    bool begin();

    // Returns -1 if the GT911 has no new report since the last call,
    // 0 if it reports no touches (finger released),
    // otherwise the number of points written (up to maxPoints).
    // Coordinates are transformed to match setRotation().
    int8_t read(TouchPoint *points, uint8_t maxPoints = 1);

    // Match the display rotation (0..3) passed to initDisplay().
    void setRotation(uint8_t rotation) { rotation_ = rotation & 3; }

    uint8_t address() const { return addr_; }

private:
    bool readRegs(uint16_t reg, uint8_t *buf, size_t len);
    bool writeReg(uint16_t reg, uint8_t value);
    void transform(TouchPoint &p) const;
    uint8_t addr_ = 0;
    uint8_t rotation_ = 0;
};

} // namespace board4848
