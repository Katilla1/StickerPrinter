# Development & Architecture Guide 🛠️

This guide is intended for developers (and AI agents) working on the StickerPrinter codebase.

## 🏗️ Core Architecture

### 1. Hardware Communication (`printerApi`)
The D21 printer uses the **Zhuhai Jiuyin** protocol over Bluetooth Low Energy (BLE).
- **Service UUID:** `0xFF00`
- **Write Characteristic:** `0xFF02` (Supports both Write with and without response).
- **Packet Size:** 64 bytes max.
- **Handshake Sequence:**
    - Initialization: `10 FF F1 03` -> `10 FF 30 11` -> `12 zero bytes` -> `10 FF 10 00 [Density]` -> `1B 40`.
    - Content Header: `1D 76 30 00 48 00 [Height Low] [Height High]`.
    - Raster Streaming: Stream 48 bytes per row (384 pixels).

### 2. The Rendering Pipeline (`renderComposition`)
The rendering process is a multi-stage pipeline:
1. **Source Selection:** Picks between AI Sketch, uploaded Image, or the Drawing Layer.
2. **Transform:** Applies `imageScale` and `imageFit` (contain/cover).
3. **Treatment:** Applies complex filters (Adaptive Thresholding, Note Clean, High Pass, or Photo Dithering).
4. **Overlay:** Renders text using a monospace font stack for ASCII art compatibility.
5. **Normalization:** Converts RGB to Grayscale, applies Contrast, and forces Alpha to `255` (Opaque).
6. **Quantization:** Binarizes the image into 1-bit black and white (0 or 255).
7. **Rasterization:** Packs 8 pixels into 1 byte for the printer.

### 3. Networking (`PeerJS`)
- **Sessions:** Host starts a PeerJS ID. Guest connects using the `?peer=ID` URL param.
- **Data Transfer:** Jobs are sent as JSON objects containing:
    - `text`: The message/ASCII art.
    - `image`: Base64 encoded PNG of the preview (to preserve the sender's artistic treatments).
    - `settings`: Full object of all sliders and mode selections.

## 🧪 Testing & Diagnostics
- Use the **Advanced Mode** to see the live system log.
- Raw Hex can be sent directly via the `diagControls` panel for protocol testing.
- `app.js` includes robust `Number()` conversions and `NaN` checks to prevent rendering crashes.

## 📝 Coding Standards
- **Pure JS:** Minimize external dependencies. Currently only `PeerJS` and `ONNX Runtime`.
- **Surgical Edits:** When modifying `app.js`, preserve the logic flow: UI Consts -> Networking -> Rendering -> Interaction Handlers.
- **D21 Focus:** Ensure all changes maintain compatibility with the 384px width constraint.
