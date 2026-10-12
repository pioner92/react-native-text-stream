import React from 'react';
import { Dimensions, ScrollView, View } from 'react-native';
import { describe, expect, it, render } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import { buffer, newKey, sleep, snapshot, testing, TEXT_STYLE, TSB, waitForSnapshot } from './helpers';

type Memory = { bytes: number; tiles: number; canvases: number; offWindow: number; footprintMB: number };
const memory = async (): Promise<Memory> => JSON.parse(await testing.tileMemory());

const PARAGRAPH = 'Streaming text straight from the network into a native view, every token lands in a buffer. ';
const BIG = (PARAGRAPH.repeat(8) + '\n\n').repeat(65); // ~49 KB

// Tiles are 256 pt high; a canvas draws 512 pt past each edge of the screen and keeps 1024 pt.
const { width, height, scale } = Dimensions.get('window');
const bytesFor = (points: number) => (points + 2 * 256) * width * scale * scale * 4;
const ON_SCREEN = bytesFor(height);
const AT_MOST = bytesFor(height + 2 * 1024);

describe('StreamText: canvas tile memory', () => {
  it('a full-screen canvas holds the screen and its margins, only the screen out of the window or after a memory warning', async () => {
    const key = newKey();
    let scroll: ScrollView | null = null;
    await render(
      <ScrollView style={{ flex: 1 }} ref={(r) => { scroll = r; }}>
        <StreamText source={buffer(BIG, TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </ScrollView>,
    );
    const s = await waitForSnapshot(key, (x) => expect(x.mode).toBe('canvas'));
    const middle = s.height / 2;
    for (const y of [middle, middle + 1000, middle - 1000, middle]) {
      scroll!.scrollTo({ y, animated: false });
      await sleep(250);
    }
    const scrolled = await memory();
    expect(scrolled.bytes).toBeGreaterThan(ON_SCREEN);
    expect(scrolled.bytes).toBeLessThanOrEqual(AT_MOST);

    // Covered by another screen: what was on screen stays, and shows at once on the way back.
    await testing.cover(true);
    const covered = await memory();
    expect(covered.offWindow).toBe(1);
    expect(covered.bytes).toBeGreaterThan(0);
    expect(covered.bytes).toBeLessThanOrEqual(ON_SCREEN);
    await testing.cover(false);
    expect((await snapshot(key)).tiles).toBeGreaterThan(0);

    // A memory warning: only the screen, and nothing drawn ahead until the user scrolls.
    await sleep(250);
    testing.simulateMemoryWarning();
    await sleep(250);
    const warned = await memory();
    expect(warned.bytes).toBeGreaterThan(0);
    expect(warned.bytes).toBeLessThanOrEqual(ON_SCREEN);
    scroll!.scrollTo({ y: middle + 100, animated: false });
    await sleep(250);
    expect((await memory()).bytes).toBeGreaterThan(ON_SCREEN);

    // Out of the window during a memory warning: nothing, and the screen drawn again on the way back.
    await testing.cover(true);
    testing.simulateMemoryWarning();
    await sleep(100);
    expect((await memory()).bytes).toBe(0);
    await testing.cover(false);
    expect((await snapshot(key)).tiles).toBeGreaterThan(0);
  });

  it('120 bubbles scrolled through hold no more than one screen and its margins', async () => {
    const keys = Array.from({ length: 120 }, () => newKey());
    let list: ScrollView | null = null;
    let contentHeight = 0;
    await render(
      <ScrollView style={{ flex: 1 }} ref={(r) => { list = r; }} onContentSizeChange={(_w, h) => (contentHeight = h)}>
        {keys.map((k, i) => (
          <View key={k} style={{ alignSelf: i % 2 ? 'flex-end' : 'flex-start', maxWidth: '85%', padding: 8 }}>
            <StreamText source={buffer(PARAGRAPH.repeat(1 + (i % 9) * 3), TSB.done)} nativeID={k} recordDisplayLog {...TEXT_STYLE} />
          </View>
        ))}
      </ScrollView>,
    );
    for (const k of keys) await waitForSnapshot(k, (x) => expect(x.mode).toBe('canvas'));
    for (let y = 0; y < contentHeight; y += 800) {
      list!.scrollTo({ y, animated: false });
      await sleep(30);
    }
    await sleep(250);
    const scrolled = await memory();
    expect(scrolled.canvases).toBe(120);
    expect(scrolled.bytes).toBeGreaterThan(0);
    expect(scrolled.bytes).toBeLessThanOrEqual(AT_MOST);
    await testing.cover(true);
    expect((await memory()).bytes).toBeLessThanOrEqual(ON_SCREEN);
    await testing.cover(false);
  });
});
