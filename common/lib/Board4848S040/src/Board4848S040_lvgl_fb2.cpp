#include "Board4848S040_lvgl_fb2.h"
#if __has_include(<lvgl.h>)

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// How it works
// ------------
// The ESP-IDF RGB driver owns fbs[0] and fbs[1] in PSRAM. With bounce buffers, an ISR copies the
// "current" framebuffer chunk by chunk into internal RAM for the LCD DMA. When it wraps past the
// last chunk it latches the next framebuffer (bb_fb_index = cur_fb_index) and fires
// on_frame_buf_complete. esp_lcd_panel_draw_bitmap() with a pointer that lies inside one of the
// driver's framebuffers doesn't copy anything; it only sets cur_fb_index.
//
// flush (last area of a frame):
//   draw_bitmap(fb)            -> cur_fb_index = fb (takes effect at the next wrap)
//   clear the semaphore, then wait for on_frame_buf_complete
//                              -> the ISR has now latched fb; from here on it reads only fb,
//                                 so the other buffer is free for LVGL to draw into.
//   lv_display_flush_ready()   -> LVGL swaps buf_act to the other buffer.
// This is esp_lvgl_port's avoid_tearing + bb_mode path (lvgl_port_flush_callback with
// on_bounce_frame_finish). If the wrap lands in the few instructions between draw_bitmap and
// the semaphore clear, we wait one extra frame: slower, but never unsafe.
//
// DIRECT mode keeps both buffers in sync inside LVGL (lv_refr.c refr_sync_areas): areas drawn
// into buffer A in frame N are copied A -> B before frame N+1 is rendered into B, minus the
// parts frame N+1 redraws anyway. So no extra code is needed here.
//
// Cache: LVGL writes through the CPU cache and the bounce ISR reads with memcpy through the same
// cache, so no writeback is needed (the driver also skips msync in bounce-buffer mode).

namespace board4848 {

// ST7701 init sequence for the 4848S040 panel. A copy of st7701_4848s040_init_operations in
// Board4848S040.cpp (it's file-static there). Keep the two identical.
static const uint8_t st7701_init_ops_fb2[] = {
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

// A wait longer than this means the frame-complete callback isn't firing. One frame at 12 MHz
// is (480+10+8+50) * (480+10+8+20) / 12 MHz = 23.7 ms.
static constexpr uint32_t VSYNC_TIMEOUT_MS = 100;

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_frameSem;
static Touch *s_touch;
static Fb2Stats s_stats;

static uint32_t tickCb() { return millis(); }

// Bounce-buffer ISR: the driver just wrapped to the start of a frame and latched cur_fb_index.
static IRAM_ATTR bool frameCompleteCb(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frameSem, &woken);
    return woken == pdTRUE;
}

static void flushCb(lv_display_t *disp, const lv_area_t *, uint8_t *px) {
    // DIRECT: called once per dirty area, px = start of the whole buffer. Present only after the
    // last area. FULL: called once per frame (always the last).
    if (lv_display_flush_is_last(disp)) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, B4848_LCD_WIDTH, B4848_LCD_HEIGHT, px);
        xSemaphoreTake(s_frameSem, 0); // drop a completion from before the swap request
        const int64_t t0 = esp_timer_get_time();
        if (xSemaphoreTake(s_frameSem, pdMS_TO_TICKS(VSYNC_TIMEOUT_MS)) != pdTRUE) {
            s_stats.vsyncTimeouts++;
        }
        const uint32_t waited = (uint32_t)(esp_timer_get_time() - t0);
        s_stats.presents++;
        s_stats.waitUs += waited;
        if (waited > s_stats.maxWaitUs) s_stats.maxWaitUs = waited;
    }
    lv_display_flush_ready(disp);
}

// Same behaviour as touchReadCb in Board4848S040_lvgl.cpp (copied so that file stays untouched).
static void touchReadCb(lv_indev_t *, lv_indev_data_t *data) {
    static TouchPoint last;
    static bool pressed = false;
    static uint32_t lastSeen = 0;

    TouchPoint p;
    int8_t n = s_touch->read(&p, 1);
    if (n > 0) {
        last = p;
        pressed = true;
        lastSeen = millis();
    } else if (n == 0 || millis() - lastSeen > 100) {
        pressed = false;
    }
    data->point.x = last.x;
    data->point.y = last.y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ST7701 init over 3-wire SPI, as Arduino_RGB_Display::begin() does it (no RST pin: soft reset).
static bool initPanelSpi() {
    Arduino_SWSPI bus(GFX_NOT_DEFINED /* DC */, B4848_LCD_CS, B4848_LCD_SCK, B4848_LCD_SDA, GFX_NOT_DEFINED /* MISO */);
    if (!bus.begin()) return false;
    bus.sendCommand(0x01); // software reset
    delay(120);
    bus.batchOperation(st7701_init_ops_fb2, sizeof(st7701_init_ops_fb2));
    return true;
}

static esp_err_t createRgbPanel() {
    // Field-by-field so it mirrors Arduino_ESP32RGBPanel::getFrameBuffer() for initDisplay(),
    // except num_fbs = 2.
    esp_lcd_rgb_panel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = 12000000;
    cfg.timings.h_res = B4848_LCD_WIDTH;
    cfg.timings.v_res = B4848_LCD_HEIGHT;
    cfg.timings.hsync_pulse_width = 8;
    cfg.timings.hsync_back_porch = 50;
    cfg.timings.hsync_front_porch = 10;
    cfg.timings.vsync_pulse_width = 8;
    cfg.timings.vsync_back_porch = 20;
    cfg.timings.vsync_front_porch = 10;
    // Arduino_ESP32RGBPanel maps polarity 1 -> idle_low 0; pclk_active_neg 0; de/pclk idle low.
    cfg.timings.flags.hsync_idle_low = 0;
    cfg.timings.flags.vsync_idle_low = 0;
    cfg.timings.flags.de_idle_high = 0;
    cfg.timings.flags.pclk_active_neg = 0;
    cfg.timings.flags.pclk_idle_high = 0;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 2;
    cfg.bounce_buffer_size_px = B4848_LCD_BOUNCE_PX;
    cfg.dma_burst_size = 64; // same storage as Arduino_GFX's psram_trans_align = 64
    cfg.hsync_gpio_num = B4848_LCD_HSYNC;
    cfg.vsync_gpio_num = B4848_LCD_VSYNC;
    cfg.de_gpio_num = B4848_LCD_DE;
    cfg.pclk_gpio_num = B4848_LCD_PCLK;
    cfg.disp_gpio_num = GPIO_NUM_NC;
    const int data[16] = {
        B4848_LCD_B0, B4848_LCD_B1, B4848_LCD_B2, B4848_LCD_B3, B4848_LCD_B4,
        B4848_LCD_G0, B4848_LCD_G1, B4848_LCD_G2, B4848_LCD_G3, B4848_LCD_G4, B4848_LCD_G5,
        B4848_LCD_R0, B4848_LCD_R1, B4848_LCD_R2, B4848_LCD_R3, B4848_LCD_R4};
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
    cfg.flags.disp_active_low = 1;
    cfg.flags.fb_in_psram = 1;

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) return err;

    esp_lcd_rgb_panel_event_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_frame_buf_complete = frameCompleteCb; // = on_bounce_frame_finish in bounce-buffer mode
    err = esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, nullptr);
    if (err != ESP_OK) return err;

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) return err;
    return esp_lcd_panel_init(s_panel); // starts scanning out fbs[0]
}

lv_display_t *lvglBeginDoubleBuffered(Touch *touch, bool fullRefresh) {
    const size_t fbBytes = (size_t)B4848_LCD_WIDTH * B4848_LCD_HEIGHT * 2;
    if (!psramFound() || ESP.getFreePsram() < 2 * fbBytes) {
        Serial.println("fb2: not enough PSRAM for 2 framebuffers");
        return nullptr;
    }
    s_frameSem = xSemaphoreCreateBinary();
    if (!s_frameSem) return nullptr;

    if (!initPanelSpi()) {
        Serial.println("fb2: ST7701 SPI init failed");
        return nullptr;
    }
    esp_err_t err = createRgbPanel();
    if (err != ESP_OK) {
        Serial.printf("fb2: RGB panel setup failed: %s\n", esp_err_to_name(err));
        return nullptr;
    }
    void *fb0 = nullptr, *fb1 = nullptr;
    err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1);
    if (err != ESP_OK || !fb0 || !fb1) {
        Serial.printf("fb2: get_frame_buffer failed: %s\n", esp_err_to_name(err));
        return nullptr;
    }

    s_touch = touch;
    lv_init();
    lv_tick_set_cb(tickCb);

    lv_display_t *disp = lv_display_create(B4848_LCD_WIDTH, B4848_LCD_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flushCb);
    // The panel starts on fbs[0], so hand LVGL fbs[1] first: the first frame is drawn off-screen.
    lv_display_set_buffers(disp, fb1, fb0, fbBytes,
                           fullRefresh ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_DIRECT);

    if (touch) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touchReadCb);
    }
    return disp;
}

Fb2Stats fb2Stats(bool reset) {
    Fb2Stats s = s_stats;
    if (reset) s_stats = Fb2Stats{};
    return s;
}

} // namespace board4848

#endif
