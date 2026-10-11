// Copy to secrets.h (git-ignored) and fill in.
#pragma once

struct WifiCred {
    const char *ssid;
    const char *password;
};

// Any number of networks. WiFiMulti joins the strongest one in range and
// switches automatically if it drops. 2.4 GHz only (ESP32-S3).
static const WifiCred WIFI_NETWORKS[] = {
    {"My<home-ssid>", "password1"},
    {"MyOtherWiFi", "password2"},
};

// Powerwall 3 gateway on the home LAN (give it a DHCP reservation).
#define PW_HOST "192.0.2.10" // your gateway's LAN IP (placeholder)
// Customer password = last 5 characters of the gateway password on the QR sticker.
#define PW_CUSTOMER_PASSWORD "XXXXX"

// POSIX TZ string for the clock (UK shown).
#define TZ_INFO "GMT0BST,M3.5.0/1,M10.5.0"

// Weather location (Open-Meteo, no API key). Kept here because it's your home location.
#define WEATHER_LAT 51.5072
#define WEATHER_LON (-0.1276) // parenthesised: a bare negative macro breaks in expressions like a-WEATHER_LON
#define WEATHER_NAME "LONDON" // shown on the weather screen
