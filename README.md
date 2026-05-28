# StickerPrinter 🖨️✨
### The Ultimate Web-to-Hardware Thermal Printing Suite

A high-performance, browser-native application for the **D21 Thermal Printer**. This suite combines reverse-engineered hardware protocols with advanced computer vision and P2P networking to create a seamless, collaborative sticker-printing experience.

## 🚀 Key Features

### 🎨 Creative Studio
- **Integrated Drawing Pad:** Draw directly on the screen with your cursor or finger.
- **AI Sketch ✨:** Transform complex photos into clean line art using on-device AI (ONNX Runtime).
- **Auto ASCII Art 🔠:** Intelligent image-to-text conversion with monospace alignment.
- **Professional Image Processing:**
    - **Adaptive Line Art:** Extracts ink from paper even in uneven lighting.
    - **High Pass Filter ⚡️:** Professional lighting normalization for clean document extraction.
    - **Note Clean 📝:** Color-distance algorithm for isolating ink from vibrant Post-it notes.
    - **Auto-Threshold (Otsu's Method):** Mathematical one-click "magic" contrast adjustment.

### 🌐 Collaborative Printing
- **Remote Host/Guest Sessions:** Share a link and let anyone in the world send stickers to your printer via PeerJS (P2P).
- **Shared Artistic Intent:** All image treatments, scaling, and font settings are synced between peers.
- **Job Queue:** Host can preview, adjust, and approve incoming remote jobs before printing.

### ⚙️ Hardware Integration
- **Direct Bluetooth Control:** Zero-install connection via Web Bluetooth API.
- **Native D21 Protocol:** Full handshake, battery monitoring, and density control.
- **Precision Sizing:** Real-time Font Size and Image Scaling sliders.
- **Clipboard Integration:** Paste screenshots or copied images directly into the workspace.

## 🛠️ Tech Stack
- **Frontend:** Vanilla JS / HTML5 / CSS3
- **Hardware:** Web Bluetooth API (GATT)
- **Networking:** PeerJS (WebRTC) for secure P2P transfers.
- **AI/ML:** ONNX Runtime Web for on-device inference.
- **Processing:** HTML5 Canvas API for real-time rasterization.

## 🏃‍♂️ Getting Started
1. Open the app in a Chrome-based browser.
2. Click **Connect Printer** and select your `D21` device.
3. Design your sticker using the **Text**, **Image**, or **Draw** tools.
4. Use **Simple** mode for quick edits or **Advanced** for precise control over threshold, dithering, and density.
5. Hit **Print Sticker**!

## 📜 Development & Protocol
This project reverse-engineers the Zhuhai Jiuyin thermal protocol. See `DEVELOPMENT.md` for a deep dive into the handshake, packet structure, and raster streaming logic.

---
*Built for precision and creativity. Ready for 203 DPI excellence.*
