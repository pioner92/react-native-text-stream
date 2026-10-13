import React from 'react';
import { describe, expect, fn, it, render, spyOn, waitFor } from 'react-native-harness';
import { StreamText, useStreamTextRef, type StreamTextRef } from 'react-native-text-stream';
import { buffer, Column, newKey, snapshot, TEXT_STYLE, testing, TSB, utf8, waitForSnapshot, waitForText, write } from './helpers';

// The ref path end to end: JS → StreamTextBuffers TurboModule → Fabric `attach` command →
// renderer → status event → JS. Nothing is mocked.
function Screen({ refOut, mounted = true, ...props }: { refOut: (ref: StreamTextRef) => void; mounted?: boolean } & Record<string, unknown>) {
  const streamRef = useStreamTextRef();
  refOut(streamRef);
  return <Column>{mounted ? <StreamText ref={streamRef} recordDisplayLog {...TEXT_STYLE} {...props} /> : null}</Column>;
}

describe('StreamText: ref', () => {
  it('attach() shows the buffer; getText() and getStatus() agree with the view', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    await render(<Screen refOut={(r) => (ref = r)} nativeID={key} />);
    const tsb = buffer();
    ref.attach(tsb);
    write(tsb, 'Привет, ');
    write(tsb, 'мир 🚀');
    testing.setStatus(tsb, TSB.done);
    await waitForText(key, 'Привет, мир 🚀');
    await waitFor(() => expect(ref.getStatus()).toEqual({ status: 'done', bytes: utf8('Привет, мир 🚀').length }));
    await expect(ref.getText()).resolves.toBe('Привет, мир 🚀');
  });

  it('applies an attach made before the view mounts', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    const { rerender } = await render(<Screen refOut={(r) => (ref = r)} nativeID={key} mounted={false} />);
    ref.attach(buffer('early bird', TSB.done));
    await rerender(<Screen refOut={(r) => (ref = r)} nativeID={key} />);
    await waitForText(key, 'early bird');
  });

  it('clear() empties the view: no text, no height, idle', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    const onStatusChange = fn();
    await render(<Screen refOut={(r) => (ref = r)} nativeID={key} onStatusChange={onStatusChange} />);
    ref.attach(buffer('to be cleared', TSB.done));
    await waitForText(key, 'to be cleared');
    ref.clear();
    await waitForSnapshot(key, (s) => {
      expect(s.text).toBe('');
      expect(s.height).toBe(0);
    });
    await waitFor(() => expect(onStatusChange).toHaveBeenLastCalledWith('idle', 0));
    expect(ref.getStatus()).toEqual({ status: 'idle', bytes: 0 });
  });

  it('a buffer made in JS is refused with a warning, and the view shows nothing', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    const warn = spyOn(console, 'warn').mockImplementation(() => {});
    await render(<Screen refOut={(r) => (ref = r)} nativeID={key} />);
    ref.attach(buffer('native first', TSB.done));
    await waitForText(key, 'native first');
    ref.attach(new ArrayBuffer(128));
    expect(warn).toHaveBeenCalledTimes(1);
    await waitForText(key, '');
    expect(ref.getStatus().status).toBe('idle');
    warn.mockRestore();
  });

  it('an unrelated prop change keeps what the ref attached', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    const { rerender } = await render(<Screen refOut={(r) => (ref = r)} nativeID={key} color="#FFFFFF" />);
    ref.attach(buffer('attached by ref', TSB.done));
    await waitForText(key, 'attached by ref');
    await rerender(<Screen refOut={(r) => (ref = r)} nativeID={key} color="#00FF00" />);
    const s = await waitForSnapshot(key, (x) => expect(x.color).toBe('#00FF00FF'));
    expect(s.text).toBe('attached by ref');
  });

  it('a new source prop replaces what the ref attached', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    const first = buffer('from prop', TSB.done);
    const { rerender } = await render(<Screen refOut={(r) => (ref = r)} nativeID={key} source={first} />);
    ref.attach(buffer('from ref', TSB.done));
    await waitForText(key, 'from ref');
    await rerender(<Screen refOut={(r) => (ref = r)} nativeID={key} source={buffer('newer prop', TSB.done)} />);
    await waitForText(key, 'newer prop');
  });

  it('the last of many quick attaches wins', async () => {
    let ref!: StreamTextRef;
    const key = newKey();
    await render(<Screen refOut={(r) => (ref = r)} nativeID={key} />);
    for (let i = 0; i < 20; i++) ref.attach(buffer(`buffer ${i}`, i % 2 ? TSB.done : TSB.open));
    await waitForText(key, 'buffer 19');
    // No other buffer's text is left behind.
    await new Promise<void>((r) => setTimeout(r, 200));
    expect((await snapshot(key)).text).toBe('buffer 19');
  });

  it('status events about a buffer replaced in the same tick never reach onStatusChange', async () => {
    let ref!: StreamTextRef;
    const events: [string, number][] = [];
    const key = newKey();
    await render(<Screen refOut={(r) => (ref = r)} nativeID={key} onStatusChange={(s: string, b: number) => events.push([s, b])} />);
    // The view reports "done" for the first buffer as it attaches it, after JS already moved on.
    ref.attach(buffer('first', TSB.done));
    const second = buffer();
    ref.attach(second);
    write(second, 'second!');
    testing.setStatus(second, TSB.done);
    await waitForText(key, 'second!');
    await waitFor(() => expect(events.at(-1)).toEqual(['done', 7]));
    expect(events).not.toContainEqual(['done', 5]);
    expect(ref.getStatus()).toEqual({ status: 'done', bytes: 7 });
  });
});
