import React from 'react';
import { Platform } from 'react-native';
import {
  beforeAll,
  describe,
  expect,
  it,
  render,
  waitFor,
} from 'react-native-harness';
import {
  releaseStream,
  StreamText,
  streamFetch,
  type StreamTextHandle,
} from 'react-native-text-stream';
import { Column, newKey, sleep, TEXT_STYLE, waitForSnapshot } from './helpers';

const SERVER = 'http://127.0.0.1:9878';
const TEXT = 'Привет é 漢字 🚀\n';
beforeAll(async () => {
  await fetch(`${SERVER}/empty`);
});

describe('streamFetch regressions', () => {
  it('preserves UTF-8 across every single-byte network chunk and removes only the initial BOM', async () => {
    const stream = streamFetch(`${SERVER}/bytes?chunk=1`);
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toBe(TEXT);
  });

  for (const capacity of [1, 3, 4, 5]) {
    it(`distinguishes exact EOF from overflow at capacity ${capacity}`, async () => {
      const exact = streamFetch(
        `${SERVER}/bytes?text=${'x'.repeat(capacity)}`,
        { capacity },
      );
      expect(await exact.done).toEqual({ status: 'done' });
      expect(exact.text()).toBe('x'.repeat(capacity));
      const overflow = streamFetch(
        `${SERVER}/bytes?text=${'x'.repeat(capacity + 1)}`,
        { capacity },
      );
      expect(await overflow.done).toEqual({ status: 'truncated' });
      const prefix = overflow.text();
      overflow.abort();
      await sleep(20);
      expect(overflow.text()).toBe(prefix);
    });
  }

  it('does not replace an existing stream when the new signal is already aborted', async () => {
    const id = newKey();
    const first = streamFetch(`${SERVER}/stall`, { streamId: id });
    await first.response;
    const controller = new AbortController();
    controller.abort();
    const second = streamFetch(`${SERVER}/echo`, {
      streamId: id,
      body: 'never send',
      signal: controller.signal,
    });
    expect(await second.done).toEqual({ status: 'cancelled' });
    first.abort();
    expect(await first.done).toEqual({ status: 'cancelled' });
    releaseStream(id);
  });

  it('settles queued cancellation while all other same-host streams remain open', async () => {
    const running = Array.from({ length: 5 }, () =>
      streamFetch(`${SERVER}/stall`),
    );
    try {
      await Promise.all(running.map(s => s.response));
      const queued = streamFetch(`${SERVER}/stall`);
      queued.abort();
      const result = await Promise.race([
        queued.done,
        sleep(1500).then(() => ({ status: 'hung' })),
      ]);
      expect(result).toEqual({ status: 'cancelled' });
    } finally {
      running.forEach(s => s.abort());
      await Promise.all(running.map(s => s.done));
    }
  });

  it('bounds initial response and idle reads, preserving partial body on timeout', async () => {
    const first = streamFetch(`${SERVER}/headers-delay`, { timeout: 150 });
    await expect(first.response).rejects.toThrow();
    expect((await first.done).status).toBe('error');
    const idle = streamFetch(`${SERVER}/stall`, { timeout: 200 });
    await idle.response;
    expect((await idle.done).status).toBe('error');
    expect(idle.text()).toBe('prefix');
  });

  it('keeps a partial body when the transport fails after headers', async () => {
    const stream = streamFetch(`${SERVER}/broken`);
    expect((await stream.response).status).toBe(200);
    expect((await stream.done).status).toBe('error');
    expect(stream.text()).toBe('prefix');
  });

  for (const encoding of ['identity', 'gzip']) {
    it(`preserves explicit ${encoding} and always sends UTF-8 request bytes`, async () => {
      const stream = streamFetch(`${SERVER}/echo`, {
        body: 'é\u0000🚀',
        headers: {
          'Accept-Encoding': encoding,
          'Content-Type': 'text/plain; charset=iso-8859-1',
        },
      });
      expect(await stream.done).toEqual({ status: 'done' });
      const echo = JSON.parse(stream.text());
      expect(echo.hex).toBe('c3a900f09f9a80');
      expect(echo.headers['accept-encoding']).toBe(encoding);
      const compressed = streamFetch(`${SERVER}/gzip`, {
        headers: { 'Accept-Encoding': encoding },
      });
      expect(await compressed.done).toEqual({ status: 'done' });
      expect(compressed.text()).toBe(TEXT);
    });
  }

  it('preserves the platform decoder outcome for malformed gzip', async () => {
    // URLSession tolerates a false gzip header and ignores gzip CRC; OkHttp rejects both.
    // done describes transport completion, not an application-level integrity check.
    for (const path of ['bad-gzip', 'bad-gzip-crc']) {
      const stream = streamFetch(`${SERVER}/${path}`);
      expect((await stream.done).status).toBe(Platform.OS === 'ios' ? 'done' : 'error');
      if (Platform.OS === 'ios') {
        // Match what URLSession delivers, not a custom decompressor's output. In iOS 26.5 a
        // false gzip header yields an empty body; an invalid CRC still yields decoded bytes.
        const platformBody = await (await fetch(`${SERVER}/${path}`)).text();
        expect(stream.text()).toBe(platformBody);
      }
    }
  });

  for (const status of [301, 302, 303, 307, 308]) {
    it(`follows ${status} redirects with platform-consistent POST handling`, async () => {
      const stream = streamFetch(`${SERVER}/redirect?status=${status}`, {
        body: 'body',
      });
      expect((await stream.response).url).toBe(`${SERVER}/echo`);
      expect(await stream.done).toEqual({ status: 'done' });
      const echo = JSON.parse(stream.text());
      expect(echo.method).toBe(status >= 307 ? 'POST' : 'GET');
      expect(echo.hex).toBe(status >= 307 ? '626f6479' : '');
    });
  }

  it('completes empty HEAD, 204 and 304 responses', async () => {
    for (const init of [{ method: 'HEAD' }, {}]) {
      for (const status of [204, 304]) {
        const stream = streamFetch(`${SERVER}/empty?status=${status}`, init);
        expect(await stream.done).toEqual({ status: 'done' });
        expect(stream.text()).toBe('');
      }
    }
  });

  it('refs read a request stream and keep its final text after release', async () => {
    const key = newKey();
    const id = newKey();
    const ref = React.createRef<StreamTextHandle>();
    await render(
      <Column>
        <StreamText
          ref={ref}
          streamId={id}
          request={{ url: `${SERVER}/bytes` }}
          nativeID={key}
          recordDisplayLog
          {...TEXT_STYLE}
        />
      </Column>,
    );
    await waitForSnapshot(
      key,
      s => expect([s.status, s.text]).toEqual(['done', TEXT]),
      8000,
    );
    await sleep(50);
    expect(ref.current!.getStatus().status).toBe('done');
    releaseStream(id);
    expect(await ref.current!.getText()).toBe(TEXT);
  });

  it('a truncated multibyte character has the same replacement in text() and the native view', async () => {
    const key = newKey();
    const stream = streamFetch(
      `${SERVER}/bytes?text=${encodeURIComponent('🚀')}`,
      { capacity: 3 },
    );
    await render(
      <Column>
        <StreamText
          source={stream}
          nativeID={key}
          recordDisplayLog
          {...TEXT_STYLE}
        />
      </Column>,
    );
    expect(await stream.done).toEqual({ status: 'truncated' });
    expect(stream.text()).toBe('\uFFFD');
    await waitForSnapshot(key, s =>
      expect([s.status, s.text]).toEqual(['truncated', '\uFFFD']),
    );
  });

  it('startup errors wake a view already subscribed to the streamId', async () => {
    const id = newKey(),
      key = newKey();
    await render(
      <Column>
        <StreamText
          streamId={id}
          nativeID={key}
          recordDisplayLog
          {...TEXT_STYLE}
        />
      </Column>,
    );
    const stream = streamFetch('not a url', { streamId: id });
    expect((await stream.done).status).toBe('error');
    await expect(stream.response).rejects.toThrow();
    await waitForSnapshot(key, s => expect(s.status).toBe('error'));
    releaseStream(id);
  });

  it('rapid replacements and old abort handles leave only the newest stream on screen and in refs', async () => {
    const id = newKey(),
      key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    await render(
      <Column>
        <StreamText
          ref={ref}
          streamId={id}
          nativeID={key}
          recordDisplayLog
          {...TEXT_STYLE}
        />
      </Column>,
    );
    const old = Array.from({ length: 10 }, () =>
      streamFetch(`${SERVER}/stall`, { streamId: id }),
    );
    const latest = streamFetch(`${SERVER}/bytes?text=latest`, { streamId: id });
    old.forEach(stream => stream.abort());
    expect(await Promise.all(old.map(stream => stream.done))).toEqual(
      old.map(() => ({ status: 'cancelled' })),
    );
    expect(await latest.done).toEqual({ status: 'done' });
    await waitForSnapshot(key, s =>
      expect([s.status, s.text]).toEqual(['done', 'latest']),
    );
    await waitFor(() => expect(ref.current!.getStatus().status).toBe('done'));
    expect(await ref.current!.getText()).toBe('latest');
    releaseStream(id);
  });

  it('declarative remount restarts after release and after completion eviction', async () => {
    const id = newKey();
    const request = { url: `${SERVER}/bytes?text=retained` };
    const mount = async () => {
      const key = newKey();
      const rendered = await render(
        <Column>
          <StreamText
            streamId={id}
            request={request}
            nativeID={key}
            recordDisplayLog
            {...TEXT_STYLE}
          />
        </Column>,
      );
      await waitForSnapshot(key, s =>
        expect([s.status, s.text]).toEqual(['done', 'retained']),
      );
      await rendered.unmount();
    };
    await mount();
    releaseStream(id);
    await mount();
    const ids = Array.from({ length: 70 }, () => newKey());
    await Promise.all(
      ids.map(streamId => streamFetch(`${SERVER}/empty`, { streamId }).done),
    );
    await mount();
    [...ids, id].forEach(releaseStream);
  });
});
