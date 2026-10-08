// 01-hello-display-touch
// Colour test, then a simple finger-paint screen showing touch coordinates.
#include <Arduino.h>
#include <Board4848S040.h>

static Arduino_RGB_Display *gfx;
static board4848::Touch touch;

static void drawHeader() {
    gfx->fillRect(0, 0, 480, 40, RGB565_DARKGREY);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(10, 12);
    gfx->print("ESP32-4848S040  touch to draw");
    gfx->fillRect(400, 0, 80, 40, RGB565_RED);
    gfx->setCursor(412, 12);
    gfx->print("CLR");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\nPSRAM: %u bytes free\n", ESP.getFreePsram());

    gfx = board4848::initDisplay();
    if (!gfx) {
        Serial.println("Display init failed");
        while (true) delay(1000);
    }
    board4848::setBacklight(255);

    const uint16_t colours[] = {RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_WHITE};
    for (uint16_t c : colours) {
        gfx->fillScreen(c);
        delay(400);
    }

    if (touch.begin()) {
        Serial.printf("GT911 found at 0x%02X\n", touch.address());
    } else {
        Serial.println("GT911 not found");
    }

    gfx->fillScreen(RGB565_BLACK);
    drawHeader();
}

void loop() {
    board4848::TouchPoint p;
    if (touch.read(&p, 1)) {
        if (p.y < 40 && p.x >= 400) {
            gfx->fillScreen(RGB565_BLACK);
            drawHeader();
        } else if (p.y >= 40) {
            gfx->fillCircle(p.x, p.y, 6, gfx->color565(p.x / 2, p.y / 2, 255 - p.x / 2));
        }
        gfx->fillRect(0, 456, 200, 24, RGB565_BLACK);
        gfx->setCursor(6, 460);
        gfx->printf("x=%3u y=%3u", p.x, p.y);
        Serial.printf("touch %u,%u\n", p.x, p.y);
    }
    delay(10);
}
