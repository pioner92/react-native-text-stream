import React from 'react';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText, type StreamTextStatus } from 'react-native-text-stream';
import { buffer, Column, newKey, snapshot, TEXT_STYLE, testing, TSB, utf8, waitForText, write } from './helpers';

type Event = [StreamTextStatus, number];

function recorder() {
  const events: Event[] = [];
  return { events, onStatusChange: (status: StreamTextStatus, bytes: number) => events.push([status, bytes]) };
}

describe('StreamText: status events', () => {
  it('reports streaming, then done with the byte count once the text is on screen', async () => {
    const { events, onStatusChange } = recorder();
    const key = newKey();
    const tsb = buffer();
    await render(
      <Column>
        <StreamText source={tsb} nativeID={key} recordDisplayLog onStatusChange={onStatusChange} {...TEXT_STYLE} />
      </Column>,
    );
    await waitFor(() => expect(events).toEqual([['streaming', 0]]));
    write(tsb, 'abc');
    testing.setStatus(tsb, TSB.done);
    await waitFor(() => expect(events).toEqual([['streaming', 0], ['done', 3]]));
    expect((await snapshot(key)).text).toBe('abc');
  });

  const finals: [string, number, StreamTextStatus][] = [
    ['done', TSB.done, 'done'],
    ['error', TSB.error, 'error'],
    ['cancelled', TSB.cancelled, 'cancelled'],
    ['overflow', TSB.overflow, 'truncated'],
  ];
  for (const [name, code, status] of finals) {
    it(`maps a ${name} buffer to "${status}"`, async () => {
      const { events, onStatusChange } = recorder();
      const tsb = buffer();
      await render(<StreamText source={tsb} onStatusChange={onStatusChange} {...TEXT_STYLE} />);
      write(tsb, 'x');
      testing.setStatus(tsb, code);
      await waitFor(() => expect(events.at(-1)).toEqual([status, 1]));
    });
  }

  it('writing past the capacity truncates: the text stops at the capacity', async () => {
    const { events, onStatusChange } = recorder();
    const key = newKey();
    const tsb = testing.createBuffer(4);
    await render(<StreamText source={tsb} nativeID={key} recordDisplayLog onStatusChange={onStatusChange} {...TEXT_STYLE} />);
    write(tsb, 'abc');
    write(tsb, 'defgh'); // does not fit: rejected whole, and the buffer is marked overflow
    await waitFor(() => expect(events.at(-1)).toEqual(['truncated', 3]));
    await waitForText(key, 'abc');
  });

  it('a buffer that is already finished reports only its final status', async () => {
    const { events, onStatusChange } = recorder();
    await render(<StreamText source={buffer('done already', TSB.done)} onStatusChange={onStatusChange} {...TEXT_STYLE} />);
    await waitFor(() => expect(events).toEqual([['done', 12]]));
  });

  it('switching from one finished buffer to another reports done again', async () => {
    const { events, onStatusChange } = recorder();
    const element = (source: ArrayBuffer) => (
      <StreamText source={source} onStatusChange={onStatusChange} {...TEXT_STYLE} />
    );
    const { rerender } = await render(element(buffer('first', TSB.done)));
    await waitFor(() => expect(events).toEqual([['done', 5]]));
    await rerender(element(buffer('second!', TSB.done)));
    await waitFor(() => expect(events).toEqual([['done', 5], ['done', 7]]));
  });

  it('removing the source reports idle and clears the view', async () => {
    const { events, onStatusChange } = recorder();
    const key = newKey();
    const element = (source?: ArrayBuffer) => (
      <StreamText source={source} nativeID={key} recordDisplayLog onStatusChange={onStatusChange} {...TEXT_STYLE} />
    );
    const { rerender } = await render(element(buffer('text', TSB.done)));
    await waitForText(key, 'text');
    await rerender(element(undefined));
    await waitFor(() => expect(events.at(-1)).toEqual(['idle', 0]));
    const s = await snapshot(key);
    expect(s.text).toBe('');
    expect(s.height).toBe(0);
  });

  it('a stream that ends mid-character shows U+FFFD and still reports done', async () => {
    const { events, onStatusChange } = recorder();
    const key = newKey();
    const tsb = buffer(utf8('ok🚀').subarray(0, 4));
    await render(<StreamText source={tsb} nativeID={key} recordDisplayLog onStatusChange={onStatusChange} {...TEXT_STYLE} />);
    testing.setStatus(tsb, TSB.done);
    await waitFor(() => expect(events.at(-1)).toEqual(['done', 4]));
    await waitForText(key, 'ok�');
  });
});
