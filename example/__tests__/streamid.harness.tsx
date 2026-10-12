import React from 'react';
import { beforeAll, describe, expect, it, render, waitFor } from 'react-native-harness';
import { abortStream, releaseStream, StreamText, streamFetch } from 'react-native-text-stream';
import { Column, newKey, sleep, snapshot, TEXT_STYLE, waitForSnapshot, waitForText } from './helpers';

// streamFetch(url, { streamId }) and <StreamText streamId />: the request and the view meet in a
// native registry, by an id the app chooses, in any order. Against server/stream-server.mjs.
const SERVER = 'http://localhost:9877';
const streamUrl = (bytes: number, tps = 0, chunk = 512) => `${SERVER}/?fmt=raw&bytes=${bytes}&chunk=${chunk}&tps=${tps}`;
const textOf = async (url: string) => (await fetch(url)).text();
let ids = 0;
const newStreamId = () => `test-stream-${Date.now()}-${++ids}`;

beforeAll(async () => {
  try {
    await fetch(`${SERVER}/time`);
  } catch {
    throw new Error(`streamId tests need the stream server at ${SERVER}`);
  }
});

function View({ id, viewKey }: { id: string; viewKey: string }) {
  return (
    <Column>
      <StreamText streamId={id} nativeID={viewKey} recordDisplayLog {...TEXT_STYLE} />
    </Column>
  );
}

describe('StreamText streamId', () => {
  it('shows a stream that starts after the view mounted', async () => {
    const url = streamUrl(8000, 50, 512);
    const reference = await textOf(url);
    const id = newStreamId();
    const key = newKey();
    await render(<View id={id} viewKey={key} />);
    await sleep(100);
    const stream = streamFetch(url, { streamId: id });
    expect(await stream.done).toEqual({ status: 'done' });
    const s = await waitForText(key, reference, 8000);
    expect(s.status).toBe('done');
  });

  it('shows a finished stream mounted afterwards, laid out ahead (canvas)', async () => {
    const url = streamUrl(3000);
    const reference = await textOf(url);
    const id = newStreamId();
    expect(await streamFetch(url, { streamId: id }).done).toEqual({ status: 'done' });
    const key = newKey();
    await render(<View id={id} viewKey={key} />);
    const s = await waitForText(key, reference, 5000);
    expect(s.mode).toBe('canvas');
  });

  it('keeps a finished stream for a remount (a list scrolled back)', async () => {
    const url = streamUrl(3000);
    const reference = await textOf(url);
    const id = newStreamId();
    await streamFetch(url, { streamId: id }).done;
    const first = await render(<View id={id} viewKey={newKey()} />);
    first.unmount();
    const key = newKey();
    await render(<View id={id} viewKey={key} />);
    const s = await waitForText(key, reference, 5000);
    expect(s.mode).toBe('canvas');
  });

  it('two views on one id show the same stream', async () => {
    const url = streamUrl(5000, 50, 512);
    const reference = await textOf(url);
    const id = newStreamId();
    const a = newKey();
    const b = newKey();
    await render(
      <Column>
        <StreamText streamId={id} nativeID={a} recordDisplayLog {...TEXT_STYLE} />
        <StreamText streamId={id} nativeID={b} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    await streamFetch(url, { streamId: id }).done;
    await waitForText(a, reference, 8000);
    await waitForText(b, reference, 8000);
  });

  it('a new request under the same id replaces the one streaming', async () => {
    const slow = streamUrl(100_000, 20, 256);
    const fast = streamUrl(4000);
    const reference = await textOf(fast);
    const id = newStreamId();
    const key = newKey();
    await render(<View id={id} viewKey={key} />);
    const first = streamFetch(slow, { streamId: id });
    await waitForSnapshot(key, (s) => expect(s.text.length).toBeGreaterThan(0), 5000);
    const second = streamFetch(fast, { streamId: id });
    expect(await first.done).toEqual({ status: 'cancelled' });
    expect(await second.done).toEqual({ status: 'done' });
    const s = await waitForText(key, reference, 8000);
    expect(s.status).toBe('done');
  });

  it('abortStream(id) stops it, keeping what arrived', async () => {
    const id = newStreamId();
    const key = newKey();
    await render(<View id={id} viewKey={key} />);
    const stream = streamFetch(streamUrl(100_000, 20, 256), { streamId: id });
    await waitForSnapshot(key, (s) => expect(s.text.length).toBeGreaterThan(0), 5000);
    abortStream(id);
    expect(await stream.done).toEqual({ status: 'cancelled' });
    await waitForSnapshot(key, (s) => expect(s.status).toBe('cancelled'), 5000);
  });

  it('releaseStream(id): a view showing it keeps its text, a new one shows nothing', async () => {
    const url = streamUrl(2000);
    const reference = await textOf(url);
    const id = newStreamId();
    const shown = newKey();
    await render(<View id={id} viewKey={shown} />);
    await streamFetch(url, { streamId: id }).done;
    await waitForText(shown, reference, 5000);
    releaseStream(id);
    const later = newKey();
    await render(<View id={id} viewKey={later} />);
    await sleep(300);
    expect((await snapshot(later)).text).toBe('');
  });

  it('<StreamText request> makes the request itself and shows it', async () => {
    const url = streamUrl(6000, 50, 512);
    const reference = await textOf(url);
    const key = newKey();
    const statuses: string[] = [];
    await render(
      <Column>
        <StreamText request={{ url }} onStatusChange={(s) => statuses.push(s)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    // The response ends a moment after its last bytes are on screen.
    await waitForSnapshot(key, (x) => expect([x.text === reference, x.status]).toEqual([true, 'done']), 8000);
    await waitFor(() => expect(statuses).toEqual(['streaming', 'done']), { timeout: 3000 });
  });
});
