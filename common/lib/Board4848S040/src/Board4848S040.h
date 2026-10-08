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
Arduino_RGB_Display *initDisplay(uint8_t rotation = 0);

// Backlight brightness 0..255 (0 = off). 100 kHz LEDC PWM.
// Below ~B4848_LCD_BL_MIN the driver cuts out, so 1..MIN is effectively off.
void setBacklight(uint8_t level);

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
