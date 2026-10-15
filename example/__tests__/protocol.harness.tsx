import React from 'react';
import { Platform } from 'react-native';
import { beforeAll, describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText, streamFetch } from 'react-native-text-stream';
import { busyWait, Column, newKey, testing, waitForSnapshot } from './helpers';

// Android's emulator reaches the host directly; iOS simulators share localhost.
const SERVER = Platform.OS === 'android' ? 'http://10.0.2.2:9881' : 'http://127.0.0.1:9881';
const output = 'Helло\u0000 🚀';
const utf8Bytes = (value: string) => new TextEncoder().encode(value).byteLength;

const parser = {
  sse: { format: 'sse' as const, textPath: '/message/content' },
  ndjson: { format: 'ndjson' as const, textPath: '/message/content' },
};

async function expectTerminalBeforeHang(stream: ReturnType<typeof streamFetch>, expected: 'done' | 'error') {
  try {
    const result = await Promise.race([
      stream.done,
      new Promise<{ status: 'hung' }>(resolve => setTimeout(() => resolve({ status: 'hung' }), 3000)),
    ]);
    expect(result.status).toBe(expected);
  } finally {
    // A failed assertion must not leave the fixture's intentionally open socket around.
    stream.abort();
    await stream.done;
  }
}

beforeAll(async () => {
  try {
    await fetch(`${SERVER}/health`);
  } catch {
    throw new Error(`protocol tests need server/protocol-server.mjs at ${SERVER}`);
  }
});

describe('streamFetch native protocol parsers', () => {
  it('extracts fragmented CRLF SSE JSON across BOM, multi-data events and escaped NUL', async () => {
    const stream = streamFetch(`${SERVER}/sse/generic`, { parser: parser.sse });
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toBe('**bold** and `code`');
  });

  it('keeps generic SSE data when no JSON Pointer is requested', async () => {
    const stream = streamFetch(`${SERVER}/sse/generic`, { parser: { format: 'sse' } });
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toContain('"message"');
    expect(stream.text()).toContain('**bold');
  });

  it('extracts fragmented NDJSON and accepts the final complete line at EOF', async () => {
    const stream = streamFetch(`${SERVER}/ndjson/generic`, { parser: parser.ndjson });
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toBe(output);
    const eof = streamFetch(`${SERVER}/ndjson/eof`, { parser: parser.ndjson });
    expect(await eof.done).toEqual({ status: 'done' });
    expect(eof.text()).toBe('last line');
  });

  it('extracts a root JSON string through the empty JSON Pointer', async () => {
    const stream = streamFetch(`${SERVER}/ndjson/root`, { parser: { format: 'ndjson', textPath: '' } });
    expect(await stream.done).toEqual({ status: 'done' });
    expect(stream.text()).toBe('root value');
  });

  for (const provider of ['openai-chat', 'openai-responses', 'anthropic', 'ollama'] as const) {
    it(`extracts text and ignores non-text frames from ${provider}`, async () => {
      const stream = streamFetch(`${SERVER}/provider/${provider}`, { parser: { provider } });
      expect(await stream.done).toEqual({ status: 'done' });
      expect(stream.text()).toBe(output);
    });

    it(`stops ${provider} errors before socket EOF`, async () => {
      const stream = streamFetch(`${SERVER}/provider/${provider}?error=1&hang=1`, { parser: { provider } });
      const result = await Promise.race([
        stream.done,
        new Promise<{ status: 'hung' }>(resolve => setTimeout(() => resolve({ status: 'hung' }), 3000)),
      ]);
      // Always release the fixture socket if an assertion fails.
      stream.abort();
      expect(result.status).toBe('error');
      expect(stream.text()).toBe('');
    });
  }

  for (const provider of ['openai-chat', 'openai-responses', 'anthropic', 'ollama'] as const) {
    it(`stops at the ${provider} terminal marker even if the socket stays open`, async () => {
      const stream = streamFetch(`${SERVER}/provider/${provider}?hang=1`, { parser: { provider } });
      await expectTerminalBeforeHang(stream, 'done');
      expect(stream.text()).toBe(output);
    });
  }

  it('does native progress while JavaScript is blocked and renders extracted Markdown', async () => {
    const key = newKey();
    const stream = streamFetch(`${SERVER}/sse/generic`, { parser: parser.sse });
    const statuses: Array<[string, number]> = [];
    await render(<Column><StreamText source={stream} nativeID={key} recordDisplayLog format="markdown" markdownFlavor="gfm"
      onStatusChange={(status, bytes) => statuses.push([status, bytes])} /></Column>);
    await stream.response;
    busyWait(800);
    // This is the first JS turn after the block: the renderer and parser had to progress natively.
    const native = await testing.snapshot(key);
    expect(native.status).toBe('done');
    expect(native.text.trimEnd()).toBe('bold and code');
    expect(await stream.done).toEqual({ status: 'done' });
    await waitFor(() => expect(statuses.at(-1)).toEqual(['done', utf8Bytes('**bold** and `code`')]), { timeout: 3000, interval: 20 });
    const attrs = JSON.parse(await testing.textAttributes(key)) as { text: string; runs: Array<{ start: number; end: number; bold: boolean; mono: boolean }> };
    const word = (name: string, check: (run: { start: number; end: number; bold: boolean; mono: boolean }) => boolean) => {
      const start = attrs.text.indexOf(name);
      expect(start).toBeGreaterThanOrEqual(0);
      expect(attrs.runs.some(run => run.start <= start && run.end >= start + name.length && check(run))).toBe(true);
    };
    word('bold', run => run.bold);
    word('code', run => run.mono);
  });

  it('uses parser configuration as request identity for the same declarative stream request', async () => {
    const streamId = newKey();
    const key = newKey();
    const request = (parser: { format: 'sse'; textPath?: string }) => ({ url: `${SERVER}/sse/generic`, streamId, parser });
    const view = (current: { format: 'sse'; textPath?: string }) => <Column>
      <StreamText streamId={streamId} request={request(current)} nativeID={key} recordDisplayLog />
    </Column>;
    const rendered = await render(view({ format: 'sse' }));
    await waitForSnapshot(key, state => expect(state.text).toContain('"message"'));
    await rendered.rerender(view(parser.sse));
    await waitForSnapshot(key, state => expect(state.text.trimEnd()).toBe('**bold** and `code`'));
  });

  it('rejects malformed, oversize, provider-error and non-2xx protocol responses', async () => {
    const malformed = streamFetch(`${SERVER}/sse/malformed`, { parser: parser.sse });
    expect((await malformed.done).status).toBe('error');
    const oversize = streamFetch(`${SERVER}/sse/oversize`, { parser: { ...parser.sse, maxEventBytes: 64 } });
    expect((await oversize.done).status).toBe('error');
    const providerError = streamFetch(`${SERVER}/provider/openai-chat?error=1`, { parser: { provider: 'openai-chat' } });
    expect((await providerError.done).status).toBe('error');
    const non2xx = streamFetch(`${SERVER}/status?code=429`, { parser: parser.sse });
    expect((await non2xx.response).status).toBe(429);
    expect((await non2xx.done).status).toBe('error');
    expect(non2xx.text()).toBe('');
  });

  it('skips missing/null JSON Pointer values but rejects a value of the wrong type', async () => {
    const skipped = streamFetch(`${SERVER}/ndjson/missing`, { parser: parser.ndjson });
    expect(await skipped.done).toEqual({ status: 'done' });
    expect(skipped.text()).toBe('');
    const wrong = streamFetch(`${SERVER}/ndjson/wrong-type`, { parser: parser.ndjson });
    expect((await wrong.done).status).toBe('error');
  });

  it('discards an unterminated SSE event at EOF but reports missing provider terminals', async () => {
    const generic = streamFetch(`${SERVER}/sse/unterminated`, { parser: parser.sse });
    expect(await generic.done).toEqual({ status: 'done' });
    expect(generic.text()).toBe('');
    const provider = streamFetch(`${SERVER}/provider/openai-chat?no-terminal=1`, { parser: { provider: 'openai-chat' } });
    expect((await provider.done).status).toBe('error');
    expect(provider.text()).toBe(output);
  });

  it('keeps completed NDJSON records but never flushes an unterminated one on idle timeout', async () => {
    const stream = streamFetch(`${SERVER}/ndjson/partial`, { parser: parser.ndjson, timeout: 150 });
    expect((await stream.done).status).toBe('error');
    expect(stream.text()).toBe('kept');
    expect(stream.text()).not.toContain('discarded');
  });

  it('counts only extracted UTF-8 bytes for capacity and can be cancelled cleanly', async () => {
    const exact = streamFetch(`${SERVER}/ndjson/exact`, { parser: parser.ndjson, capacity: utf8Bytes('é🚀') });
    expect(await exact.done).toEqual({ status: 'done' });
    expect(exact.text()).toBe('é🚀');
    const overflow = streamFetch(`${SERVER}/ndjson/exact`, { parser: parser.ndjson, capacity: utf8Bytes('é🚀') - 1 });
    expect((await overflow.done).status).toBe('truncated');

    const hanging = streamFetch(`${SERVER}/provider/openai-chat?no-terminal=1&hang=1`, { parser: { provider: 'openai-chat' } });
    await waitFor(() => expect(hanging.text().length).toBeGreaterThan(0), { timeout: 3000, interval: 20 });
    hanging.abort();
    expect(await hanging.done).toEqual({ status: 'cancelled' });
  });
});
