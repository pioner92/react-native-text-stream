import React from 'react';
import { beforeAll, describe, expect, it, render } from 'react-native-harness';
import { StreamText, streamFetch } from 'react-native-text-stream';
import { Column, newKey, sleep, TEXT_STYLE, waitForSnapshot, waitForText } from './helpers';

// streamFetch() against server/stream-server.mjs (`bun run stream-server`; on Android also
// `adb reverse tcp:9877 tcp:9877`).
const SERVER = 'http://localhost:9877';
const streamUrl = (bytes: number, tps = 0, chunk = 512) => `${SERVER}/?fmt=raw&bytes=${bytes}&chunk=${chunk}&tps=${tps}`;

beforeAll(async () => {
  try {
    await fetch(`${SERVER}/time`);
  } catch {
    throw new Error(`streamFetch tests need the stream server at ${SERVER}`);
  }
});

describe('streamFetch', () => {
  it('streams a response body into the view, and reports the response and the end', async () => {
    const url = streamUrl(20_000, 50, 1024);
    const reference = await (await fetch(url)).text();
    const key = newKey();
    const stream = streamFetch(url);
    await render(
      <Column>
        <StreamText source={stream} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    const response = await stream.response;
    expect(response.status).toBe(200);
    expect(response.ok).toBe(true);
    expect(response.headers['content-type']).toContain('text/plain');
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toBe(reference);
    const s = await waitForText(key, reference, 8000);
    expect(s.status).toBe('done');
  });

  it('sends the method, headers and body', async () => {
    const stream = streamFetch(`${SERVER}/echo`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Echo': 'token-123' },
      body: JSON.stringify({ prompt: 'Hello, мир' }),
    });
    expect((await stream.response).headers['x-echo-method']).toBe('POST');
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toBe('POST token-123\n{"prompt":"Hello, мир"}');
  });

  it('gives an HTTP error status, with its body', async () => {
    const stream = streamFetch(`${SERVER}/status?code=404`);
    const response = await stream.response;
    expect(response.status).toBe(404);
    expect(response.ok).toBe(false);
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toBe('status 404');
  });

  it('stops on abort(), keeping what arrived', async () => {
    const key = newKey();
    const stream = streamFetch(streamUrl(100_000, 20, 256));
    await render(
      <Column>
        <StreamText source={stream} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    await stream.response;
    await sleep(300);
    stream.abort();
    expect(await stream.done).toEqual({ status: 'cancelled' });
    const received = stream.text();
    expect(received.length).toBeGreaterThan(0);
    expect(received.length).toBeLessThan(100_000);
    await waitForSnapshot(key, (s) => expect(s.status).toBe('cancelled'), 5000);
  });

  it('stops on an AbortSignal', async () => {
    const controller = new AbortController();
    const stream = streamFetch(streamUrl(100_000, 20, 256), { signal: controller.signal });
    await stream.response;
    controller.abort();
    expect(await stream.done).toEqual({ status: 'cancelled' });
  });

  it('stops at its capacity, as truncated', async () => {
    const stream = streamFetch(streamUrl(50_000, 0, 1024), { capacity: 4096 });
    expect(await stream.done).toEqual({ status: 'truncated' });
    expect(new TextEncoder().encode(stream.text()).length).toBeLessThanOrEqual(4096);
  });

  it('fails without a server: the response rejects, done says error', async () => {
    const stream = streamFetch('http://localhost:9/');
    await expect(stream.response).rejects.toThrow();
    const done = await stream.done;
    expect(done.status).toBe('error');
    expect(done.error).toBeTruthy();
  });

  it('rejects an invalid URL as an error', async () => {
    const done = await streamFetch('not a url').done;
    expect(done.status).toBe('error');
  });
});
