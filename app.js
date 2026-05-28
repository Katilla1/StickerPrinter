const SERVICE_UUID = 0xFF00;
const WRITE_CHARACTERISTIC_UUID = 0xFF02;
const PACKET_SIZE_BYTES = 64;

// --- UI Elements ---
const connectBtn = document.getElementById('connectBtn');
const testPrintBtn = document.getElementById('testPrintBtn');
const printBtn = document.getElementById('printBtn');
const simpleModeBtn = document.getElementById('simpleModeBtn');
const advancedModeBtn = document.getElementById('advancedModeBtn');
const remoteModeBtn = document.getElementById('remoteModeBtn');
const lineArtBtn = document.getElementById('lineArtBtn');
const photoDetailBtn = document.getElementById('photoDetailBtn');
const aiSketchBtn = document.getElementById('aiSketchBtn');
const disconnectBtn = document.getElementById('disconnectBtn');
const statusEl = document.getElementById('status');
const batteryStatEl = document.getElementById('batteryStat');
const logEl = document.getElementById('log');
const messageInput = document.getElementById('messageInput');
const imageInput = document.getElementById('imageInput');
const clearImageBtn = document.getElementById('clearImageBtn');
const widthInput = document.getElementById('widthInput');
const lengthInput = document.getElementById('lengthInput');
const fontSizeInput = document.getElementById('fontSizeInput');
const imageScaleInput = document.getElementById('imageScaleInput');
const textPositionInput = document.getElementById('textPositionInput');
const imageFitInput = document.getElementById('imageFitInput');
const thresholdInput = document.getElementById('thresholdInput');
const contrastInput = document.getElementById('contrastInput');
const ditherInput = document.getElementById('ditherInput');
const invertInput = document.getElementById('invertInput');
const densityInput = document.getElementById('densityInput');
const labelModeInput = document.getElementById('labelModeInput');
const previewCanvas = document.getElementById('preview');
const previewLoading = document.getElementById('previewLoading');

// --- Remote / Networking ---
const editorPanel = document.getElementById('editorPanel');
const remotePanel = document.getElementById('remotePanel');
const startRemoteBtn = document.getElementById('startRemoteBtn');
const stopRemoteBtn = document.getElementById('stopRemoteBtn');
const remoteStatus = document.getElementById('remoteStatus');
const sessionInfo = document.getElementById('sessionInfo');
const shareLink = document.getElementById('shareLink');
const remoteQueue = document.getElementById('remoteQueue');
const remoteQueuePanel = document.getElementById('remoteQueuePanel');
const clearQueueBtn = document.getElementById('clearQueueBtn');
const clearQueueBtnTop = document.getElementById('clearQueueBtnTop');

// --- Application State ---
let compositionImage = null;
let aiSketchImage = null;
let advancedMode = false;
let simpleImageStyle = 'line-art';
let onnxSessionPromise = null;
let peer = null;
let connections = [];
let isGuest = false;
let hostPeerId = null;

/**
 * Core Printer API: Handles Bluetooth GATT communication
 */
const printerApi = {
    device: null,
    server: null,
    service: null,
    characteristic: null,
    isBusy: false,
    isReady: false,

    async connect() {
        if (!navigator.bluetooth) throw new Error('Web Bluetooth not supported.');
        
        try {
            this.device = await navigator.bluetooth.requestDevice({
                filters: [{ namePrefix: 'D21' }],
                optionalServices: [SERVICE_UUID],
            });

            this.device.addEventListener('gattserverdisconnected', () => {
                this.reset();
                setStatus('Printer disconnected.');
            });

            setStatus('Connecting...');
            this.server = await this.device.gatt.connect();
            this.service = await this.server.getPrimaryService(SERVICE_UUID);
            this.characteristic = await this.service.getCharacteristic(WRITE_CHARACTERISTIC_UUID);
            
            await this.characteristic.startNotifications();
            this.characteristic.addEventListener('characteristicvaluechanged', (e) => {
                const bytes = new Uint8Array(e.target.value.buffer);
                const hex = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('-');
                
                // Battery Decoding
                if (bytes.length === 4 && bytes[0] === 0x1C && bytes[1] === 0x04) {
                    if (batteryStatEl) batteryStatEl.textContent = `${bytes[2]}%`;
                }

                // Ready Check
                if (hex === "1c-38-0d") {
                    setStatus("Printer Ready!");
                    this.isReady = true;
                }
                appendLog(`Printer Status: ${hex}`);
            });

            setStatus(`Connected to ${this.device.name}`);
            return this;
        } catch (e) {
            setStatus(`Connection failed: ${e.message}`);
            throw e;
        }
    },

    reset() {
        this.server = null;
        this.service = null;
        this.characteristic = null;
        this.isBusy = false;
        this.isReady = false;
    },

    async disconnect() {
        if (this.device?.gatt?.connected) this.device.gatt.disconnect();
        this.reset();
    },

    async printComposition(preRenderedRaster = null) {
        if (this.isBusy) {
            setStatus("Printer busy, please wait...");
            return;
        }
        
        if (!this.characteristic) await this.connect();
        
        this.isBusy = true;
        this.isReady = false;
        try {
            const raster = preRenderedRaster || renderComposition();
            const density = parseInt(densityInput?.value) || 2;

            // --- AUTO-CROP ---
            let firstRow = -1, lastRow = -1;
            for (let y = 0; y < raster.rows; y++) {
                const rowOffset = y * raster.bytesPerRow;
                const hasContent = raster.data.slice(rowOffset, rowOffset + raster.bytesPerRow).some(b => b !== 0);
                if (hasContent) {
                    if (firstRow === -1) firstRow = y;
                    lastRow = y;
                }
            }

            if (firstRow === -1) { firstRow = 0; lastRow = 40; }
            const startY = Math.max(0, firstRow - 8);
            const endY = Math.min(raster.rows, lastRow + 8);
            const printableRows = endY - startY;
            const printableData = raster.data.slice(startY * raster.bytesPerRow, endY * raster.bytesPerRow);

            setStatus('Initializing Handshake...');
            const handshake = [
                new Uint8Array([0x10, 0xFF, 0xF1, 0x03]), 
                new Uint8Array([0x10, 0xFF, 0x30, 0x11]), 
                new Uint8Array(12),                       
                new Uint8Array([0x10, 0xFF, 0x10, 0x00, density]), 
                new Uint8Array([0x1B, 0x40])              
            ];
            for (const cmd of handshake) {
                await writePacket(this.characteristic, cmd);
                await new Promise(r => setTimeout(r, 150));
            }

            let timeout = 0;
            while (!this.isReady && timeout < 20) {
                await new Promise(r => setTimeout(r, 100));
                timeout++;
            }

            setStatus('Streaming Content...');
            const header = new Uint8Array([0x1D, 0x76, 0x30, 0x00, 48, 0x00, printableRows & 0xFF, (printableRows >> 8) & 0xFF]);
            await writePacket(this.characteristic, header);
            await new Promise(r => setTimeout(r, 100));

            for (let y = 0; y < printableRows; y++) {
                const row = printableData.slice(y * raster.bytesPerRow, (y + 1) * raster.bytesPerRow);
                await writePacket(this.characteristic, row);
                await new Promise(r => setTimeout(r, 20)); 
            }

            setStatus('Finalizing...');
            const finalize = [
                new Uint8Array([0x1B, 0x4A, 0x30]),       
                new Uint8Array([0x10, 0xFF, 0xF1, 0x45])  
            ];
            for (const cmd of finalize) {
                await writePacket(this.characteristic, cmd);
                await new Promise(r => setTimeout(r, 150));
            }

            setStatus('Print complete!');
        } catch (e) {
            setStatus(`Print error: ${e.message}`);
            throw e;
        } finally {
            this.isBusy = false;
        }
    },

    async printRawText(text = "D21 QUICK TEST") {
        if (this.isBusy) return setStatus("Busy...");
        const oldVal = messageInput.value;
        const oldImage = compositionImage;
        const oldAi = aiSketchImage;
        
        messageInput.value = text + "\n" + new Date().toLocaleTimeString();
        compositionImage = null;
        aiSketchImage = null;
        
        try {
            await this.printComposition();
        } finally {
            messageInput.value = oldVal;
            compositionImage = oldImage;
            aiSketchImage = oldAi;
            renderComposition();
        }
    }
};

// --- Remote Networking (PeerJS) ---
function initPeer(id = null) {
    if (peer) peer.destroy();
    peer = new Peer(id);
    
    peer.on('open', (id) => {
        if (isGuest) {
            setStatus('Connecting to host...');
            setupConnection(peer.connect(hostPeerId));
        } else {
            remoteStatus.textContent = 'Session Live!';
            sessionInfo.classList.remove('hidden');
            const url = new URL(window.location.href);
            url.searchParams.set('peer', id);
            shareLink.textContent = url.toString();
        }
    });

    peer.on('connection', (conn) => {
        setupConnection(conn);
        appendLog(`New remote connection: ${conn.peer}`);
    });

    peer.on('error', (err) => {
        setStatus(`Remote Error: ${err.type}`);
    });
}

function setupConnection(conn) {
    conn.on('data', (data) => {
        if (data.type === 'print') {
            const job = { ...data, sender: conn.peer, timestamp: new Date().toLocaleTimeString() };
            addJobToQueue(job);
            appendLog(`Received job from ${conn.peer}`);
        }
    });
    connections.push(conn);
}

function addJobToQueue(job) {
    const jobEl = document.createElement('div');
    jobEl.className = 'panel';
    jobEl.style.padding = '10px';
    jobEl.innerHTML = `
        <div style="font-size: 0.8rem; font-weight: bold;">From: ${job.sender} <span style="font-weight: normal; opacity: 0.6;">${job.timestamp}</span></div>
        <div style="font-size: 0.9rem; margin: 4px 0;">${job.text || '(No text)'}</div>
        <div class="row" style="gap: 8px;">
            <button class="secondary" style="padding: 4px 12px; font-size: 0.8rem; flex: 1;">Preview</button>
            <button class="primary" style="padding: 4px 12px; font-size: 0.8rem; flex: 1;">Print</button>
        </div>
    `;
    
    const [prevBtn, prntBtn] = jobEl.querySelectorAll('button');

    const loadImg = () => new Promise(res => {
        if (!job.image) return res(null);
        const img = new Image();
        img.onload = () => res(img);
        img.src = job.image;
    });

    prevBtn.onclick = async () => {
        const img = await loadImg();
        renderComposition({ ...job.settings, image: img, aiImage: null });
        setStatus(`Previewing job from ${job.sender}`);
    };

    prntBtn.onclick = async () => {
        const img = await loadImg();
        const raster = renderComposition({ ...job.settings, image: img, aiImage: null });
        try { await printerApi.printComposition(raster); } catch(e) {}
        renderComposition();
    };
    
    remoteQueue.prepend(jobEl);
    if (remoteQueue.querySelector('p')) remoteQueue.querySelector('p').remove();
}

// --- Core Rendering & Processing ---
function renderComposition(customSettings = null) {
    const s = {
        width: Number(widthInput?.value) || 384,
        height: Number(lengthInput?.value) || 240,
        text: messageInput?.value || '',
        threshold: Number(thresholdInput?.value) || 128,
        contrast: Number(contrastInput?.value) || 0,
        invert: invertInput?.checked || false,
        dither: ditherInput?.checked || false,
        imageFit: imageFitInput?.value || 'contain',
        image: compositionImage,
        aiImage: aiSketchImage,
        fontSize: Number(fontSizeInput?.value) || 24,
        imageScale: Number(imageScaleInput?.value || 100) / 100,
        textPosition: textPositionInput?.value || 'center',
        advancedMode: !!advancedMode,
        simpleImageStyle: simpleImageStyle || 'line-art',
        ...(customSettings || {})
    };

    if (isNaN(s.width) || isNaN(s.height)) {
        appendLog(`Error: Invalid dimensions ${s.width}x${s.height}`);
        return null;
    }

    previewCanvas.width = s.width;
    previewCanvas.height = s.height;
    const ctx = previewCanvas.getContext('2d', { willReadFrequently: true });
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, s.width, s.height);

    const source = s.aiImage || s.image;
    if (source && source.width > 0) {
        let scale = s.imageFit === 'cover' 
            ? Math.max(s.width / source.width, s.height / source.height)
            : Math.min(s.width / source.width, s.height / source.height);
        
        scale *= s.imageScale;
        if (isNaN(scale)) scale = 1;

        const dw = source.width * scale, dh = source.height * scale;
        ctx.drawImage(source, (s.width - dw) / 2, (s.height - dh) / 2, dw, dh);
        
        if (!s.advancedMode && !s.aiImage) {
            applyTreatment(ctx, s.width, s.height, s.simpleImageStyle);
        }
    }

    if (s.text) {
        ctx.font = `bold ${s.fontSize}px ui-monospace, "Cascadia Code", "Source Code Pro", Menlo, Consolas, monospace`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.strokeStyle = 'white';
        ctx.lineWidth = s.fontSize/10;
        ctx.fillStyle = 'black';
        const lines = s.text.split('\n');
        const lineHeight = s.fontSize * 1.1;
        const totalHeight = lines.length * lineHeight;
        const yStart = s.textPosition === 'top' ? s.fontSize : s.textPosition === 'bottom' ? s.height - totalHeight + (s.fontSize/2) : (s.height - totalHeight + lineHeight)/2;
        
        lines.forEach((line, i) => {
            const y = yStart + i * lineHeight;
            ctx.strokeText(line, s.width/2, y); 
            ctx.fillText(line, s.width/2, y);
        });
    }
    
    const imgData = ctx.getImageData(0, 0, s.width, s.height);
    const data = imgData.data;
    const bytesPerRow = Math.ceil(s.width / 8);
    const raster = new Uint8Array(bytesPerRow * s.height);
    const lum = new Float32Array(s.width * s.height);
    
    const c = s.contrast;
    const contrastFactor = (259 * (c + 255)) / (255 * (259 - c));
    
    for (let i = 0; i < lum.length; i++) {
        let v = data[i*4] * 0.299 + data[i*4+1] * 0.587 + data[i*4+2] * 0.114;
        if (c !== 0) v = contrastFactor * (v - 128) + 128;
        lum[i] = Math.max(0, Math.min(255, v));
    }

    if (s.advancedMode && s.dither) {
        for (let y = 0; y < s.height; y++) {
            for (let x = 0; x < s.width; x++) {
                const i = y * s.width + x;
                const old = lum[i], v = old < s.threshold ? 0 : 255, err = old - v;
                lum[i] = v;
                if (x+1 < s.width) lum[i+1] += err * 7/16;
                if (y+1 < s.height && x > 0) lum[i+s.width-1] += err * 3/16;
                if (y+1 < s.height) lum[i+s.width] += err * 5/16;
                if (y+1 < s.height && x+1 < s.width) lum[i+s.width+1] += err * 1/16;
            }
        }
    }

    for (let y = 0; y < s.height; y++) {
        for (let bx = 0; bx < bytesPerRow; bx++) {
            let byte = 0;
            for (let bit = 0; bit < 8; bit++) {
                const x = bx * 8 + bit;
                if (x >= s.width) continue;
                const i = y * s.width + x;
                const isBlack = lum[i] < s.threshold;
                const finalBlack = s.invert ? !isBlack : isBlack;
                if (finalBlack) byte |= (1 << (7 - bit));
                const col = finalBlack ? 0 : 255;
                data[i*4] = data[i*4+1] = data[i*4+2] = col;
                data[i*4+3] = 255;
            }
            raster[y * bytesPerRow + bx] = byte;
        }
    }
    ctx.putImageData(imgData, 0, 0);
    return { data: raster, width: s.width, rows: s.height, bytesPerRow };
}

function applyTreatment(ctx, w, h, style) {
    const imgData = ctx.getImageData(0, 0, w, h);
    if (style === 'line-art') {
        const blurred = boxBlur(extractLum(imgData.data), w, h, 1);
        for (let i = 0; i < blurred.length; i++) {
            const x = i % w, y = Math.floor(i / w);
            if (x === 0 || x === w - 1 || y === 0 || y === h - 1) continue;
            const grad = Math.abs(blurred[i+1] - blurred[i-1]) + Math.abs(blurred[i+w] - blurred[i-w]);
            const val = (grad > 30 || blurred[i] < 80) ? 0 : 255;
            imgData.data[i*4] = imgData.data[i*4+1] = imgData.data[i*4+2] = val;
        }
    } else if (style === 'photo') {
        const lum = extractLum(imgData.data);
        for (let y = 0; y < h; y++) {
            for (let x = 0; x < w; x++) {
                const i = y * w + x, old = lum[i], v = old < 128 ? 0 : 255, err = (old - v) / 8;
                lum[i] = v;
                if (x+1 < w) lum[i+1] += err; if (x+2 < w) lum[i+2] += err;
                if (y+1 < h && x > 0) lum[i+w-1] += err; if (y+1 < h) lum[i+w] += err;
                if (y+1 < h && x+1 < w) lum[i+w+1] += err; if (y+2 < h) lum[i+w*2] += err;
            }
        }
        for (let i = 0; i < lum.length; i++) imgData.data[i*4] = imgData.data[i*4+1] = imgData.data[i*4+2] = lum[i];
    }
    ctx.putImageData(imgData, 0, 0);
}

function extractLum(rgba) {
    const lum = new Float32Array(rgba.length / 4);
    for (let i = 0; i < lum.length; i++) lum[i] = rgba[i*4]*0.299 + rgba[i*4+1]*0.587 + rgba[i*4+2]*0.114;
    return lum;
}

function boxBlur(data, w, h, r) {
    const out = new Float32Array(data.length);
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            let sum = 0, count = 0;
            for (let sy = y-r; sy <= y+r; sy++) {
                for (let sx = x-r; sx <= x+r; sx++) {
                    if (sx >= 0 && sx < w && sy >= 0 && sy < h) { sum += data[sy*w + sx]; count++; }
                }
            }
            out[y*w + x] = sum / count;
        }
    }
    return out;
}

// --- AI Logic ---
async function generateAiSketch() {
    if (!compositionImage) return setStatus('Upload image first.');
    previewLoading.classList.remove('hidden');
    setStatus('Running AI Sketch...');
    try {
        if (!onnxSessionPromise) {
            if (!window.ort) await loadScript('https://cdn.jsdelivr.net/npm/onnxruntime-web@1.14.0/dist/ort.min.js');
            onnxSessionPromise = window.ort.InferenceSession.create('https://huggingface.co/rocca/informative-drawings-line-art-onnx/resolve/main/model.onnx', { executionProviders: ['wasm'] });
        }
        const session = await onnxSessionPromise;
        const w = 384, h = 384;
        const canvas = document.createElement('canvas'); canvas.width = w; canvas.height = h;
        const ctx = canvas.getContext('2d', { willReadFrequently: true });
        ctx.drawImage(compositionImage, 0, 0, w, h);
        const rgba = ctx.getImageData(0, 0, w, h).data, input = new Float32Array(w * h * 3);
        for (let i = 0; i < w * h; i++) {
            input[i] = rgba[i*4]/255; input[w*h + i] = rgba[i*4+1]/255; input[2*w*h + i] = rgba[i*4+2]/255;
        }
        const results = await session.run({ input: new window.ort.Tensor('float32', input, [1, 3, h, w]) });
        const output = results.output.data, outData = new Uint8ClampedArray(w * h * 4);
        for (let i = 0; i < w * h; i++) {
            const v = output[i] * 255; outData[i*4] = outData[i*4+1] = outData[i*4+2] = v; outData[i*4+3] = 255;
        }
        const outCanvas = document.createElement('canvas'); outCanvas.width = w; outCanvas.height = h;
        outCanvas.getContext('2d').putImageData(new ImageData(outData, w, h), 0, 0);
        aiSketchImage = outCanvas; renderComposition(); setStatus('AI Sketch complete!');
    } catch (e) { setStatus(`AI Error: ${e.message}`); } finally { previewLoading.classList.add('hidden'); }
}

// --- Interaction Handlers ---
function setMode(mode) {
    document.body.className = `${mode}-mode`;
    [simpleModeBtn, advancedModeBtn, remoteModeBtn].forEach(b => b.classList.toggle('active', b.id.startsWith(mode)));
    editorPanel.classList.toggle('hidden', mode === 'remote');
    remotePanel.classList.toggle('hidden', mode !== 'remote');
    advancedMode = (mode === 'advanced'); renderComposition();
}

function setStatus(msg) { statusEl.textContent = msg; appendLog(msg); }
function appendLog(msg) { logEl.textContent += `[${new Date().toLocaleTimeString()}] ${msg}\n`; logEl.scrollTop = logEl.scrollHeight; }

connectBtn.onclick = () => printerApi.connect();
testPrintBtn.onclick = () => printerApi.printRawText();
disconnectBtn.onclick = () => { printerApi.disconnect(); setStatus('State reset.'); };
printBtn.onclick = () => {
    if (isGuest) {
        const settings = {
            text: messageInput.value,
            threshold: parseInt(thresholdInput.value),
            contrast: parseInt(contrastInput?.value || 0),
            invert: invertInput?.checked,
            dither: ditherInput?.checked,
            imageFit: imageFitInput?.value,
            fontSize: parseInt(fontSizeInput.value),
            imageScale: parseInt(imageScaleInput?.value || 100) / 100,
            textPosition: textPositionInput.value,
            width: parseInt(widthInput.value),
            height: parseInt(lengthInput.value),
            advancedMode: advancedMode,
            simpleImageStyle: simpleImageStyle,
            density: parseInt(densityInput?.value) || 1,
            labelMode: labelModeInput?.checked
        };
        const img = aiSketchImage || compositionImage ? previewCanvas.toDataURL('image/png') : null;
        connections.forEach(c => c.send({ type: 'print', text: messageInput.value, image: img, settings }));
        setStatus('Sent to host!');
    } else printerApi.printComposition();
};

simpleModeBtn.onclick = () => setMode('simple');
advancedModeBtn.onclick = () => setMode('advanced');
remoteModeBtn.onclick = () => setMode('remote');
lineArtBtn.onclick = () => { simpleImageStyle = 'line-art'; aiSketchImage = null; renderComposition(); };
photoDetailBtn.onclick = () => { simpleImageStyle = 'photo'; aiSketchImage = null; renderComposition(); };
aiSketchBtn.onclick = () => generateAiSketch();
clearQueueBtn.onclick = clearQueueBtnTop.onclick = () => { remoteQueue.innerHTML = '<p style="font-size: 0.8rem;">No incoming jobs.</p>'; };

imageInput.onchange = (e) => {
    const file = e.target.files[0];
    if (file) {
        const img = new Image();
        img.onload = () => { compositionImage = img; aiSketchImage = null; renderComposition(); };
        img.src = URL.createObjectURL(file);
    }
};

clearImageBtn.onclick = () => { compositionImage = null; aiSketchImage = null; imageInput.value = ''; renderComposition(); };

document.addEventListener('paste', (e) => {
    const item = Array.from(e.clipboardData.items).find(x => x.type.startsWith('image'));
    if (item) {
        const img = new Image();
        img.onload = () => { compositionImage = img; aiSketchImage = null; renderComposition(); setStatus('Pasted image loaded.'); };
        img.src = URL.createObjectURL(item.getAsFile());
    }
});

[messageInput, widthInput, lengthInput, fontSizeInput, imageScaleInput, textPositionInput, thresholdInput, contrastInput, imageFitInput, densityInput, ditherInput, invertInput, labelModeInput].forEach(el => {
    if (el) el.oninput = () => renderComposition();
});

startRemoteBtn.onclick = () => initPeer();
stopRemoteBtn.onclick = () => { peer.destroy(); sessionInfo.classList.add('hidden'); remoteStatus.textContent = 'Session stopped.'; };
shareLink.onclick = () => { navigator.clipboard.writeText(shareLink.textContent); setStatus('Link copied!'); };

// --- Init ---
const params = new URLSearchParams(window.location.search);
if (params.has('peer')) {
    isGuest = true; hostPeerId = params.get('peer');
    document.body.classList.add('guest-mode');
    initPeer();
    printBtn.textContent = 'Send to Printer';
}

function loadScript(url) {
    return new Promise((res, rej) => {
        const s = document.createElement('script'); s.src = url; s.onload = res; s.onerror = rej; document.head.appendChild(s);
    });
}

async function writePacket(char, data) {
    if (char.properties.writeWithoutResponse) await char.writeValueWithoutResponse(data);
    else await char.writeValueWithResponse(data);
}
