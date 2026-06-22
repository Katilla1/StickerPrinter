# ESP32 Drawing Pad for D21 Printer 🎨🖨️

This sub-project turns your Hosyond 3.5" ESP32 display into a standalone drawing tablet for the D21 Thermal Printer.

## 🚀 Features
- **Direct BLE Connection:** Connects directly to the D21 printer without a phone or computer.
- **Low-Latency Drawing:** Optimized drawing canvas with thick-line support.
- **Real-time Status:** Monitors printer battery and connection state.
- **Persistent Color Layer:** Colored strokes stay colored through undo/redo and are preserved for relay upload.
- **One-Touch Print:** Instantly sends your drawing to the printer.

## 🛠️ Requirements
- **Hardware:** Hosyond 3.5" ESP32 Module (ST7796 Driver, Resistive Touch).
- **Software:** VS Code with [PlatformIO IDE](https://platformio.org/platformio-ide).

## 📥 Getting Started
1. Open this folder (`ESP32-DrawingPad`) in VS Code.
2. PlatformIO will automatically download the necessary libraries (`LovyanGFX`, `NimBLE-Arduino`).
3. Connect your ESP32 board via USB.
4. Click the **Upload** button (check-mark icon in the bottom bar).

## 🎮 Usage
1. **Connect:** Power on your D21 printer. Tap the **CONN** button on the ESP32 screen. The status bar will change to "Connected" once paired.
2. **Draw:** Use the monochrome pen, brush, ink, and eraser tools. Tap **SIZE** to open the compact brush-size menu.
3. **Refine:** Use **P/E** to switch printer or email/relay output, **U/R** for stroke undo/redo, **-/+** for zoom out/in, and **S/J** to toggle smoothed or raw stroke input. Tap `?` to open the on-device button guide.
4. **Pan While Zoomed:** At `2X` or `4X`, tap the zoom label to enter `PAN`, then drag the canvas to move around the full note. Tap `PAN` again to return to drawing.
5. **Print or Send:** In printer mode, tap **CONN** then **PRINT**. In email mode, tap **SEND** to upload the sketch to your relay.
6. **Clear:** Tap **CLEAR** to wipe the canvas and start over.

## D21 BLE Print Flow
The standalone print path mirrors the working browser app:

1. Connect to service `FF00`, characteristic `FF02`.
2. Subscribe to `FF02` notifications with a CCCD write response.
3. Watch for `01 01 02 B6 00` to confirm the printer application layer is awake.
4. Send the web app handshake, then raster rows as 48-byte chunks.
5. Treat repeated `01 06` notifications as command/raster ACKs.

Printed output applies a small raster ink-spread pass so thermal lines better match the apparent on-screen brush weight after downsampling from the 640-pixel canvas to the 384-dot printer head.

## WiFi Relay Setup
1. Copy `src/wifi_config.example.h` to `src/wifi_config.h`.
2. Fill in `WHITEPAD_WIFI_SSID`, `WHITEPAD_WIFI_PASS`, and `WHITEPAD_RELAY_URL`.
3. If you want relay protection, also set `WHITEPAD_RELAY_TOKEN` in `src/wifi_config.h`.
4. Start the local relay:

```bash
WHITEPAD_RELAY_TOKEN=your-secret node relay/server.js
```

The ESP32 sends a compact `.wbm` 1-bit bitmap to `POST /api/sketches`. If `WHITEPAD_RELAY_TOKEN` is set, it adds `Authorization: Bearer <token>` to the request. The relay saves uploads in `relay/uploads/`; email forwarding can be added there without storing email credentials on the ESP32.

`WHITEPAD_RELAY_URL` can be either the full endpoint or just the base host/domain. If you point it at `https://printer.yourdomain.com`, the firmware will automatically post to `/api/sketches`.

The browser site now has a separate `Send to Relay` button that posts the rendered sticker to `POST /api/jobs`. That endpoint stores the job for later pickup, while `POST /api/sketches` stays token-protected for the ESP32 upload path.

If the website is hosted on GitHub Pages or another separate origin, set `WHITEPAD_CORS_ORIGIN` on the relay so the browser can call it, for example:

```bash
WHITEPAD_CORS_ORIGIN=https://printer.korgai.ink WHITEPAD_RELAY_TOKEN=your-secret node relay/server.js
```

When the ESP32 is switched to `EMAIL` mode, it will now poll `GET /api/jobs/next` every few seconds, download the next queued sketch, and print it over BLE automatically.

## ⚙️ Configuration
The pinout is pre-configured for the "Cheap Yellow Display" (CYD) 3.5" variant in `platformio.ini`. If your touch or screen orientation is inverted, you can adjust the `build_flags` in `platformio.ini` or the `tft.setRotation()` call in `src/ui.cpp`.
