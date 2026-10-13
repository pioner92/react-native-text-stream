// Dedicated loopback fixture for device proof of renderer/fetch lifecycle findings.
// Run: node server/review-test-server.mjs
// It deliberately has its own port and in-memory side-effect counter so normal harness suites
// cannot affect the evidence collected by review-repro.harness.tsx.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const port = Number(process.env.PORT) || 19878;
const effects = new Map();
const paused = new Map();
const evidenceFile = process.env.REVIEW_EVIDENCE_PATH
  ?? path.resolve('reviews/2026-10-13/device-proof/evidence.jsonl');
fs.mkdirSync(path.dirname(evidenceFile), { recursive: true });

function keyOf(url) {
  return url.searchParams.get('key') ?? '';
}

function json(res, value, status = 200) {
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  res.end(JSON.stringify(value));
}

function text(res, value, status = 200) {
  res.writeHead(status, {
    'Content-Type': 'text/plain; charset=utf-8',
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  res.end(value);
}

async function discard(req) {
  for await (const _ of req) {
    // The counter records accepted HTTP mutations, independent of their body size.
  }
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? '127.0.0.1'}`);
  if (url.pathname === '/empty') {
    res.writeHead(204, { 'Cache-Control': 'no-store' });
    return res.end();
  }
  if (url.pathname === '/count') return json(res, { count: effects.get(keyOf(url)) ?? 0 });
  if (url.pathname === '/reset' && req.method === 'POST') {
    await discard(req);
    effects.set(keyOf(url), 0);
    return json(res, { count: 0 });
  }
  if (url.pathname === '/evidence' && req.method === 'POST') {
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    try {
      const record = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      fs.appendFileSync(evidenceFile, `${JSON.stringify(record)}\n`);
      return json(res, { saved: true });
    } catch {
      return text(res, 'invalid evidence JSON', 400);
    }
  }
  if (url.pathname === '/paused') {
    const key = keyOf(url);
    res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' });
    res.flushHeaders();
    res.write(url.searchParams.get('first') ?? 'first');
    paused.set(key, res);
    const timer = setTimeout(() => { paused.delete(key); res.end('fixture timeout'); }, 15000);
    res.on('close', () => { clearTimeout(timer); paused.delete(key); });
    return;
  }
  if (url.pathname === '/complete' && req.method === 'POST') {
    await discard(req);
    const target = paused.get(keyOf(url));
    if (!target) return text(res, 'missing paused response', 404);
    target.end(url.searchParams.get('tail') ?? 'tail');
    return json(res, { completed: true });
  }
  if (url.pathname === '/stream') return text(res, url.searchParams.get('text') ?? '');
  if (url.pathname === '/effect' && req.method === 'POST') {
    await discard(req);
    const key = keyOf(url);
    const next = (effects.get(key) ?? 0) + 1;
    effects.set(key, next);
    console.log(`EFFECT key=${key} count=${next}`);
    const delay = Math.max(0, Math.min(5000, Number(url.searchParams.get('delay')) || 0));
    return setTimeout(() => text(res, `effect:${key}:${next}`), delay);
  }
  return text(res, 'not found', 404);
});

server.listen(port, '127.0.0.1', () => console.log(`device proof fixture ready on http://127.0.0.1:${port}`));
