#include "Board4848S040.h"
#include <Wire.h>

namespace board4848 {

// ST7701 init sequence for the 4848S040 panel.
// Byte-identical to aquaElectronics/esp32-4848s040-st7701 and Arduino_GFX st7701_type9_init_operations,
// plus a 10 ms delay before Sleep Out. CD 00 disables MDT.
static const uint8_t st7701_4848s040_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x31, 0x05,
    WRITE_C8_D8, 0xCD, 0x00,

    WRITE_COMMAND_8, 0xB0, // Positive voltage gamma
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18,

    WRITE_COMMAND_8, 0xB1, // Negative voltage gamma
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18,

    // Page 1
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

    WRITE_C8_D8, 0xB0, 0x60, // Vop = 4.7375 V
    WRITE_C8_D8, 0xB1, 0x32, // VCOM
    WRITE_C8_D8, 0xB2, 0x07, // VGH = 15 V
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49, // VGL = -10.17 V
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21, // AVDD = 6.6 V, AVCL = -4.6 V
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00,

    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE4, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE7, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,

    WRITE_C8_D16, 0xEC, 0x3C, 0x00,

    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA,

    // VAP & VAN
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
    WRITE_C8_D8, 0xE5, 0xE4,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

    WRITE_C8_D8, 0x3A, 0x60, // 0x70 RGB888, 0x60 RGB666, 0x50 RGB565

    DELAY, 10,
    WRITE_COMMAND_8, 0x11, // Sleep out
    END_WRITE,

    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29, // Display on
    END_WRITE};

Arduino_RGB_Display *initDisplay(uint8_t rotation) {
    // The 480x480x2 framebuffer must fit in PSRAM. Arduino_GFX aborts (ESP_ERROR_CHECK)
    // rather than returning false if allocation fails, so check up front.
    if (!psramFound() || ESP.getFreePsram() < (uint32_t)B4848_LCD_WIDTH * B4848_LCD_HEIGHT * 2) {
        return nullptr;
    }

    Arduino_DataBus *bus = new Arduino_SWSPI(
        GFX_NOT_DEFINED /* DC */, B4848_LCD_CS, B4848_LCD_SCK, B4848_LCD_SDA, GFX_NOT_DEFINED /* MISO */);

    // Same values as Arduino_GFX's ESP32_4848S040_86BOX_GUITION dev-device config.
    Arduino_ESP32RGBPanel *panel = new Arduino_ESP32RGBPanel(
        B4848_LCD_DE, B4848_LCD_VSYNC, B4848_LCD_HSYNC, B4848_LCD_PCLK,
        B4848_LCD_R0, B4848_LCD_R1, B4848_LCD_R2, B4848_LCD_R3, B4848_LCD_R4,
        B4848_LCD_G0, B4848_LCD_G1, B4848_LCD_G2, B4848_LCD_G3, B4848_LCD_G4, B4848_LCD_G5,
        B4848_LCD_B0, B4848_LCD_B1, B4848_LCD_B2, B4848_LCD_B3, B4848_LCD_B4,
        1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
        1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
        0 /* pclk_active_neg */, 12000000 /* prefer_speed (PCLK Hz) */, false /* useBigEndian */,
        0 /* de_idle_high */, 0 /* pclk_idle_high */, 0 /* bounce_buffer_size_px */);

    Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
        B4848_LCD_WIDTH, B4848_LCD_HEIGHT, panel, rotation, true /* auto_flush */,
        bus, GFX_NOT_DEFINED /* RST */, st7701_4848s040_init_operations, sizeof(st7701_4848s040_init_operations));

    if (!gfx->begin()) {
        delete gfx;
        delete panel;
        delete bus;
        return nullptr;
    }
    return gfx;
}

void setBacklight(uint8_t level) {
    // Attach explicitly: analogWriteFrequency() is ignored on a pin that isn't attached yet
    // (core 3.x), which left the PWM at 1 kHz. 100 kHz avoids audible coil whine.
    static bool attached = false;
    if (!attached) {
        attached = ledcAttach(B4848_LCD_BL, 100000, 8);
        if (!attached) return;
    }
    ledcWrite(B4848_LCD_BL, level);
}

// ---------------- GT911 ----------------
static constexpr uint16_t GT911_REG_PRODUCT_ID = 0x8140;
static constexpr uint16_t GT911_REG_STATUS = 0x814E;
static constexpr uint16_t GT911_REG_POINT1 = 0x814F;

bool Touch::begin() {
    Wire.begin(B4848_TOUCH_SDA, B4848_TOUCH_SCL, 400000);
    const uint8_t candidates[] = {0x5D, 0x14};
    for (uint8_t a : candidates) {
        addr_ = a;
        uint8_t id[4];
        if (readRegs(GT911_REG_PRODUCT_ID, id, sizeof(id)) && memcmp(id, "911", 3) == 0) {
            return true;
        }
    }
    addr_ = 0;
    return false;
}

int8_t Touch::read(TouchPoint *points, uint8_t maxPoints) {
    if (!addr_) return -1;
    uint8_t status;
    if (!readRegs(GT911_REG_STATUS, &status, 1)) return -1;
    if (!(status & 0x80)) return -1; // no new report

    uint8_t count = status & 0x0F;
    if (count > 5) count = 0;
    uint8_t n = count < maxPoints ? count : maxPoints;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t raw[8]; // track id, x lo, x hi, y lo, y hi, size lo, size hi, reserved
        if (!readRegs(GT911_REG_POINT1 + i * 8, raw, sizeof(raw))) {
            n = i;
            break;
        }
        points[i].x = raw[1] | (raw[2] << 8);
        points[i].y = raw[3] | (raw[4] << 8);
        transform(points[i]);
    }
    writeReg(GT911_REG_STATUS, 0); // ack so the GT911 refills the buffer
    return n;
}

// Inverse of Arduino_RGB_Display's rotation mapping (logical -> panel), so touch
// coordinates land in the same logical space the display draws in.
void Touch::transform(TouchPoint &p) const {
    const uint16_t px = p.x, py = p.y;
    switch (rotation_) {
    case 1:
        p.x = py;
        p.y = B4848_LCD_WIDTH - 1 - px;
        break;
    case 2:
        p.x = B4848_LCD_WIDTH - 1 - px;
        p.y = B4848_LCD_HEIGHT - 1 - py;
        break;
    case 3:
        p.x = B4848_LCD_HEIGHT - 1 - py;
        p.y = px;
        break;
    }
}

bool Touch::readRegs(uint16_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(addr_);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.endTransmission(false); // repeated start; a NACK shows up as a short read below
    if (Wire.requestFrom(addr_, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool Touch::writeReg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(addr_);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

} // namespace board4848
