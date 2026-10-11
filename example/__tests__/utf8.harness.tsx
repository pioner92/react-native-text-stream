import React from 'react';
import { afterEach, describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText, useStreamTextRef, type StreamTextRef, type StreamTextStatus } from 'react-native-text-stream';
import { arrayBuffer, buffer, Column, newKey, snapshot, TEXT_STYLE, testing, TSB, utf8 } from './helpers';

afterEach(() => testing.stopProducers());

const BOM = [0xef, 0xbb, 0xbf];

function Screen({ refOut, ...props }: { refOut: (ref: StreamTextRef) => void } & Record<string, unknown>) {
  const ref = useStreamTextRef();
  refOut(ref);
  return (
    <Column>
      <StreamText ref={ref} recordDisplayLog {...TEXT_STYLE} {...props} />
    </Column>
  );
}

/** Streams `bytes` in chunks of `chunk` every `intervalMs`; returns what the view and getText() show. */
async function streamed(bytes: number[], chunk: number, intervalMs: number) {
  let ref!: StreamTextRef;
  let status: StreamTextStatus | undefined;
  const key = newKey();
  const tsb = buffer();
  await render(<Screen refOut={(r) => (ref = r)} source={tsb} nativeID={key} onStatusChange={(s: StreamTextStatus) => (status = s)} />);
  testing.startProducer(tsb, arrayBuffer(Uint8Array.from(bytes)), chunk, intervalMs, TSB.done);
  await waitFor(() => expect(status).toBe('done'), { timeout: 10_000 });
  return { view: (await snapshot(key)).text, getText: await ref.getText() };
}

/** The same bytes, finished before the view mounts: decoded in one go. */
async function whole(bytes: number[]) {
  let status: StreamTextStatus | undefined;
  const key = newKey();
  await render(
    <Column>
      <StreamText source={buffer(Uint8Array.from(bytes), TSB.done)} nativeID={key} recordDisplayLog onStatusChange={(s) => (status = s)} {...TEXT_STYLE} />
    </Column>,
  );
  await waitFor(() => expect(status).toBe('done'));
  return (await snapshot(key)).text;
}

function prng(seed: number) {
  return () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 2 ** 32;
  };
}

describe('StreamText: UTF-8 across chunk boundaries', () => {
  it('a lead byte that breaks a held sequence starts a new character', async () => {
    // Byte by byte, one per frame: E2 is held, then 41 breaks it, then E2 82 AC is a whole "€".
    const { view, getText } = await streamed([0xe2, 0x41, 0xe2, 0x82, 0xac], 1, 30);
    expect(view).toBe('�A€');
    expect(getText).toBe('�A€');
  });

  it('a U+FEFF that starts a chunk mid-stream is kept', async () => {
    const bytes = [...utf8('ABC'), ...BOM, ...utf8('D')];
    const { view } = await streamed(bytes, 3, 30);
    expect(view).toBe('ABC﻿D');
  });

  it('only the BOM opening the stream is dropped, however it arrives', async () => {
    const bytes = [...BOM, ...BOM, ...utf8('A')];
    for (const chunk of [1, 2, 3, 7]) {
      const { view, getText } = await streamed(bytes, chunk, 20);
      expect({ chunk, view, getText }).toEqual({ chunk, view: '﻿A', getText: '﻿A' });
    }
    expect(await whole(bytes)).toBe('﻿A');
  });

  it('random bytes: any chunking shows what decoding them whole shows', async () => {
    const random = prng(2026);
    for (let run = 0; run < 8; run++) {
      // Mostly bytes near the UTF-8 boundaries, plus valid multi-byte text, plus BOMs.
      const bytes: number[] = [];
      while (bytes.length < 96) {
        const r = random();
        if (r < 0.4) bytes.push(0x7f + Math.floor(random() * 0x81));
        else if (r < 0.75) bytes.push(...utf8(['ж', '€', '🚀', 'a', ' '][Math.floor(random() * 5)]!));
        else if (r < 0.8) bytes.push(...BOM);
        else bytes.push(Math.floor(random() * 256));
      }
      const chunk = 1 + Math.floor(random() * 5);
      const expected = await whole(bytes);
      const { view, getText } = await streamed(bytes, chunk, 2);
      expect({ run, chunk, view, getText }).toEqual({ run, chunk, view: expected, getText: expected });
    }
  });
});
