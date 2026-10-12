import React from 'react';
import { Platform, View } from 'react-native';
import { describe, expect, it, render } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import { buffer, newKey, testing, TEXT_STYLE, TSB, waitForSnapshot } from './helpers';

const SENTENCES = [
  'Streaming text straight from the network into a native view, every token lands in a buffer. ',
  'Привет! Это проверка переноса строк с кириллицей внутри длинного абзаца. ',
  '漢字とかなが混ざった行、そして English words in between. ',
  'Short one. ',
];
// Twenty chat messages, from a line to a few KB.
const SIZES = [40, 120, 300, 900, 2000, 60, 500, 3000, 150, 1200, 80, 700, 2500, 200, 1000, 50, 400, 1800, 100, 600];
const MESSAGES = SIZES.map((size, i) => {
  let text = '';
  for (let j = i; text.length < size; j++) text += SENTENCES[j % SENTENCES.length];
  return text.slice(0, size).trim() + ` #${i}`;
});

type Frame = { w: number; h: number; mode: string };
type Resize = { mainMs: number; layouts: number; longestMs: number; views: Record<string, Frame[]> };

/** Resizes the surface on the main thread, as a rotation or Split View does. */
async function resize(keys: string[], width: number, ms = 400): Promise<Resize> {
  return JSON.parse(await testing.resizeSurface(keys, width, ms));
}

const settled = (frames: Frame[]) => frames[frames.length - 1];

function Chat({ keys, style }: { keys: string[]; style: object }) {
  return (
    <View style={{ alignSelf: 'stretch' }}>
      {MESSAGES.map((text, i) => (
        <View key={keys[i]} style={{ alignSelf: i % 2 ? 'flex-end' : 'flex-start', maxWidth: '85%', padding: 8 }}>
          <StreamText source={buffer(text, TSB.done)} nativeID={keys[i]} recordDisplayLog {...style} />
        </View>
      ))}
    </View>
  );
}

describe('StreamText: the surface changes width (rotation, Split View)', () => {
  // iOS: CoreText, and TextKit without a lineHeight; Android: StaticLayout either way.
  for (const [engine, style] of [
    [Platform.OS === 'ios' ? 'coreText' : 'staticLayout', TEXT_STYLE],
    [Platform.OS === 'ios' ? 'textKit' : 'staticLayout', { fontSize: 14 }],
  ] as const) {
    it(`20 ${engine} bubbles${'lineHeight' in style ? '' : ' without a lineHeight'} stay on the canvas, and no frame shows a bubble at its old height`, async () => {
      testing.clearPreparedLayouts();
      const keys = MESSAGES.map(() => newKey());
      await render(<Chat keys={keys} style={style} />);
      for (const key of keys) {
        await waitForSnapshot(key, (s) => {
          expect(s.mode).toBe('canvas');
          expect(s.engine).toBe(engine);
        });
      }
      const heights: Record<number, number[]> = {};
      const check = (r: Resize, width: number) => {
        const at = keys.map((key) => {
          const frames = r.views[key];
          const last = settled(frames);
          // Every frame from the first one after the resize: a canvas, at the new width, at its height.
          for (const frame of frames) expect(frame).toEqual(last);
          expect(last.mode).toBe('canvas');
          return last.h;
        });
        if (heights[width]) expect(at).toEqual(heights[width]);
        heights[width] = at;
        // One layout per bubble at most, on the main thread.
        expect(r.layouts).toBeLessThanOrEqual(keys.length);
        return at;
      };
      try {
        const narrow = check(await resize(keys, 300), 300);
        const wide = check(await resize(keys, 430), 430);
        for (let i = 0; i < keys.length; i++) expect(narrow[i]).toBeGreaterThanOrEqual(wide[i]);
        expect(narrow.reduce((a, b) => a + b)).toBeGreaterThan(wide.reduce((a, b) => a + b));
        // Back and forth: the layouts of both widths are cached.
        expect((await resize(keys, 300)).layouts).toBe(0);
        expect((await resize(keys, 430)).layouts).toBe(0);
        // Dragging a Split View divider: a new width every frame.
        for (const width of [400, 380, 360, 340, 320, 300]) check(await resize(keys, width, 16), width);
        // Settled at widths from the drag (laid out again: the cache has moved on), the same heights.
        check(await resize(keys, 400), 400);
        check(await resize(keys, 340), 340);
      } finally {
        await resize(keys, 0);
      }
    });
  }
});
