import React from 'react';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText, type StreamTextHandle, type StreamTextStatus } from 'react-native-text-stream';
import { buffer, Column, newKey, snapshot, TEXT_STYLE, TSB, waitForSnapshot, write } from './helpers';

type Format = 'plain' | 'markdown';

describe('StreamText Markdown ownership seams', () => {
  it('keeps an imperative source through same-length prop replacement and a format toggle', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const oldProp = buffer('**prop-one**', TSB.done);
    const nextProp = buffer('**prop-two**', TSB.done); // Same byte length: identity must still win.
    const view = (source: ArrayBuffer, format: Format) => (
      <Column><StreamText ref={ref} source={source} nativeID={key} recordDisplayLog format={format} {...TEXT_STYLE} /></Column>
    );
    const mounted = await render(view(oldProp, 'markdown'));
    ref.current!.attach(buffer('**by-ref**', TSB.done));
    await waitForSnapshot(key, s => expect(s.text).toBe('by-ref\n'));
    await mounted.rerender(view(nextProp, 'markdown'));
    await waitForSnapshot(key, s => expect(s.text).toBe('prop-two\n'));
    ref.current!.attach(buffer('**kept**', TSB.done));
    await waitForSnapshot(key, s => expect(s.text).toBe('kept\n'));
    await mounted.rerender(view(nextProp, 'plain'));
    await waitForSnapshot(key, s => expect(s.text).toBe('**kept**'));
  });

  it('reformats a retained imperative terminal source across flavor and style changes', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: [StreamTextStatus, number][] = [];
    const view = (flavor: 'commonmark' | 'gfm', fontSize: number) => (
      <Column><StreamText ref={ref} nativeID={key} recordDisplayLog format="markdown" markdownFlavor={flavor}
        fontSize={fontSize} lineHeight={fontSize + 6} onStatusChange={(s, bytes) => events.push([s, bytes])} /></Column>
    );
    const mounted = await render(view('commonmark', 14));
    ref.current!.attach(buffer('**retained**', TSB.done));
    const first = await waitForSnapshot(key, s => expect(s.text).toBe('retained\n'));
    // The initial terminal callback must already belong to the first render. Otherwise a
    // delayed callback could make the reformat assertion pass without exercising its event.
    await waitFor(() => expect(events.at(-1)).toEqual(['done', 12]));
    events.length = 0;
    await mounted.rerender(view('gfm', 26));
    await waitForSnapshot(key, s => {
      expect(s.text).toBe('retained\n');
      expect(s.height).toBeGreaterThan(first.height);
    });
    await waitFor(() => expect(events.at(-1)).toEqual(['done', 12]));
  });

  for (const [name, status, expected] of [
    ['done', TSB.done, 'done'],
    ['error', TSB.error, 'error'],
    ['cancelled', TSB.cancelled, 'cancelled'],
    ['truncated', TSB.overflow, 'truncated'],
  ] as const) {
    it(`renders an empty Markdown ${name} terminal before reporting it`, async () => {
      const key = newKey();
      const events: [StreamTextStatus, number][] = [];
      await render(<Column><StreamText source={buffer('', status)} nativeID={key} recordDisplayLog format="markdown"
        onStatusChange={(s, bytes) => events.push([s, bytes])} {...TEXT_STYLE} /></Column>);
      await waitForSnapshot(key, s => { expect(s.text).toBe(''); expect(s.status).toBe(expected); });
      expect(events).toEqual([[expected, 0]]);
    });
  }

  it('does not revive a queued Markdown parse after clear', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    await render(<Column><StreamText ref={ref} nativeID={key} recordDisplayLog format="markdown" {...TEXT_STYLE} /></Column>);
    const pending = buffer();
    ref.current!.attach(pending);
    write(pending, '**queued markdown** '.repeat(400));
    ref.current!.clear();
    await waitForSnapshot(key, s => { expect(s.text).toBe(''); expect(s.status).toBe('idle'); });
    await new Promise<void>(resolve => setTimeout(resolve, 100));
    expect((await snapshot(key)).text).toBe('');
  });
});
