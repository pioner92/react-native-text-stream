// Local streaming server for the example app: node server/stream-server.mjs
//
// GET  /?fmt=raw|sse|ndjson&tps=<chunks/s, 0 = burst>&chunk=<bytes (raw) or chars>
//        &repeat=<paragraphs> | &bytes=<approximate body size>&id=<run id>&para=<paragraph bytes>
// GET  /log?id=<run id>          -> { total, events: [[sentAtMs, cumulativeBytes], ...] }
// POST /results?name=<file>      -> writes the JSON body to bench-results/<file>.json (never overwrites)
//
// Listens on loopback only, which the iOS simulator shares with the host. For a device on the LAN:
// HOST=0.0.0.0 node server/stream-server.mjs
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';

const PORT = Number(process.env.PORT) || 9877;
const HOSTS = process.env.HOST ? [process.env.HOST] : ['127.0.0.1', '::1'];
const RESULTS_DIR = path.join(path.dirname(new URL(import.meta.url).pathname), '..', 'bench-results');

const MiB = 1024 * 1024;
const MAX_TEXT_BYTES = 64 * MiB;
const MAX_RESULTS_BYTES = 16 * MiB;
// Send logs kept for /log, oldest dropped first.
const MAX_SEND_LOGS = 100;

// An LLM-style answer; the emoji are multi-byte, so raw chunks split them mid-character.
const PARAGRAPHS = [
  'Streaming text straight from the network into a native view ⚡️ Every token lands in a native buffer, and the view picks it up on the next frame.',
  'No bridge, no setState, no re-render. JavaScript hands the buffer over once and steps aside 🚀',
  'Under the hood: the network layer appends bytes to an append-only log, and the view decodes only the new UTF-8 slice on the next frame ✨',
  'Freeze the JS thread and watch: the native card keeps flowing while the JS card stops dead 🧊',
  'Each frame costs O(delta), not O(n). A 100 KB answer streams as smoothly as a 1 KB one 📈',
];

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

/** A finite number in [min, max], or `fallback` when the parameter is absent. */
function param(q, name, fallback, min, max) {
  const raw = q.get(name);
  if (raw === null || raw === '') return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || n < min || n > max) {
    throw new HttpError(400, `${name} must be a number from ${min} to ${max}`);
  }
  return n;
}

function buildText({ targetBytes, repeat, paraBytes }) {
  const out = [];
  let size = 0;
  // para=<bytes>: merge sentences into paragraphs of about this size (0 = one per sentence).
  let inPara = 0;
  for (let i = 0; targetBytes ? size < targetBytes : i < repeat; i++) {
    const sentence = PARAGRAPHS[i % PARAGRAPHS.length];
    inPara += Buffer.byteLength(sentence);
    const breakHere = !paraBytes || inPara >= paraBytes;
    if (breakHere) inPara = 0;
    const p = sentence + (breakHere ? '\n\n' : ' ');
    out.push(p);
    size += Buffer.byteLength(p);
  }
  return out.join('');
}

// Send logs per run id, the reference clock for latency (sim and host share Date.now()).
const sendLogs = new Map();

function keepSendLog(id, log) {
  sendLogs.delete(id);
  sendLogs.set(id, log);
  while (sendLogs.size > MAX_SEND_LOGS) sendLogs.delete(sendLogs.keys().next().value);
}

function stream(req, res, q) {
  const fmt = q.get('fmt') ?? 'raw';
  if (!['raw', 'sse', 'ndjson'].includes(fmt)) throw new HttpError(400, 'fmt must be raw, sse or ndjson');
  const tps = param(q, 'tps', 50, 0, 1000);
  const size = param(q, 'chunk', 16, 1, MiB);
  const text = buildText({
    targetBytes: param(q, 'bytes', 0, 0, MAX_TEXT_BYTES),
    repeat: param(q, 'repeat', 20, 0, 100_000),
    paraBytes: param(q, 'para', 0, 0, MAX_TEXT_BYTES),
  });
  const id = q.get('id');
  const bytes = Buffer.from(text, 'utf8');
  const chars = Array.from(text);

  const log = { total: fmt === 'raw' ? bytes.length : null, events: [] };
  if (id) keepSendLog(id, log);

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

  // Writes the next chunk; returns false once everything is sent, 'drain' when the socket is full.
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

  // Paced: one chunk per tick; a full socket pauses the ticks until it drains.
  let timer;
  const stop = () => clearInterval(timer);
  const tick = () => {
    if (closed) return stop();
    const r = writeNext();
    if (r === false) {
      stop();
      finish();
    } else if (r === 'drain') {
      stop();
      res.once('drain', start);
    }
  };
  const start = () => {
    if (!closed) timer = setInterval(tick, 1000 / tps);
  };
  req.on('close', stop);
  start();
}

function readBody(req, limit) {
  return new Promise((resolve, reject) => {
    const parts = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > limit) {
        req.removeAllListeners('data');
        req.resume(); // discard the rest
        reject(new HttpError(413, `body over ${limit} bytes`));
        return;
      }
      parts.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(parts).toString('utf8')));
    req.on('error', reject);
  });
}

async function saveResults(req, res, q) {
  const name = (q.get('name') || `bench-${Date.now()}`).replace(/[^\w.-]/g, '_').slice(0, 200);
  const body = await readBody(req, MAX_RESULTS_BYTES);
  try {
    JSON.parse(body);
  } catch {
    throw new HttpError(400, 'body is not JSON');
  }
  fs.mkdirSync(RESULTS_DIR, { recursive: true });
  const file = path.join(RESULTS_DIR, `${name}.json`);
  try {
    fs.writeFileSync(file, body, { flag: 'wx' });
  } catch (e) {
    if (e.code === 'EEXIST') throw new HttpError(409, `${name}.json already exists`);
    throw e;
  }
  console.log(`results -> ${file}`);
  res.writeHead(200, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ file }));
}

async function handle(req, res) {
  const url = new URL(req.url, 'http://x');
  const q = url.searchParams;
  if (url.pathname === '/log') {
    const log = sendLogs.get(q.get('id')) ?? { total: null, events: [] };
    res.writeHead(200, { 'Content-Type': 'application/json' });
    return res.end(JSON.stringify(log));
  }
  if (url.pathname === '/results' && req.method === 'POST') return saveResults(req, res, q);
  // The send logs' clock, for a device that doesn't share it (a phone): see example/src/bench/clock.ts.
  if (url.pathname === '/time') {
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    return res.end(JSON.stringify({ now: Date.now() }));
  }
  // streamFetch() tests: what the request carried, and a given status.
  if (url.pathname === '/echo') {
    const body = await readBody(req, 64 * 1024);
    res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8', 'X-Echo-Method': req.method });
    return res.end(`${req.method} ${req.headers['x-echo'] ?? ''}\n${body}`);
  }
  if (url.pathname === '/status') {
    const code = Number(q.get('code'));
    if (!Number.isInteger(code) || code < 200 || code > 599) throw new HttpError(400, 'code must be 200..599');
    res.writeHead(code, { 'Content-Type': 'text/plain; charset=utf-8' });
    return res.end(`status ${code}`);
  }
  return stream(req, res, q);
}

for (const host of HOSTS) {
  http
    .createServer((req, res) => {
      handle(req, res).catch((e) => {
        const status = e instanceof HttpError ? e.status : 500;
        if (status === 500) console.error(e);
        if (res.headersSent) return res.destroy();
        res.writeHead(status, { 'Content-Type': 'text/plain; charset=utf-8' });
        res.end(e.message);
      });
    })
    .on('error', (e) => console.error(`stream server on ${host}: ${e.message}`))
    .listen(PORT, host, () => console.log(`stream server on http://${host.includes(':') ? `[${host}]` : host}:${PORT}`));
}
