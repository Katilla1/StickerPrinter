#ifndef WIFI_SENDER_H
#define WIFI_SENDER_H

#include <Arduino.h>

struct RelayJob {
    uint8_t* data = nullptr;
    size_t length = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t format = 0;
};

class WiFiSender {
public:
    static bool isConfigured();
    static bool connect();
    static void disconnect();
    static bool isConnected();
    static const char* currentSSID();
    static const char* localIP();
    static const char* gatewayIP();
    static const char* relayHost();
    static int lastStatus();
    static const char* lastError();
    static bool fetchNextJob(RelayJob& job);
    static void freeJob(RelayJob& job);
    static bool sendSketch(const uint8_t* bitmap, uint16_t width, uint16_t height);
    static bool sendColorSketch(const uint8_t* indexed4bpp, const uint16_t* palette, uint8_t paletteCount, uint16_t width, uint16_t height);
};

#endif
