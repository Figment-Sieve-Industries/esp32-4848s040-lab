#pragma once
// First-boot setup portal (design boards U and V of the private design canvas).
//
// Flow: no usable config (cfg::configured() false), or the screen held for 5 s at power-on -> setup mode instead of
// the dashboard:
//   - Soft AP "PW-Display-XXXX" (last 2 MAC bytes, open, channel follows the STA once it joins), captive DNS that
//     answers every name with 192.168.4.1, web server on port 80 serving one self-contained page (no CDN: the phone
//     has no internet while it is on the AP).
//   - LVGL setup screen on the panel: QR code to join the AP (WIFI:T:nopass;S:<ssid>;;), the steps, then the test
//     progress (Wi-Fi, gateway found, gateway login, location/time zone) with OK / failed + reason.
//   - The phone submits Wi-Fi first; the device joins it as STA while keeping the AP (WIFI_AP_STA), because the
//     town lookup (Open-Meteo geocoding), gateway discovery (gw_discover.h) and the login test need the home LAN or
//     the internet. Those run on the device and are exposed to the page as small JSON endpoints.
//   - "Test and save": every step is tested; on success cfg::save() (the only NVS write), then ESP.restart().
// Passwords are never sent back to the page and never printed on serial.
#include <Board4848S040.h>

// True if setup should run: no usable config, or the touch panel is held down for 5 s right after boot
// (the panel shows a countdown while it is held). Call after lvglBegin() and cfg::begin().
bool setupRequested(board4848::Touch *touch);

// Runs the setup portal until the user saves; then restarts. Never returns. Call before the dashboard screens are
// created and before powerwallStart() (it owns Wi-Fi while it runs).
[[noreturn]] void setupPortalRun();
