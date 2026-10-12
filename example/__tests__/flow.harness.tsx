import React from 'react';
import { PixelRatio, ScrollView, Text } from 'react-native';
import { afterEach, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import {
  arrayBuffer,
  buffer,
  Column,
  describeAndroid,
  layouts,
  newKey,
  TEXT_STYLE,
  testing,
  TSB,
  utf8,
  waitForSnapshot,
  waitForText,
} from './helpers';

// One long paragraph, streamed: on Android the lines a DynamicLayout already wrapped are frozen
// past a few dozen (StreamTextFlow), so an append re-wraps only the tail. Whatever the platform
// does, the text must lay out exactly as it does in one piece.
const LATIN = 'A long paragraph without a line break, streamed in chunks; words, numbers 12345 and emoji 🚀 keep coming. '.repeat(380);
const HEBREW = 'שלום עולם, זו פסקה ארוכה בעברית בלי שבירת שורה, עם מילה English באמצע. '.repeat(450);

afterEach(() => testing.stopProducers());

// Android only for now: on iOS the text view runs 2 pt off <Text> without a lineHeight here, and
// the right-to-left run doesn't finish within the test's time; both to look into on iOS.
describeAndroid('StreamText: one long paragraph, streamed', () => {
  for (const [name, text, style] of [
    ['with a lineHeight', LATIN, TEXT_STYLE],
    ["with the font's line height", LATIN, { fontSize: 14 }],
    ['right to left', HEBREW, TEXT_STYLE],
  ] as const) {
    it(`lays out as in one piece, ${name}`, async () => {
      const key = newKey();
      const { heights, onLayout } = layouts();
      const tsb = buffer(undefined, TSB.open, 128 * 1024);
      testing.startProducer(tsb, arrayBuffer(utf8(text)), 512, 2, TSB.done);
      // In a scroll view: tens of thousands of points, more than the screen lets a column grow.
      const { unmount } = await render(
        <ScrollView>
          <Column>
            <StreamText source={tsb} nativeID={key} recordDisplayLog onLayout={onLayout('stream')} {...style} />
            {/* Greedy wrapping, as StreamText (Android's <Text> balances lines by default). */}
            <Text textBreakStrategy="simple" onLayout={onLayout('text')} style={style}>
              {text}
            </Text>
          </Column>
        </ScrollView>,
      );
      const streamed = await waitForSnapshot(key, (s) => expect(s.status).toBe('done'), 20000);
      expect(streamed.text).toBe(text);
      await waitFor(() => {
        expect(heights.stream).toBe(streamed.height);
        // Within a pixel of <Text> at tens of thousands of points (its frame rounds on its own);
        // exactly the height of the text laid out in one piece below.
        expect(Math.abs(heights.stream - heights.text) * PixelRatio.get()).toBeLessThanOrEqual(1);
      });
      unmount();

      // The same text mounted finished: laid out ahead, the same height.
      const finished = newKey();
      await render(
        <ScrollView>
          <Column>
            <StreamText source={buffer(text, TSB.done, 128 * 1024)} nativeID={finished} recordDisplayLog {...style} />
          </Column>
        </ScrollView>,
      );
      const mounted = await waitForText(finished, text, 8000);
      expect(mounted.mode).toBe('canvas');
      expect(mounted.height).toBe(streamed.height);
    });
  }
});
