# D21 Cloud Sticker Printer 🖨️✨

A high-performance, reverse-engineered web application for the **D21 Thermal Printer** (Zhuhai Jiuyin protocol). This app allows for local Bluetooth printing and remote P2P printing using PeerJS.

## 🌟 Key Features
- **Native D21 Protocol:** Fully reverse-engineered handshake and raster streaming for 100% hardware compatibility.
- **AI Sketch ✨:** Integrated ONNX line-art model to transform photos into clean printable outlines.
- **Remote Printing:** Start a session, share a link, and let friends send stickers directly to your printer from anywhere.
- **Auto-Crop:** Automatically trims whitespace to minimize paper waste.
- **Pro Features:** Live battery monitoring, adjustable print density, dithering, and custom thresholding.
- **Zero-Install:** Runs entirely in the browser via Web Bluetooth.

## 🚀 Getting Started
1. **Live App:** Visit `https://printer.korgai.ink` (or your deployed URL).
2. **Connect:** Ensure your D21 is ON and not paired with any other device. Click **Connect Printer**.
3. **Design:** Type text or upload an image. Use the **AI Sketch** button for the best results with photos.
4. **Print:** Hit **Print Sticker**.

## 🌐 Remote Printing
1. Go to the **Remote** tab.
2. Click **Start Remote Session**.
3. Copy the unique link and send it to a friend.
4. When they send a design, it will appear in your **Incoming Prints** queue.
5. **Preview** their design to verify it, then click **Print**.

## 🛠️ Technical Details
- **Protocol:** Zhuhai Jiuyin ( proprietary 51 78 wrapper).
- **Communication:** Web Bluetooth API (GATT).
- **Networking:** PeerJS (WebRTC) for peer-to-peer job transfers.
- **Image Processing:** HTML5 Canvas + ONNX Runtime for AI line-art extraction.

## 📜 Legal & Safety
- **Privacy:** No data is stored on any server. Jobs are transferred directly between peers.
- **Compatibility:** Designed specifically for D21-series thermal printers.

---
*Built with ❤️ for the thermal printing community.*
