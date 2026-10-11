import React from 'react';
import { afterEach, describe, expect, it, render, waitFor } from 'react-native-harness';
import { fetch as nitroFetch } from 'react-native-nitro-fetch';
import { StreamText, useStreamTextRef, type StreamTextRef, type StreamTextStatus } from 'react-native-text-stream';
import { bufferText, committedBytes, headerStatus } from 'text-stream-testing';
import { arrayBuffer, buffer, Column, layouts, newKey, snapshot, TEXT_STYLE, testing, TSB, utf8, waitForSnapshot } from './helpers';

// Seams, live: each test crosses a boundary between two layers that were written separately and
// checks that both sides agree on the same data.
const SERVER = 'http://localhost:9877';

afterEach(() => testing.stopProducers());

describe('seams', () => {
  it('producer (background writes) ↔ renderer ↔ JS reader: the same text, cut at odd sizes', async () => {
    const text = 'Привет 🚀 漢字 é ' + 'mixed ✨ текст 𝄞 '.repeat(80);
    const key = newKey();
    const tsb = buffer();
    // 7-byte chunks split multi-byte characters across appends all the time.
    testing.startProducer(tsb, arrayBuffer(utf8(text)), 7, 2, TSB.done);
    await render(<Column><StreamText source={tsb} nativeID={key} recordDisplayLog {...TEXT_STYLE} /></Column>);
    const s = await waitForSnapshot(key, (x) => expect(x.status).toBe('done'), 8000);
    expect(s.text).toBe(text);
    expect(bufferText(tsb)).toBe(text);
    expect(committedBytes(tsb)).toBe(utf8(text).length);
    expect(headerStatus(tsb)).toBe('done');
  });

  it('renderer ↔ shadow node ↔ Yoga: the laid-out height is the height the renderer reported', async () => {
    const key = newKey();
    const { heights, onLayout } = layouts();
    const tsb = buffer();
    testing.startProducer(tsb, arrayBuffer(utf8('line of text\n'.repeat(30))), 13, 5, TSB.done);
    await render(<Column><StreamText source={tsb} nativeID={key} recordDisplayLog onLayout={onLayout('s')} {...TEXT_STYLE} /></Column>);
    const s = await waitForSnapshot(key, (x) => expect(x.status).toBe('done'), 5000);
    await waitFor(() => expect(heights.s).toBe(s.height));
    expect(s.height).toBe(31 * TEXT_STYLE.lineHeight); // 30 lines and the empty one after the last \n
  });

  it('renderer ↔ event ↔ JS wrapper: the view, onStatusChange and getStatus() agree', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    const events: [StreamTextStatus, number][] = [];
    function Screen() {
      ref = useStreamTextRef();
      return <StreamText ref={ref} nativeID={key} recordDisplayLog onStatusChange={(st, b) => events.push([st, b])} {...TEXT_STYLE} />;
    }
    await render(<Screen />);
    const tsb = buffer();
    ref.attach(tsb);
    testing.startProducer(tsb, arrayBuffer(utf8('x'.repeat(300))), 30, 5, TSB.cancelled);
    await waitFor(() => expect(events.at(-1)?.[0]).toBe('cancelled'), { timeout: 5000 });
    const s = await snapshot(key);
    expect(s.status).toBe('cancelled');
    expect(ref.getStatus()).toEqual({ status: 'cancelled', bytes: 300 });
    expect(events.at(-1)).toEqual(['cancelled', 300]);
  });

  it('nitro-fetch (the real producer) ↔ renderer: what the server sent is what the view shows', async () => {
    const reachable = await globalThis.fetch(`${SERVER}/?fmt=raw&bytes=10&tps=0`).then(
      (r) => r.ok,
      () => false,
    );
    if (!reachable) throw new Error(`Needs the example stream server on ${SERVER}: npm --prefix example run stream-server`);

    const url = `${SERVER}/?fmt=raw&bytes=6000&chunk=7&tps=400`;
    const expected = await (await globalThis.fetch(url)).text();
    const res = await nitroFetch(url, { stream: true, nativeBuffer: true });
    const key = newKey();
    await render(<Column><StreamText source={res.nativeBuffer} nativeID={key} recordDisplayLog {...TEXT_STYLE} /></Column>);
    const s = await waitForSnapshot(key, (x) => expect(x.status).toBe('done'), 15000);
    expect(s.text).toBe(expected);
    expect(bufferText(res.nativeBuffer as ArrayBuffer)).toBe(expected);
    expect(committedBytes(res.nativeBuffer as ArrayBuffer)).toBe(utf8(expected).length);
  });
});
