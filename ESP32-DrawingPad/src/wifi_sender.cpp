#include "wifi_sender.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif

struct WiFiCredential {
    const char* ssid;
    const char* password;
};

// Development configuration. Move this to Preferences or a setup screen after
// the relay flow is proven. WHITEPAD_WIFI_NETWORKS can contain multiple trusted
// networks, and the sender will prefer the strongest visible configured network.
#ifndef WHITEPAD_WIFI_SSID
#define WHITEPAD_WIFI_SSID ""
#endif

#ifndef WHITEPAD_WIFI_PASS
#define WHITEPAD_WIFI_PASS ""
#endif

#ifndef WHITEPAD_RELAY_URL
#define WHITEPAD_RELAY_URL ""
#endif

#ifndef WHITEPAD_RELAY_TOKEN
#define WHITEPAD_RELAY_TOKEN ""
#endif

#ifndef WHITEPAD_WIFI_NETWORKS
#define WHITEPAD_WIFI_NETWORKS {{WHITEPAD_WIFI_SSID, WHITEPAD_WIFI_PASS}}
#endif

static const WiFiCredential WIFI_NETWORKS[] = WHITEPAD_WIFI_NETWORKS;
static const uint8_t WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
static int lastHttpStatus = 0;
static char lastRelayHost[64] = {0};
static char lastConnectError[64] = {0};

static bool parseRelayUrl(const char* url, const char* defaultPath, bool useUrlPath, char* host, size_t hostLen, uint16_t& port, const char*& path, bool& secure) {
    const char* hostStart = url;
    const char* pathStart = nullptr;
    const char* portStart = nullptr;

    secure = false;
    port = 80;

    if (strncmp(url, "http://", 7) == 0) {
        hostStart = url + 7;
        port = 80;
    } else if (strncmp(url, "https://", 8) == 0) {
        hostStart = url + 8;
        secure = true;
        port = 443;
    }

    pathStart = strchr(hostStart, '/');
    portStart = strchr(hostStart, ':');

    size_t copyLen = 0;
    if (portStart && (!pathStart || portStart < pathStart)) {
        copyLen = portStart - hostStart;
        port = atoi(portStart + 1);
    } else if (pathStart) {
        copyLen = pathStart - hostStart;
    } else {
        copyLen = strlen(hostStart);
    }

    if (copyLen == 0 || copyLen >= hostLen || port == 0) return false;
    memcpy(host, hostStart, copyLen);
    host[copyLen] = '\0';

    if (useUrlPath && pathStart) {
        path = pathStart;
    } else {
        path = defaultPath;
    }
    return true;
}

static bool readHttpResponseBody(Client& client, uint8_t*& outData, size_t& outLen) {
    outData = nullptr;
    outLen = 0;

    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    int statusCode = 0;
    if (statusLine.startsWith("HTTP/1.")) {
        statusCode = statusLine.substring(9, 12).toInt();
    }
    lastHttpStatus = statusCode > 0 ? statusCode : -7;
    if (statusCode == 204) return true;
    if (statusCode < 200 || statusCode >= 300) return false;

    int contentLength = -1;
    while (true) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) break;
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
        }
    }
    if (contentLength < 0) return false;
    if (contentLength == 0) return true;

    outData = (uint8_t*)malloc(contentLength);
    if (!outData) return false;

    size_t offset = 0;
    uint32_t start = millis();
    while (offset < (size_t)contentLength && millis() - start < 8000) {
        int available = client.available();
        if (available <= 0) {
            delay(10);
            continue;
        }
        size_t toRead = min((size_t)available, (size_t)contentLength - offset);
        size_t read = client.read(outData + offset, toRead);
        if (read == 0) {
            delay(10);
            continue;
        }
        offset += read;
    }

    if (offset != (size_t)contentLength) {
        free(outData);
        outData = nullptr;
        return false;
    }
    outLen = offset;
    return true;
}

static bool writeAll(Client& client, const uint8_t* data, size_t len) {
    const size_t CHUNK_SIZE = 1024;
    size_t sent = 0;

    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > CHUNK_SIZE) chunk = CHUNK_SIZE;

        size_t written = client.write(data + sent, chunk);
        if (written == 0) return false;

        sent += written;
        delay(1);
    }

    return true;
}

static bool postPayload(const uint8_t* header, size_t headerBytes, const uint8_t* body, size_t bodyBytes) {
    if (!header || headerBytes == 0 || (!body && bodyBytes > 0)) {
        lastHttpStatus = -1;
        return false;
    }
    if (!WiFiSender::connect()) {
        lastHttpStatus = -2;
        return false;
    }

    char host[64];
    const char* path = nullptr;
    uint16_t port = 80;
    bool secure = false;
    if (!parseRelayUrl(WHITEPAD_RELAY_URL, "/api/sketches", true, host, sizeof(host), port, path, secure)) {
        lastHttpStatus = -3;
        WiFiSender::disconnect();
        return false;
    }
    strlcpy(lastRelayHost, host, sizeof(lastRelayHost));

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    Client* client = nullptr;

    if (secure) {
        secureClient.setInsecure();
        secureClient.setTimeout(8000);
        client = &secureClient;
    } else {
        plainClient.setTimeout(6000);
        client = &plainClient;
    }

    IPAddress relayIP;
    bool connected = false;
    if (relayIP.fromString(host)) {
        connected = client->connect(relayIP, port);
    } else {
        connected = client->connect(host, port);
    }

    if (!connected) {
        lastHttpStatus = -4;
        WiFiSender::disconnect();
        return false;
    }

    const size_t payloadBytes = headerBytes + bodyBytes;
    client->printf("POST %s HTTP/1.1\r\n", path);
    client->printf("Host: %s:%u\r\n", host, port);
    client->print("Content-Type: application/octet-stream\r\n");
    if (strlen(WHITEPAD_RELAY_TOKEN) > 0) {
        client->printf("Authorization: Bearer %s\r\n", WHITEPAD_RELAY_TOKEN);
    }
    client->printf("Content-Length: %u\r\n", (unsigned)payloadBytes);
    client->print("Connection: close\r\n\r\n");

    if (!writeAll(*client, header, headerBytes)) {
        lastHttpStatus = -5;
        client->stop();
        WiFiSender::disconnect();
        return false;
    }

    if (!writeAll(*client, body, bodyBytes)) {
        lastHttpStatus = -6;
        client->stop();
        WiFiSender::disconnect();
        return false;
    }

    uint32_t start = millis();
    while (!client->available() && client->connected() && millis() - start < 8000) {
        delay(10);
    }

    String statusLine = client->readStringUntil('\n');
    client->stop();
    int code = 0;
    if (statusLine.startsWith("HTTP/1.")) {
        code = statusLine.substring(9, 12).toInt();
    }
    lastHttpStatus = code > 0 ? code : -7;

    WiFiSender::disconnect();
    return code >= 200 && code < 300;
}

bool WiFiSender::isConfigured() {
    if (strlen(WHITEPAD_RELAY_URL) == 0) return false;
    for (uint8_t i = 0; i < WIFI_NETWORK_COUNT; i++) {
        if (WIFI_NETWORKS[i].ssid && strlen(WIFI_NETWORKS[i].ssid) > 0) return true;
    }
    return false;
}

bool WiFiSender::connect() {
    if (!isConfigured()) return false;
    if (WiFi.status() == WL_CONNECTED) return true;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, true);
    delay(100);

    int bestNetwork = -1;
    int32_t bestRSSI = -1000;
    int found = WiFi.scanNetworks();
    if (found < 0) {
        strlcpy(lastConnectError, "scan failed", sizeof(lastConnectError));
        WiFi.mode(WIFI_OFF);
        return false;
    }
    for (int scanIdx = 0; scanIdx < found; scanIdx++) {
        String foundSSID = WiFi.SSID(scanIdx);
        for (uint8_t cfgIdx = 0; cfgIdx < WIFI_NETWORK_COUNT; cfgIdx++) {
            if (foundSSID == WIFI_NETWORKS[cfgIdx].ssid && WiFi.RSSI(scanIdx) > bestRSSI) {
                bestNetwork = cfgIdx;
                bestRSSI = WiFi.RSSI(scanIdx);
            }
        }
    }
    WiFi.scanDelete();

    if (bestNetwork >= 0) {
        WiFi.begin(WIFI_NETWORKS[bestNetwork].ssid, WIFI_NETWORKS[bestNetwork].password);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            delay(100);
        }
        if (WiFi.status() == WL_CONNECTED) return true;
        snprintf(lastConnectError, sizeof(lastConnectError), "auth fail %s", WIFI_NETWORKS[bestNetwork].ssid);
        WiFi.disconnect(false);
    }

    for (uint8_t i = 0; i < WIFI_NETWORK_COUNT; i++) {
        if (!WIFI_NETWORKS[i].ssid || strlen(WIFI_NETWORKS[i].ssid) == 0 || i == bestNetwork) continue;
        WiFi.begin(WIFI_NETWORKS[i].ssid, WIFI_NETWORKS[i].password);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
            delay(100);
        }
        if (WiFi.status() == WL_CONNECTED) return true;
        snprintf(lastConnectError, sizeof(lastConnectError), "no connect %s", WIFI_NETWORKS[i].ssid);
        WiFi.disconnect(false);
    }

    if (lastConnectError[0] == '\0') {
        strlcpy(lastConnectError, "no known ssid seen", sizeof(lastConnectError));
    }
    WiFi.mode(WIFI_OFF);
    return false;
}

void WiFiSender::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool WiFiSender::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

const char* WiFiSender::currentSSID() {
    static char ssid[33] = {0};
    if (WiFi.status() != WL_CONNECTED) return "";
    String current = WiFi.SSID();
    current.toCharArray(ssid, sizeof(ssid));
    return ssid;
}

const char* WiFiSender::localIP() {
    static char ip[16] = {0};
    if (WiFi.status() != WL_CONNECTED) return "";
    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));
    return ip;
}

const char* WiFiSender::gatewayIP() {
    static char ip[16] = {0};
    if (WiFi.status() != WL_CONNECTED) return "";
    WiFi.gatewayIP().toString().toCharArray(ip, sizeof(ip));
    return ip;
}

int WiFiSender::lastStatus() {
    return lastHttpStatus;
}

const char* WiFiSender::lastError() {
    return lastConnectError;
}

bool WiFiSender::fetchNextJob(RelayJob& job) {
    job.data = nullptr;
    job.length = 0;
    job.width = 0;
    job.height = 0;
    job.format = 0;

    lastHttpStatus = 0;
    if (!isConfigured()) {
        lastHttpStatus = -1;
        return false;
    }
    if (!connect()) {
        lastHttpStatus = -2;
        return false;
    }

    char host[64];
    const char* path = nullptr;
    uint16_t port = 80;
    bool secure = false;
    if (!parseRelayUrl(WHITEPAD_RELAY_URL, "/api/jobs/next", false, host, sizeof(host), port, path, secure)) {
        lastHttpStatus = -3;
        WiFiSender::disconnect();
        return false;
    }
    strlcpy(lastRelayHost, host, sizeof(lastRelayHost));

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    Client* client = nullptr;

    if (secure) {
        secureClient.setInsecure();
        secureClient.setTimeout(8000);
        client = &secureClient;
    } else {
        plainClient.setTimeout(6000);
        client = &plainClient;
    }

    IPAddress relayIP;
    bool connected = false;
    if (relayIP.fromString(host)) {
        connected = client->connect(relayIP, port);
    } else {
        connected = client->connect(host, port);
    }
    if (!connected) {
        lastHttpStatus = -4;
        WiFiSender::disconnect();
        return false;
    }

    client->printf("GET %s HTTP/1.1\r\n", path);
    client->printf("Host: %s:%u\r\n", host, port);
    if (strlen(WHITEPAD_RELAY_TOKEN) > 0) {
        client->printf("Authorization: Bearer %s\r\n", WHITEPAD_RELAY_TOKEN);
    }
    client->print("Connection: close\r\n\r\n");

    uint8_t* response = nullptr;
    size_t responseLen = 0;
    bool ok = readHttpResponseBody(*client, response, responseLen);
    client->stop();
    WiFiSender::disconnect();
    if (!ok) {
        if (response) free(response);
        return false;
    }
    if (lastHttpStatus == 204 || responseLen == 0) {
        if (response) free(response);
        return false;
    }
    if (responseLen < 9 || memcmp(response, "WPD1", 4) != 0) {
        free(response);
        lastHttpStatus = -8;
        return false;
    }

    uint16_t width = response[4] | ((uint16_t)response[5] << 8);
    uint16_t height = response[6] | ((uint16_t)response[7] << 8);
    uint8_t format = response[8];
    size_t expectedBytes = 9 + (((uint32_t)width * height) / 8);
    if (format != 1 || width == 0 || height == 0 || responseLen != expectedBytes) {
        free(response);
        lastHttpStatus = -9;
        return false;
    }

    job.data = response;
    job.length = responseLen;
    job.width = width;
    job.height = height;
    job.format = format;
    return true;
}

void WiFiSender::freeJob(RelayJob& job) {
    if (job.data) free(job.data);
    job.data = nullptr;
    job.length = 0;
    job.width = 0;
    job.height = 0;
    job.format = 0;
}

const char* WiFiSender::relayHost() {
    return lastRelayHost;
}

bool WiFiSender::sendSketch(const uint8_t* bitmap, uint16_t width, uint16_t height) {
    lastHttpStatus = 0;
    if (!bitmap) {
        lastHttpStatus = -1;
        return false;
    }
    const size_t bitmapBytes = ((uint32_t)width * height) / 8;

    uint8_t sketchHeader[9] = {
        'W', 'P', 'D', '1',
        (uint8_t)(width & 0xFF), (uint8_t)((width >> 8) & 0xFF),
        (uint8_t)(height & 0xFF), (uint8_t)((height >> 8) & 0xFF),
        1
    };

    return postPayload(sketchHeader, sizeof(sketchHeader), bitmap, bitmapBytes);
}

bool WiFiSender::sendColorSketch(const uint8_t* indexed4bpp, const uint16_t* palette, uint8_t paletteCount, uint16_t width, uint16_t height) {
    lastHttpStatus = 0;
    if (!indexed4bpp || !palette || paletteCount == 0 || paletteCount > 15) {
        lastHttpStatus = -1;
        return false;
    }

    const size_t pixelBytes = (((uint32_t)width * height) + 1) / 2;
    const size_t headerBytes = 10 + ((size_t)paletteCount * 2);
    uint8_t header[10 + (15 * 2)];

    header[0] = 'W';
    header[1] = 'P';
    header[2] = 'C';
    header[3] = '1';
    header[4] = width & 0xFF;
    header[5] = (width >> 8) & 0xFF;
    header[6] = height & 0xFF;
    header[7] = (height >> 8) & 0xFF;
    header[8] = 2; // 4-bit indexed RGB565.
    header[9] = paletteCount;

    for (uint8_t i = 0; i < paletteCount; i++) {
        header[10 + (i * 2)] = palette[i] & 0xFF;
        header[11 + (i * 2)] = (palette[i] >> 8) & 0xFF;
    }

    return postPayload(header, headerBytes, indexed4bpp, pixelBytes);
}
