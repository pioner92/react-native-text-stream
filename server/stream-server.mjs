// Local streaming server for the example app: node server/stream-server.mjs
//
// GET  /?fmt=raw|sse|ndjson&tps=<chunks/s, 0 = burst>&chunk=<bytes (raw) or chars>
//        &repeat=<paragraphs> | &bytes=<approximate body size>&id=<run id>
// GET  /log?id=<run id>          -> { total, events: [[sentAtMs, cumulativeBytes], ...] }
// POST /results?name=<file>      -> writes the JSON body to bench-results/<file>.json
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';

const PORT = Number(process.env.PORT) || 9877;
const RESULTS_DIR = path.join(path.dirname(new URL(import.meta.url).pathname), '..', 'bench-results');

// An LLM-style answer; the emoji are multi-byte, so raw chunks split them mid-character.
const PARAGRAPHS = [
  'Streaming text straight from the network into a native view ⚡️ Every token lands in a native buffer, and the view picks it up on the next frame.',
  'No bridge, no setState, no re-render. JavaScript hands the buffer over once and steps aside 🚀',
  'Under the hood: URLSession appends bytes to an append-only log, and a CADisplayLink on the main thread decodes only the new UTF-8 slice ✨',
  'Freeze the JS thread and watch: the native card keeps flowing while the JS card stops dead 🧊',
  'Each frame costs O(delta), not O(n). A 100 KB answer streams as smoothly as a 1 KB one 📈',
];

function buildText(q) {
  const targetBytes = Number(q.get('bytes') ?? 0);
  const out = [];
  let size = 0;
  for (let i = 0; targetBytes ? size < targetBytes : i < Number(q.get('repeat') ?? 20); i++) {
    const p = PARAGRAPHS[i % PARAGRAPHS.length] + '\n\n';
    out.push(p);
    size += Buffer.byteLength(p);
  }
  return out.join('');
}

// Send logs per run id, the reference clock for latency (sim and host share Date.now()).
const sendLogs = new Map();

function stream(req, res, q) {
  const tps = Number(q.get('tps') ?? 50);
  const size = Number(q.get('chunk') ?? 16);
  const fmt = q.get('fmt') ?? 'raw';
  const id = q.get('id');
  const text = buildText(q);
  const bytes = Buffer.from(text, 'utf8');
  const chars = Array.from(text);

  const log = { total: fmt === 'raw' ? bytes.length : null, events: [] };
  if (id) sendLogs.set(id, log);

  res.writeHead(200, {
    'Content-Type': fmt === 'sse' ? 'text/event-stream' : 'text/plain; charset=utf-8',
    'Cache-Control': 'no-cache',
    // Stops URLSession from buffering the first bytes for MIME sniffing.
    'X-Content-Type-Options': 'nosniff',
  });

  let i = 0;
  let sent = 0;
  let closed = false;
  req.on('close', () => (closed = true));

  // Writes the next chunk; returns false once everything is sent.
  const writeNext = () => {
    if (fmt === 'raw') {
      if (i >= bytes.length) return false;
      // Deliberately splits UTF-8 sequences across chunks.
      const piece = bytes.subarray(i, (i += size));
      sent += piece.length;
      log.events.push([Date.now(), sent]);
      return res.write(piece) ? true : 'drain';
    }
    if (i >= chars.length) return false;
    const content = chars.slice(i, (i += size)).join('');
    const json = JSON.stringify({ choices: [{ delta: { content } }] });
    const frame = fmt === 'sse' ? `data: ${json}\n\n` : json + '\n';
    sent += Buffer.byteLength(frame);
    log.events.push([Date.now(), sent]);
    return res.write(frame) ? true : 'drain';
  };

  const finish = () => {
    if (fmt === 'sse') res.write('data: [DONE]\n\n');
    res.end();
  };

  if (tps <= 0) {
    // Burst: as fast as the socket accepts, honoring backpressure.
    const pump = () => {
      while (!closed) {
        const r = writeNext();
        if (r === false) return finish();
        if (r === 'drain') return res.once('drain', pump);
      }
    };
    return pump();
  }

  const timer = setInterval(() => {
    if (closed || writeNext() === false) {
      clearInterval(timer);
      if (!closed) finish();
    }
  }, 1000 / tps);
  req.on('close', () => clearInterval(timer));
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const parts = [];
    req.on('data', (c) => parts.push(c));
    req.on('end', () => resolve(Buffer.concat(parts).toString('utf8')));
    req.on('error', reject);
  });
}

http
  .createServer(async (req, res) => {
    const url = new URL(req.url, 'http://x');
    const q = url.searchParams;

    if (url.pathname === '/log') {
      const log = sendLogs.get(q.get('id')) ?? { total: null, events: [] };
      res.writeHead(200, { 'Content-Type': 'application/json' });
      return res.end(JSON.stringify(log));
    }

    if (url.pathname === '/results' && req.method === 'POST') {
      const name = (q.get('name') ?? `bench-${Date.now()}`).replace(/[^\w.-]/g, '_');
      fs.mkdirSync(RESULTS_DIR, { recursive: true });
      const file = path.join(RESULTS_DIR, `${name}.json`);
      fs.writeFileSync(file, await readBody(req));
      console.log(`results -> ${file}`);
      res.writeHead(200, { 'Content-Type': 'application/json' });
      return res.end(JSON.stringify({ file }));
    }

    return stream(req, res, q);
  })
  .listen(PORT, '0.0.0.0', () => console.log(`stream server on http://0.0.0.0:${PORT}`));
