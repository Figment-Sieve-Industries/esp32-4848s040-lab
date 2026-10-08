#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "Board4848S040_pins.h"

// Display: Arduino_GFX RGB panel with the ST7701 init sequence for this board.
// The framebuffer lives in PSRAM, so PSRAM must be enabled (qio_opi).
namespace board4848 {

// Create the display object and run the ST7701 init. Returns nullptr on failure.
Arduino_RGB_Display *initDisplay(uint8_t rotation = 0);

// Backlight brightness 0..255 (0 = off).
void setBacklight(uint8_t level);

struct TouchPoint {
    uint16_t x;
    uint16_t y;
};

// Minimal GT911 driver (polling, no INT/RST pins on this board).
class Touch {
public:
    // Probes 0x5D then 0x14. Returns false if no GT911 answers.
    bool begin();
    // Returns number of touches (0..5) and fills up to maxPoints entries.
    uint8_t read(TouchPoint *points, uint8_t maxPoints = 1);
    uint8_t address() const { return addr_; }

private:
    bool readRegs(uint16_t reg, uint8_t *buf, size_t len);
    bool writeReg(uint16_t reg, uint8_t value);
    uint8_t addr_ = 0;
};

} // namespace board4848
