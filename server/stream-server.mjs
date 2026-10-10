// Local streaming server for the example app: node server/stream-server.mjs
// Query: fmt=raw|sse|ndjson, tps=<chunks per second>, chunk=<bytes (raw) or chars>, repeat=<text repeats>
import http from 'node:http';

const PORT = Number(process.env.PORT) || 9877;
// An LLM-style answer; the emoji are multi-byte, so raw chunks split them mid-character.
const PARAGRAPHS = [
  'Streaming text straight from the network into a native view ⚡️ Every token lands in a native buffer, and the view picks it up on the next frame.',
  'No bridge, no setState, no re-render. JavaScript hands the buffer over once and steps aside 🚀',
  'Under the hood: URLSession appends bytes to an append-only log, and a CADisplayLink on the main thread decodes only the new UTF-8 slice ✨',
  'Freeze the JS thread and watch: the native card keeps flowing while the JS card stops dead 🧊',
  'Each frame costs O(delta), not O(n). A 100 KB answer streams as smoothly as a 1 KB one 📈',
];
const buildText = (repeat) =>
  Array.from({ length: repeat }, (_, i) => PARAGRAPHS[i % PARAGRAPHS.length] + '\n\n').join('');

http
  .createServer((req, res) => {
    const q = new URL(req.url, 'http://x').searchParams;
    const tps = Number(q.get('tps') ?? 50);
    const size = Number(q.get('chunk') ?? 16);
    const fmt = q.get('fmt') ?? 'raw';
    const text = buildText(Number(q.get('repeat') ?? 20));
    const bytes = Buffer.from(text, 'utf8');
    const chars = Array.from(text);

    res.writeHead(200, {
      'Content-Type': fmt === 'sse' ? 'text/event-stream' : 'text/plain; charset=utf-8',
      'Cache-Control': 'no-cache',
      // Stops URLSession from buffering the first bytes for MIME sniffing.
      'X-Content-Type-Options': 'nosniff',
    });

    let i = 0;
    const timer = setInterval(() => {
      if (fmt === 'raw') {
        if (i >= bytes.length) {
          clearInterval(timer);
          return res.end();
        }
        // Deliberately splits UTF-8 sequences across chunks.
        res.write(bytes.subarray(i, (i += size)));
        return;
      }
      if (i >= chars.length) {
        if (fmt === 'sse') res.write('data: [DONE]\n\n');
        clearInterval(timer);
        return res.end();
      }
      const content = chars.slice(i, (i += size)).join('');
      const json = JSON.stringify({ choices: [{ delta: { content } }] });
      res.write(fmt === 'sse' ? `data: ${json}\n\n` : json + '\n');
    }, 1000 / tps);

    req.on('close', () => clearInterval(timer));
  })
  .listen(PORT, '0.0.0.0', () => console.log(`stream server on http://0.0.0.0:${PORT}`));
