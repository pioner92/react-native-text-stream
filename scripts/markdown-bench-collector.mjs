// Run before the dedicated Release Markdown benchmark. Saves each run without overwriting.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
const directory = process.env.MARKDOWN_BENCH_DIR ?? path.resolve('bench-results');
fs.mkdirSync(directory, { recursive: true });
http.createServer((req, res) => {
  if (req.method !== 'POST' || req.url !== '/results') { res.writeHead(404).end(); return; }
  const chunks = [];
  let size = 0;
  req.on('data', chunk => {
    size += chunk.length;
    if (size > 16 * 1024 * 1024) { req.destroy(); return; }
    chunks.push(chunk);
  });
  req.on('end', () => {
    try {
      const result = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      if (!Array.isArray(result.results)) throw new Error('Missing results');
      const destination = path.join(directory, `markdown-${Date.now()}.json`);
      fs.writeFileSync(destination, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
      console.log(`Saved ${result.results.length} runs to ${destination}`);
      res.writeHead(201).end('saved');
    } catch (error) {
      console.error(error);
      res.writeHead(400).end('invalid results');
    }
  });
}).listen(19879, '127.0.0.1', () => console.log('Markdown benchmark collector listening on 19879'));
