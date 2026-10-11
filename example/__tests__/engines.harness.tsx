import { describe, expect, it } from 'react-native-harness';
import type { StreamTextTestStyle } from 'text-stream-testing';
import { PARAGRAPHS, testing } from './helpers';

// The CoreText engine must make exactly TextKit's lines (the text view's engine): a canvas swapped
// for a text view, or a remount of a streamed text, must not move a single glyph.

const LATIN =
  'Streaming text straight from the network into a native view, every token lands in a native buffer. ' +
  'No bridge, no setState, no re-render: JavaScript hands the buffer over once and steps aside.\n\n' +
  'AV Wa To Ty "quoted" — dashes, ellipses… and numbers 1234567890 keep their kerning.';

const CORPUS: Record<string, string> = {
  latin: LATIN,
  mixed: PARAGRAPHS,
  emoji: 'Family 👨‍👩‍👧‍👦 flags 🇺🇦🇯🇵 skin 👋🏽 keycaps 1️⃣ and ⚡️ variation selectors in a sentence that wraps over lines.',
  thai: 'ภาษาไทยไม่มีการเว้นวรรคระหว่างคำ การตัดบรรทัดจึงต้องใช้พจนานุกรม ข้อความนี้ยาวพอที่จะขึ้นบรรทัดใหม่หลายครั้ง',
  devanagari: 'हिन्दी में संयुक्ताक्षर और मात्राएँ होती हैं, जैसे क्षत्रिय, श्रृंगार और द्वार; यह वाक्य कई पंक्तियों में टूटेगा।',
  vietnamese: 'Tiếng Việt có nhiều dấu chồng: Nguyễn, ưởng, ượng, ỗ, ặ — dòng này đủ dài để xuống dòng vài lần.',
  cjk: '漢字とかなが混ざった長い文章は、句読点の後でも改行されます。中文的标点符号，例如逗号和句号。한국어 문장도 섞여 있습니다.',
  url: 'See https://example.com/a/really/long/path/that/does/not/fit/on/a/line?with=query&and=more#fragment for details.',
  nbsp: 'Numbers with no-break spaces and thin spaces and en spaces and em spaces keep their place across lines.',
  trailingNewline: 'A text that ends in a line break.\n',
  crlf: 'Windows line\r\nendings\r\nin a text.\r\n',
  blankLines: '\n\nStarts with blank lines,\n\n\nhas some in the middle\n\n',
  longWord: 'Supercalifragilisticexpialidocious'.repeat(6) + ' then normal words.',
  longParagraph: LATIN.replace(/\n/g, ' ').repeat(25), // ~7K characters: still one CoreText paragraph
};

const BASE: StreamTextTestStyle = { fontSize: 14, lineHeight: 21 };
const STYLES: Record<string, StreamTextTestStyle> = {
  base: BASE,
  center: { ...BASE, textAlign: 'center' },
  right: { ...BASE, textAlign: 'right' },
  letterSpacing: { ...BASE, letterSpacing: 0.6 },
  bold: { ...BASE, fontWeight: '700' },
  menlo: { ...BASE, fontFamily: 'Menlo' },
  georgia: { fontSize: 17, lineHeight: 24, fontFamily: 'Georgia' },
  tight: { fontSize: 15, lineHeight: 16 }, // below the font's own line height
  divides256: { fontSize: 18, lineHeight: 32 }, // lines meet tile edges
};

function expectSameLines(result: Record<string, number>, what: string) {
  if (result.coreText !== 1) throw new Error(`${what}: CoreText did not take it`);
  if (result.mismatchedLines || result.baselineSpread >= 0.01 || result.maxGlyphDx >= 0.05 ||
      result.heightCoreText !== result.heightTextKit) {
    throw new Error(`${what}: ${JSON.stringify(result)}`);
  }
  const details = JSON.stringify(result);
  expect({ what, mismatchedLines: result.mismatchedLines, details }).toEqual({ what, mismatchedLines: 0, details });
  expect(result.linesCoreText).toBe(result.linesTextKit);
  expect(result.heightCoreText).toBe(result.heightTextKit);
  expect(result.baselineSpread).toBeLessThan(0.01);
  // A third of a pixel at 3x would already show; stay well below.
  expect(result.maxGlyphDx).toBeLessThan(0.05);
  expect(result.comparedGlyphs).toBeGreaterThan(0);
}

describe('StreamText: CoreText lines match TextKit', () => {
  for (const [name, text] of Object.entries(CORPUS)) {
    it(`${name}, at three widths`, () => {
      for (const width of [300, 301.5, 211]) {
        expectSameLines(testing.compareEngines(text, BASE, width), `${name} @ ${width}`);
      }
    });
  }

  for (const [name, style] of Object.entries(STYLES)) {
    it(`style ${name}`, () => {
      for (const [textName, text] of Object.entries({ latin: LATIN, mixed: PARAGRAPHS })) {
        expectSameLines(testing.compareEngines(text, style, 300), `${name} / ${textName}`);
      }
    });
  }

  it('leaves to TextKit what it does not reproduce', () => {
    const rejected: [string, string, StreamTextTestStyle][] = [
      ['no lineHeight', LATIN, { fontSize: 14, lineHeight: 0 }],
      ['justified', LATIN, { ...BASE, textAlign: 'justify' }],
      ['Hebrew', 'שלום עולם, זה טקסט בעברית שנכתב מימין לשמאל.', BASE],
      ['Arabic', 'مرحبا بالعالم، هذا نص عربي يكتب من اليمين إلى اليسار.', BASE],
      ['tab', 'Column\tcolumn\tcolumn', BASE],
      ['ideographic space', '全角　スペース', BASE],
      ['one huge paragraph', 'word '.repeat(2000), BASE],
    ];
    for (const [what, text, style] of rejected) {
      expect({ what, coreText: testing.compareEngines(text, style, 300).coreText }).toEqual({ what, coreText: 0 });
    }
  });
});
