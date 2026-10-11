import React from 'react';
import { ScrollView, Text, View } from 'react-native';
import { afterEach, beforeEach, describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import { createFrameMonitor } from 'text-stream-testing';
import {
  arrayBuffer,
  buffer,
  Column,
  layouts,
  newKey,
  PARAGRAPHS,
  sleep,
  snapshot,
  TEXT_STYLE,
  testing,
  TSB,
  utf8,
  waitForSnapshot,
  waitForText,
} from './helpers';

// A finished `source` prop is laid out in the shadow node (CoreText where it can) and drawn on a
// canvas, without a text view; everything else keeps the text view.

const monitor = createFrameMonitor();
const prepares = (phases: Record<string, number>) =>
  (phases['prepare.coreText.count'] ?? 0) + (phases['prepare.textKit.count'] ?? 0);

const LONG = 'Streaming text straight from the network into a native view, every token lands in a buffer.\n\n'.repeat(60);

beforeEach(() => {
  testing.setPreparedLayout(true);
  testing.setPreparedCoreText(true);
  testing.clearPreparedLayouts();
});
afterEach(() => testing.stopProducers());

/** Mounts `text` finished, in a 300 pt column, and returns its snapshot once shown. */
async function mountFinished(text: string, props: Record<string, unknown> = TEXT_STYLE) {
  const key = newKey();
  const { heights, onLayout } = layouts();
  const view = await render(
    <Column>
      <StreamText source={buffer(text, TSB.done)} nativeID={key} recordDisplayLog onLayout={onLayout('stream')} {...props} />
    </Column>,
  );
  const s = await waitForText(key, text);
  await waitFor(() => expect(heights.stream).toBe(s.height));
  return { key, s, heights, view };
}

describe('StreamText: prepared layouts', () => {
  it('draws a finished source on a canvas, at the height the text view would have', async () => {
    const prepared = await mountFinished(PARAGRAPHS);
    expect(prepared.s.mode).toBe('canvas');
    expect(prepared.s.engine).toBe('coreText');
    expect(prepared.s.tiles).toBeGreaterThan(0);
    expect(prepared.s.accessibilityValue).toBe(PARAGRAPHS);
    prepared.view.unmount();

    testing.setPreparedLayout(false);
    const legacy = await mountFinished(PARAGRAPHS);
    expect(legacy.s.mode).toBe('textView');
    expect(prepared.s.height).toBe(legacy.s.height);
  });

  for (const [what, text] of Object.entries({
    'a trailing line break': 'Ends with a line break.\n',
    'blank lines': '\n\nTop\n\n\nmiddle\n\n',
    'CRLF': 'One\r\ntwo\r\n',
  })) {
    it(`gives the text view's height with ${what}`, async () => {
      const prepared = await mountFinished(text);
      expect(prepared.s.mode).toBe('canvas');
      prepared.view.unmount();
      testing.setPreparedLayout(false);
      const legacy = await mountFinished(text);
      expect(prepared.s.height).toBe(legacy.s.height);
    });
  }

  it('uses TextKit where CoreText does not reproduce it, still on a canvas', async () => {
    for (const [text, props] of [
      [PARAGRAPHS, { fontSize: 14 }], // no lineHeight
      ['שלום עולם, טקסט בעברית.', TEXT_STYLE],
      ['tab\tseparated', TEXT_STYLE],
    ] as const) {
      const { s, view } = await mountFinished(text, props);
      expect(s.mode).toBe('canvas');
      expect(s.engine).toBe('textKit');
      view.unmount();
    }
  });

  it('keeps the text view for an open stream, attach(), and texts over 64 KB', async () => {
    const open = newKey();
    const tsb = buffer('still streaming');
    await render(<StreamText source={tsb} nativeID={open} recordDisplayLog {...TEXT_STYLE} />);
    expect((await waitForText(open, 'still streaming')).mode).toBe('textView');

    const attached = newKey();
    let ref: { attach(b: ArrayBuffer): void } | null = null;
    await render(<StreamText ref={(r) => { ref = r; }} nativeID={attached} recordDisplayLog {...TEXT_STYLE} />);
    ref!.attach(buffer('attached', TSB.done));
    expect((await waitForText(attached, 'attached')).mode).toBe('textView');

    const big = 'x'.repeat(70 * 1024);
    const large = newKey();
    await render(<StreamText source={buffer(big, TSB.done, 128 * 1024)} nativeID={large} recordDisplayLog {...TEXT_STYLE} />);
    expect((await waitForText(large, big, 8000)).mode).toBe('textView');
  });

  it('a new style or width gets a new layout, not a text view', async () => {
    const key = newKey();
    const text = 'A paragraph long enough that a bigger font or a narrower column adds lines to it. '.repeat(4);
    const source = buffer(text, TSB.done);
    const element = (props: { fontSize: number; width: number; color?: string }) => (
      <Column width={props.width}>
        <StreamText source={source} nativeID={key} recordDisplayLog fontSize={props.fontSize} lineHeight={21} color={props.color} />
      </Column>
    );
    const { rerender } = await render(element({ fontSize: 14, width: 300 }));
    const first = await waitForSnapshot(key, (s) => expect(s.mode).toBe('canvas'));

    await rerender(element({ fontSize: 17, width: 300 }));
    const bigger = await waitForSnapshot(key, (s) => expect(s.fontSize).toBe(17));
    expect(bigger.mode).toBe('canvas');
    expect(bigger.height).toBeGreaterThan(first.height);

    await rerender(element({ fontSize: 17, width: 220 }));
    const narrower = await waitForSnapshot(key, (s) => expect(s.height).toBeGreaterThan(bigger.height));
    expect(narrower.mode).toBe('canvas');

    // A color needs no new layout at all.
    monitor.streamTextPhases(true);
    await rerender(element({ fontSize: 17, width: 220, color: '#FF0000' }));
    const red = await waitForSnapshot(key, (s) => expect(s.color).toBe('#FF0000FF'));
    expect(red.mode).toBe('canvas');
    expect(red.height).toBe(narrower.height);
    expect(prepares(monitor.streamTextPhases(true))).toBe(0);
  });

  it('a new width without a new layout falls back to the text view, text and height intact', async () => {
    const key = newKey();
    const source = buffer(PARAGRAPHS, TSB.done);
    const element = (width: number) => (
      <Column width={width}>
        <StreamText source={source} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>
    );
    const { rerender, unmount } = await render(element(300));
    const wide = await waitForSnapshot(key, (s) => expect(s.mode).toBe('canvas'));

    testing.setPreparedLayout(false); // the shadow node prepares nothing for the new width
    await rerender(element(220));
    const narrow = await waitForSnapshot(key, (s) => expect(s.mode).toBe('textView'));
    expect(narrow.text).toBe(PARAGRAPHS);
    expect(narrow.height).toBeGreaterThan(wide.height);
    unmount();

    const legacy = newKey();
    await render(
      <Column width={220}>
        <StreamText source={buffer(PARAGRAPHS, TSB.done)} nativeID={legacy} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    expect((await waitForText(legacy, PARAGRAPHS)).height).toBe(narrow.height);
  });

  it('lays each text out once, even past the cache size (a long chat)', async () => {
    // More finished texts than the cache holds, one after another…
    for (let i = 0; i < 70; i++) {
      const { view } = await mountFinished(`message ${i} ` + 'word '.repeat(20));
      view.unmount();
    }
    // …then twenty in one commit: each is laid out once, and each mounts as a canvas.
    monitor.streamTextPhases(true);
    const keys = Array.from({ length: 20 }, newKey);
    const texts = keys.map((_, i) => `bubble ${i} ` + 'text '.repeat(30));
    await render(
      <Column>
        {keys.map((key, i) => (
          <StreamText key={key} source={buffer(texts[i], TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
        ))}
      </Column>,
    );
    for (const [i, key] of keys.entries()) {
      expect((await waitForText(key, texts[i])).mode).toBe('canvas');
    }
    expect(prepares(monitor.streamTextPhases(true))).toBe(20);
  });

  it('the view’s own height wins for a text it streamed', async () => {
    const key = newKey();
    const tsb = buffer();
    const { heights, onLayout } = layouts();
    await render(
      <Column>
        <StreamText source={tsb} nativeID={key} recordDisplayLog onLayout={onLayout('stream')} {...TEXT_STYLE} />
      </Column>,
    );
    testing.startProducer(tsb, arrayBuffer(utf8(PARAGRAPHS)), 40, 5, TSB.done);
    const done = await waitForSnapshot(key, (s) => expect(s.status).toBe('done'), 5000);
    expect(done.mode).toBe('textView');
    await sleep(300);
    expect(heights.stream).toBe(done.height);
    expect((await snapshot(key)).mode).toBe('textView');
  });

  it('a streamed text mounts again from the cache: no layout at mount', async () => {
    const tsb = buffer();
    const first = newKey();
    const { unmount } = await render(
      <Column>
        <StreamText source={tsb} nativeID={first} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    testing.startProducer(tsb, arrayBuffer(utf8(PARAGRAPHS)), 64, 5, TSB.done);
    await waitForSnapshot(first, (s) => expect(s.status).toBe('done'), 5000);
    unmount(); // lays it out in the background
    await waitFor(() => expect(testing.preparedCacheCount()).toBe(1), { timeout: 2000 });

    monitor.streamTextPhases(true);
    const again = newKey();
    await render(
      <Column>
        <StreamText source={tsb} nativeID={again} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    expect((await waitForText(again, PARAGRAPHS)).mode).toBe('canvas');
    expect(prepares(monitor.streamTextPhases(true))).toBe(0);
  });

  it('a memory warning empties the cache', async () => {
    await mountFinished(PARAGRAPHS);
    expect(testing.preparedCacheCount()).toBeGreaterThan(0);
    testing.simulateMemoryWarning();
    await waitFor(() => expect(testing.preparedCacheCount()).toBe(0), { timeout: 2000 });
  });

  it('a tall text keeps tiles only near the screen as it scrolls', async () => {
    const key = newKey();
    let scroll: ScrollView | null = null;
    await render(
      <View style={{ height: 400 }}>
        <ScrollView ref={(r) => { scroll = r; }}>
          <Column>
            <StreamText source={buffer(LONG, TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
          </Column>
        </ScrollView>
      </View>,
    );
    const top = await waitForText(key, LONG);
    expect(top.mode).toBe('canvas');
    // 400 pt on screen plus 512 pt drawn ahead: a handful of 256 pt tiles, not the whole text.
    expect(top.tiles).toBeGreaterThan(0);
    expect(top.tiles).toBeLessThanOrEqual(6);
    scroll!.scrollTo({ y: top.height / 2, animated: false });
    await sleep(300);
    const middle = await snapshot(key);
    expect(middle.tiles).toBeGreaterThan(0);
    expect(middle.tiles).toBeLessThanOrEqual(10);
  });

  it('after attach() or clear(), the view decides the height, not the source prop', async () => {
    const key = newKey();
    let ref: { attach(b: ArrayBuffer): void; clear(): void } | null = null;
    const { heights, onLayout } = layouts();
    const source = buffer(PARAGRAPHS, TSB.done);
    const element = (width: number) => (
      <Column width={width}>
        <StreamText ref={(r) => { ref = r; }} source={source} nativeID={key} recordDisplayLog onLayout={onLayout('stream')} {...TEXT_STYLE} />
      </Column>
    );
    const { rerender } = await render(element(300));
    const prepared = await waitForText(key, PARAGRAPHS);
    expect(prepared.mode).toBe('canvas');

    ref!.attach(buffer('short', TSB.done));
    const attached = await waitForText(key, 'short');
    await waitFor(() => expect(heights.stream).toBe(attached.height));
    // A Yoga pass (a new width) must not bring the prop's text back.
    await rerender(element(280));
    await sleep(200);
    expect((await snapshot(key)).text).toBe('short');
    expect(heights.stream).toBe(attached.height);

    ref!.clear();
    await waitFor(() => expect(heights.stream).toBe(0));
    await rerender(element(300));
    await sleep(200);
    expect(heights.stream).toBe(0);

    // A new prop takes over again.
    const next = 'next prop';
    await rerender(
      <Column width={300}>
        <StreamText source={buffer(next, TSB.done)} nativeID={key} recordDisplayLog onLayout={onLayout('stream')} {...TEXT_STYLE} />
      </Column>,
    );
    const s = await waitForText(key, next);
    expect(s.mode).toBe('canvas');
    await waitFor(() => expect(heights.stream).toBe(s.height));
  });

  for (const width of [301.5, 211.33, 333.67]) {
    it(`at a fractional width (${width}) the canvas and the text view agree`, async () => {
      const text = 'Wrap thresholds sit at fractions of a point: WWWW WWWW WWWW iiii iiii WWWW. '.repeat(6);
      const key = newKey();
      const { view } = await (async () => {
        const v = await render(
          <Column width={width}>
            <StreamText source={buffer(text, TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
          </Column>,
        );
        return { view: v };
      })();
      const prepared = await waitForText(key, text);
      expect(prepared.mode).toBe('canvas');
      view.unmount();
      testing.setPreparedLayout(false);
      const legacyKey = newKey();
      await render(
        <Column width={width}>
          <StreamText source={buffer(text, TSB.done)} nativeID={legacyKey} recordDisplayLog {...TEXT_STYLE} />
        </Column>,
      );
      const legacy = await waitForText(legacyKey, text);
      expect(prepared.height).toBe(legacy.height);
    });
  }

  it('selects text on the canvas itself, no text view', async () => {
    const { key, s } = await mountFinished(PARAGRAPHS);
    expect(s.mode).toBe('canvas');
    // First line, a few points in: the first word.
    expect(await testing.selectWordAt(key, 20, 10)).toBe('Streaming');
    // Third paragraph (Cyrillic), first line of it.
    const line = 21;
    const cyrillicTop = PARAGRAPHS.split('\n\n')[0].length > 0 ? 4 * line : 0;
    expect(await testing.selectWordAt(key, 15, cyrillicTop + line / 2)).toBe('Привет');
    expect((await snapshot(key)).mode).toBe('canvas');
  });

  it('bubbles mounted below the screen draw once scrolled to, and let go of tiles far away', async () => {
    const keys = Array.from({ length: 20 }, newKey);
    const texts = keys.map((_, i) => `bubble ${i} ` + 'some words in a chat message. '.repeat(12));
    let scroll: ScrollView | null = null;
    await render(
      <View style={{ height: 400 }}>
        <ScrollView ref={(r) => { scroll = r; }}>
          <Column>
            {keys.map((key, i) => (
              <StreamText key={key} source={buffer(texts[i], TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
            ))}
          </Column>
        </ScrollView>
      </View>,
    );
    const last = keys[keys.length - 1];
    expect((await waitForText(last, texts[texts.length - 1])).tiles).toBe(0); // far below the screen
    expect((await snapshot(keys[0])).tiles).toBeGreaterThan(0);
    scroll!.scrollToEnd({ animated: false });
    await waitForSnapshot(last, (s) => expect(s.tiles).toBeGreaterThan(0));
    await waitForSnapshot(keys[0], (s) => expect(s.tiles).toBe(0));
  });

  it('lays out like <Text> beside it', async () => {
    const { heights, onLayout } = layouts();
    const key = newKey();
    await render(
      <Column>
        <StreamText source={buffer(PARAGRAPHS, TSB.done)} nativeID={key} recordDisplayLog onLayout={onLayout('stream')} {...TEXT_STYLE} />
        <Text onLayout={onLayout('text')} style={TEXT_STYLE}>
          {PARAGRAPHS}
        </Text>
      </Column>,
    );
    expect((await waitForText(key, PARAGRAPHS)).mode).toBe('canvas');
    await waitFor(() => expect(heights.stream).toBe(heights.text));
  });
});
