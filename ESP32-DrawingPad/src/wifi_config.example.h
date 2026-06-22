#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// Copy this file to src/wifi_config.h and fill in your local network details.
// Keep src/wifi_config.h out of git because it contains private credentials.

// Add every trusted network you want White Pad to use. At send time, the ESP32
// scans and chooses the strongest configured network it can see.
#define WHITEPAD_WIFI_NETWORKS { \
    {"HomeWiFi", "HomePassword"}, \
    {"StudioWiFi", "StudioPassword"}, \
    {"PhoneHotspot", "HotspotPassword"} \
}

// Example for a relay running on your laptop:
//   node relay/server.js
//   ipconfig getifaddr en0
// Then use that IP here. The ESP32 now accepts either a full endpoint path
// or just the base host/domain and will default to /api/sketches.
#define WHITEPAD_RELAY_URL "http://192.168.1.50:8787"

// Optional shared secret for relay uploads. If set, the ESP32 sends:
//   Authorization: Bearer <token>
// The relay should validate the same token before accepting jobs.
// #define WHITEPAD_RELAY_TOKEN "replace-with-a-long-random-secret"

// Cloudflare tunnel example:
//   cloudflared tunnel --url http://localhost:8787
// Then use the generated HTTPS URL:
// #define WHITEPAD_RELAY_URL "https://your-tunnel.trycloudflare.com"

#endif
