#pragma once
// LVGL views of the setup portal (design board U of the private design canvas): the hold-to-enter countdown and
// the portal screen (QR code to join the hotspot + 3 steps + status card; test view with 4 step rows). LVGL thread
// only. Objects are created once; setupscreen::update() changes only what differs.
#include <lvgl.h>
#include <stdint.h>

namespace setupscreen {

// ---- hold-to-enter countdown (setupRequested) ----
void countdownShow();               // creates and loads the countdown screen
void countdownSet(uint32_t heldMs, uint32_t needMs); // big seconds-left number + progress bar
void countdownClose();              // back to the previous screen, deletes the countdown

// ---- portal screen ----
enum class Mode : uint8_t { Join, Test };
enum class Mark : uint8_t { Wait, Run, Ok, Fail };
enum class Tone : uint8_t { Muted, Warn, Ok, Fail };

constexpr int STEPS = 4;

struct View {
    Mode mode = Mode::Join;
    // Join view: status card
    Tone statusTone = Tone::Warn;
    char status[64] = "Starting...";
    // Test view
    Mark mark[STEPS] = {};
    char label[STEPS][48] = {};
    char note[STEPS][40] = {};
    Tone resultTone = Tone::Warn;
    char result[96] = "";
};

// apSsid: the hotspot name shown in step 1; qr: the payload for the QR code (WIFI:T:nopass;S:...;;).
void portalShow(const char *apSsid, const char *qr);
void portalUpdate(const View &v);

} // namespace setupscreen
