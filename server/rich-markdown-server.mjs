// Deterministic loopback fixtures for native Markdown attachments (PORT defaults to 9882).
import http from 'node:http';
import { deflateSync } from 'node:zlib';

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const payload = Buffer.concat([Buffer.from(type), data]);
  const result = Buffer.alloc(data.length + 12);
  result.writeUInt32BE(data.length);
  payload.copy(result, 4);
  result.writeUInt32BE(crc32(payload), result.length - 4);
  return result;
}
function png(width, height) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 2;
  const pixels = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const offset = y * (width * 3 + 1) + 1 + x * 3;
      const purple = ((x >> 4) + (y >> 4)) % 2 === 0;
      pixels[offset] = purple ? 167 : 34;
      pixels[offset + 1] = purple ? 139 : 211;
      pixels[offset + 2] = purple ? 250 : 238;
    }
  }
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', header), chunk('IDAT', deflateSync(pixels)), chunk('IEND', Buffer.alloc(0))]);
}
const image = png(160, 80);
const largeImage = png(4096, 2048);
const requests = new Map();
const activity = new Map();
const port = Number(process.env.PORT) || 9882;

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  const key = url.searchParams.get('key') ?? '';
  if (url.pathname === '/health') return res.writeHead(204).end();
  if (url.pathname === '/counts') {
    res.setHeader('Content-Type', 'application/json');
    return res.end(JSON.stringify(requests.get(key) ?? { count: 0, authorization: false, cookie: false }));
  }
  if (url.pathname === '/activity') {
    res.setHeader('Content-Type', 'application/json');
    return res.end(JSON.stringify(activity.get(key) ?? { active: 0, peak: 0 }));
  }
  if (url.pathname === '/markdown') {
    const host = req.headers.host ?? `127.0.0.1:${port}`;
    const body = Buffer.from(`# Native Markdown\n\nTables, images and code update on the native side.\n\n` +
      '| Feature | State | Notes |\n| :--- | :---: | ---: |\n' +
      '| **Tables** | Ready | Cells wrap at the available width |\n' +
      '| Images | Async | 160 × 80 |\n| Unicode | 🚀 | Привет 世界 |\n\n' +
      '```typescript\n// A streaming response\nconst answer: string = "Hello";\nconsole.log(answer, 42);\n```\n\n' +
      `![Purple and teal checkerboard](http://${host}/image.png?delay=600&key=demo)\n\n` +
      '```python\ndef greet(name):\n    return "Hello " + name # native highlighting\n```\n');
    res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' });
    let offset = 0;
    const timer = setInterval(() => {
      res.write(body.subarray(offset, offset + 23));
      offset += 23;
      if (offset >= body.length) { clearInterval(timer); res.end(); }
    }, 12);
    res.on('close', () => clearInterval(timer));
    return;
  }
  const count = requests.get(key) ?? { count: 0, authorization: false, cookie: false };
  count.count++;
  count.authorization ||= Boolean(req.headers.authorization);
  count.cookie ||= Boolean(req.headers.cookie);
  requests.set(key, count);
  const group = url.searchParams.get('group');
  if (group) {
    const state = activity.get(group) ?? { active: 0, peak: 0 };
    state.active++;
    state.peak = Math.max(state.peak, state.active);
    activity.set(group, state);
    res.on('close', () => state.active--);
  }
  const timer = setTimeout(() => {
    if (url.pathname === '/broken.png') return res.writeHead(404).end('missing');
    if (url.pathname === '/invalid.png') return res.writeHead(200, { 'Content-Type': 'image/png' }).end('invalid image');
    if (url.pathname === '/oversize.png') {
      res.writeHead(200, { 'Content-Type': 'image/png', 'Content-Length': 9 * 1024 * 1024 });
      return res.end(Buffer.alloc(9 * 1024 * 1024));
    }
    if (url.pathname === '/oversize-chunked.png') {
      res.writeHead(200, { 'Content-Type': 'image/png' });
      const chunk = Buffer.alloc(64 * 1024);
      let sent = 0;
      const send = () => {
        while (!res.destroyed && sent < 144) {
          sent++;
          if (!res.write(chunk)) { res.once('drain', send); return; }
        }
        if (!res.destroyed) res.end();
      };
      send();
      return;
    }
    if (url.pathname === '/image.png' || url.pathname === '/large.png') {
      const bytes = url.pathname === '/large.png' ? largeImage : image;
      return res.writeHead(200, { 'Content-Type': 'image/png', 'Content-Length': bytes.length,
        'Cache-Control': 'no-store' }).end(bytes);
    }
    res.writeHead(404).end('not found');
  }, Math.min(10000, Math.max(0, Number(url.searchParams.get('delay')) || 0)));
  res.on('close', () => clearTimeout(timer));
});
server.listen(port, '127.0.0.1', () => console.log(`rich Markdown fixtures ready on ${port}`));
process.on('SIGTERM', () => server.close(() => process.exit(0)));
process.on('SIGINT', () => server.close(() => process.exit(0)));
