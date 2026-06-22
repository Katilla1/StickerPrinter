const fs = require("fs");
const http = require("http");
const path = require("path");
const { parseSketch, sketchToPngBuffer } = require("./wbm-png");

const PORT = Number(process.env.PORT || 8787);
const RELAY_TOKEN = process.env.WHITEPAD_RELAY_TOKEN || "";
const CORS_ORIGIN = process.env.WHITEPAD_CORS_ORIGIN || "*";
const UPLOAD_DIR = path.join(__dirname, "uploads");
const JOB_DIR = path.join(UPLOAD_DIR, "jobs");

fs.mkdirSync(UPLOAD_DIR, { recursive: true });
fs.mkdirSync(JOB_DIR, { recursive: true });

function readBody(req, limitBytes = 128 * 1024) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;

    req.on("data", (chunk) => {
      total += chunk.length;
      if (total > limitBytes) {
        reject(new Error("Payload too large"));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });

    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

function saveSketch(body, prefix) {
  const sketch = parseSketch(body);
  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  const ext = sketch.type === "color" ? "wpc" : "wbm";
  const file = path.join(prefix, `sketch-${stamp}.${ext}`);
  const pngFile = path.join(prefix, `sketch-${stamp}.png`);
  fs.writeFileSync(file, body);
  fs.writeFileSync(pngFile, sketchToPngBuffer(sketch));
  return { sketch, file, pngFile };
}

function listQueuedFiles() {
  return fs.readdirSync(JOB_DIR)
    .filter((name) => /\.wbm$/i.test(name))
    .sort();
}

function applyCors(res) {
  res.setHeader("Access-Control-Allow-Origin", CORS_ORIGIN);
  res.setHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Whitepad-Source, X-Whitepad-Format");
}

const server = http.createServer(async (req, res) => {
  applyCors(res);

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    return;
  }

  if (req.method === "GET" && (req.url === "/" || req.url === "/api/sketches")) {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("White Pad relay OK\n");
    return;
  }

  if (req.method === "GET" && req.url === "/api/jobs") {
    const files = listQueuedFiles().slice(-24).reverse();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ jobs: files }));
    return;
  }

  if (req.method === "GET" && req.url === "/api/jobs/next") {
    if (RELAY_TOKEN) {
      const auth = req.headers.authorization || "";
      const token = auth.startsWith("Bearer ") ? auth.slice(7).trim() : "";
      if (token !== RELAY_TOKEN) {
        res.writeHead(401, { "Content-Type": "text/plain" });
        res.end("Unauthorized\n");
        return;
      }
    }

    const files = listQueuedFiles();
    if (files.length === 0) {
      res.writeHead(204, { "Content-Length": "0" });
      res.end();
      return;
    }

    const file = path.join(JOB_DIR, files[0]);
    const pngFile = file.replace(/\.wbm$/i, ".png");
    const body = fs.readFileSync(file);
    fs.unlinkSync(file);
    if (fs.existsSync(pngFile)) fs.unlinkSync(pngFile);

    res.writeHead(200, {
      "Content-Type": "application/octet-stream",
      "Content-Length": body.length,
    });
    res.end(body);
    return;
  }

  if (req.method !== "POST" || req.url !== "/api/sketches") {
    if (req.method === "POST" && req.url === "/api/jobs") {
      try {
        const body = await readBody(req);
        const { file, pngFile } = saveSketch(body, JOB_DIR);
        console.log(`Queued ${path.basename(file)} and ${path.basename(pngFile)} (${body.length} bytes)`);
        res.writeHead(200, {
          "Connection": "close",
          "Content-Length": "0",
        });
        res.end();
      } catch (error) {
        res.writeHead(400, { "Content-Type": "text/plain" });
        res.end(`${error.message}\n`);
      }
      return;
    }

    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not found\n");
    return;
  }

  if (RELAY_TOKEN) {
    const auth = req.headers.authorization || "";
    const token = auth.startsWith("Bearer ") ? auth.slice(7).trim() : "";
    if (token !== RELAY_TOKEN) {
      res.writeHead(401, { "Content-Type": "text/plain" });
      res.end("Unauthorized\n");
      return;
    }
  }

  try {
    const body = await readBody(req);
    try {
      const { file, pngFile } = saveSketch(body, UPLOAD_DIR);
      console.log(`Saved ${path.basename(file)} and ${path.basename(pngFile)} (${body.length} bytes)`);
      res.writeHead(200, {
        "Connection": "close",
        "Content-Length": "0",
      });
      res.end();
    } catch {
      res.writeHead(400, { "Content-Type": "text/plain" });
      res.end("Invalid White Pad sketch\n");
      return;
    }
  } catch (error) {
    res.writeHead(500, { "Content-Type": "text/plain" });
    res.end(`${error.message}\n`);
  }
});

server.listen(PORT, "0.0.0.0", () => {
  console.log(`White Pad relay listening on http://0.0.0.0:${PORT}`);
  if (RELAY_TOKEN) {
    console.log("White Pad relay token auth enabled");
  }
  console.log(`White Pad relay CORS origin: ${CORS_ORIGIN}`);
});
