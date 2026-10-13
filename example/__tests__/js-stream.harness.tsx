import { beforeAll, describe, expect, it, waitFor } from 'react-native-harness';
import { streamTextOverXhr, utf8ByteLength } from '../src/js-stream';
import { sleep } from './helpers';

const SERVER = 'http://localhost:9877';
const slowUrl = (bytes: number, tps = 20, chunk = 16) =>
  `${SERVER}/?fmt=raw&bytes=${bytes}&chunk=${chunk}&tps=${tps}`;

beforeAll(async () => {
  try {
    await fetch(`${SERVER}/time`);
  } catch {
    throw new Error(`XHR stream tests need the stream server at ${SERVER}`);
  }
});

describe('XHR JS streaming baseline', () => {
  it('delivers a delta before completion and preserves UTF-8 text and byte count', async () => {
    const chunks: string[] = [];
    let complete = false;
    const request = streamTextOverXhr(slowUrl(600), (text) => {
      chunks.push(text);
    });
    request.done.then(() => (complete = true));

    await waitFor(() => {
      expect(chunks.length).toBeGreaterThan(0);
      expect(complete).toBe(false);
    }, { timeout: 3_000, interval: 20 });

    const result = await request.done;
    expect(chunks.join('')).toBe(result.text);
    expect(result.bytes).toBe(utf8ByteLength(result.text));
    expect(result.text).toContain('⚡️');
  });

  it('stops a live request and rejects as AbortError', async () => {
    const chunks: string[] = [];
    const request = streamTextOverXhr(slowUrl(50_000, 10, 256), (text) => {
      chunks.push(text);
    });
    await waitFor(() => expect(chunks.length).toBeGreaterThan(0), { timeout: 3_000, interval: 20 });
    request.abort();
    await expect(request.done).rejects.toMatchObject({ name: 'AbortError' });
    const count = chunks.length;
    await sleep(250);
    expect(chunks).toHaveLength(count);
  });

  it('rejects a non-2xx response', async () => {
    const request = streamTextOverXhr(`${SERVER}/status?code=404`, () => {});
    await expect(request.done).rejects.toThrow('HTTP 404');
  });
});
