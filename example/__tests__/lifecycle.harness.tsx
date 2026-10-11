import React from 'react';
import { afterEach, describe, expect, it, render } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import { createFrameMonitor } from 'text-stream-testing';
import { arrayBuffer, buffer, busyWait, Column, newKey, sleep, snapshot, TEXT_STYLE, testing, TSB, utf8, waitForSnapshot, waitForText } from './helpers';

const monitor = createFrameMonitor();

afterEach(() => testing.stopProducers());

describe('StreamText: lifecycle', () => {
  it('survives unmounting while a producer keeps writing', async () => {
    const tsb = buffer();
    testing.startProducer(tsb, arrayBuffer(utf8('streaming… '.repeat(400))), 64, 5, TSB.done);
    const { unmount } = await render(<StreamText source={tsb} {...TEXT_STYLE} />);
    await sleep(80);
    unmount();
    await sleep(300); // the producer keeps writing into the buffer nobody shows
    // The app is fine, and a new view works.
    const key = newKey();
    await render(<StreamText source={buffer('after', TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />);
    await waitForText(key, 'after');
  });

  it('a reused text view keeps nothing from its previous owner', async () => {
    for (let round = 0; round < 12; round++) {
      const styled = newKey();
      const { unmount } = await render(
        <StreamText
          source={buffer(`old text ${round}`, TSB.done)}
          nativeID={styled}
          recordDisplayLog
          fontSize={30}
          color="#FF0000"
          selectable={false}
        />,
      );
      await waitForText(styled, `old text ${round}`);
      unmount();
      await sleep(30); // let the view deallocate and return its text view to the pool

      const plain = newKey();
      await render(<StreamText source={buffer('new', TSB.done)} nativeID={plain} recordDisplayLog />);
      const s = await waitForText(plain, 'new');
      expect(s.fontSize).toBe(15);
      expect(s.color).not.toBe('#FF0000FF');
      expect(s.selectable).toBe(true);
    }
  });

  it('keeps showing text while the JS thread is blocked', async () => {
    const key = newKey();
    const tsb = buffer();
    await render(<Column><StreamText source={tsb} nativeID={key} recordDisplayLog {...TEXT_STYLE} /></Column>);
    testing.startProducer(tsb, arrayBuffer(utf8('word '.repeat(600))), 60, 20, TSB.done);
    await sleep(50);
    const blockedFrom = Date.now();
    busyWait(800);
    const blockedTo = Date.now();
    await waitForSnapshot(key, (s) => expect(s.status).toBe('done'), 5000);
    // [wallClockMs, bytesShown, ...]: when bytes reached the screen.
    const log = monitor.streamTextDisplayLog(key);
    const shownWhileBlocked = [];
    for (let i = 0; i < log.length; i += 2) {
      if (log[i]! > blockedFrom + 50 && log[i]! < blockedTo) shownWhileBlocked.push(log[i + 1]!);
    }
    expect(shownWhileBlocked.length).toBeGreaterThan(10);
    expect(shownWhileBlocked.at(-1)!).toBeGreaterThan(shownWhileBlocked[0]!);
  });

  it('switching buffers mid-stream shows only the new one', async () => {
    const key = newKey();
    const first = buffer();
    testing.startProducer(first, arrayBuffer(utf8('A'.repeat(4000))), 50, 5, TSB.done);
    const element = (source: ArrayBuffer) => <StreamText source={source} nativeID={key} recordDisplayLog {...TEXT_STYLE} />;
    const { rerender } = await render(element(first));
    await waitForSnapshot(key, (s) => expect(s.text.length).toBeGreaterThan(0));
    const second = buffer();
    testing.startProducer(second, arrayBuffer(utf8('B'.repeat(500))), 25, 5, TSB.done);
    await rerender(element(second));
    const s = await waitForSnapshot(key, (x) => expect(x.status).toBe('done'), 5000);
    expect(s.text).toBe('B'.repeat(500));
  });

  it('mounting and unmounting many views in a row is clean', async () => {
    for (let i = 0; i < 40; i++) {
      const { unmount } = await render(<StreamText source={i % 3 ? buffer(`v${i}`, TSB.done) : buffer()} {...TEXT_STYLE} />);
      unmount();
    }
    const key = newKey();
    await render(<StreamText source={buffer('last', TSB.done)} nativeID={key} recordDisplayLog />);
    await waitForText(key, 'last');
    expect((await snapshot(key)).height).toBeGreaterThan(0);
  });
});

describe('StreamText: waiting for the first token', () => {
  it('shows text that arrives long after the view mounted (the watcher backs off, then wakes)', async () => {
    const key = newKey();
    const tsb = buffer();
    await render(<StreamText source={tsb} nativeID={key} recordDisplayLog {...TEXT_STYLE} />);
    await sleep(1500); // quiet: the watcher polls less often
    const before = Date.now();
    testing.append(tsb, arrayBuffer(utf8('finally')));
    await waitForText(key, 'finally');
    expect(Date.now() - before).toBeLessThan(500);
  });
});
