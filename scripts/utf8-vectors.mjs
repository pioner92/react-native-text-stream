// Writes the UTF-8 vectors the Android decoder is tested against (Utf8StreamDecoderTest.kt):
// one line per case, `<input hex>\t<expected UTF-16 hex>`, the expected text from TextDecoder.
// node scripts/utf8-vectors.mjs
import fs from 'node:fs';
import path from 'node:path';

const out = path.join(
  path.dirname(new URL(import.meta.url).pathname),
  '../packages/react-native-text-stream/android/src/test/resources/utf8-vectors.tsv',
);
const decoder = new TextDecoder('utf-8');
const encode = (s) => new TextEncoder().encode(s);
const b = (...x) => Uint8Array.from(x);

// The cases of src/__tests__/buffer.test.ts.
const cases = [
  encode('Hello, world!'),
  encode('Привет, мир'),
  encode('漢字 ⚡️ テキスト'),
  encode('🚀 🧊 📈 𝄞'),
  b(),
  b(0x41, 0x80, 0x42),
  b(0xc0, 0xc1, 0xf5, 0xf8, 0xfe, 0xff, 0x41),
  b(0xc3, 0x41),
  b(0xe2, 0x82, 0x41),
  b(0xf0, 0x9f, 0x9a, 0x41),
  b(0xe0, 0x80, 0x80, 0xc0, 0xaf),
  b(0xed, 0xa0, 0x80, 0xed, 0xbf, 0xbf),
  b(0xf4, 0x90, 0x80, 0x80),
  b(0xf4, 0x8f, 0xbf, 0xbf),
  b(0x41, 0xf0, 0x9f, 0x9a),
  b(0xef, 0xbb, 0xbf, 0x41),
  b(0xef, 0xbb, 0xbf, 0xef, 0xbb, 0xbf, 0x41),
  b(0x41, 0xef, 0xbb, 0xbf, 0x42),
  b(0xef, 0xbb),
  b(0xef, 0xbb, 0xbf, 0xff, 0x41),
];

// Deterministic random bytes, mostly near the UTF-8 boundaries, where decoders disagree.
let seed = 42;
const random = () => {
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
for (let run = 0; run < 500; run++) {
  const input = new Uint8Array(1 + Math.floor(random() * 32));
  for (let i = 0; i < input.length; i++) {
    input[i] = random() < 0.5 ? Math.floor(random() * 256) : 0x7f + Math.floor(random() * 0x80);
  }
  cases.push(input);
}

const hex = (bytes) => Buffer.from(bytes).toString('hex');
const utf16 = (s) => [...Array(s.length).keys()].map((i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, cases.map((c) => `${hex(c)}\t${utf16(decoder.decode(c))}\n`).join(''));
console.log(`wrote ${cases.length} cases to ${out}`);
