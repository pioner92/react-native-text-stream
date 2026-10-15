import React from 'react';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText, type StreamTextHandle } from 'react-native-text-stream';
import { arrayBuffer, buffer, busyWait, Column, newKey, snapshot, testing, TSB, utf8, waitForSnapshot, write } from './helpers';

const sample = '# Heading\n\n**Bold** and *italic* with `code` — Привет 🚀.\n\n- first\n- second';
const nativeView = (source: ArrayBuffer, key: string, format: 'plain' | 'markdown' = 'markdown', size = 16) =>
  <Column><StreamText source={source} nativeID={key} recordDisplayLog format={format} markdownFlavor="gfm" fontSize={size} /></Column>;

const waitRendered = (key: string, text: string) => waitForSnapshot(key, s => expect(s.text.trimEnd()).toBe(text));

describe('StreamText native Markdown', () => {
  it('renders completed content and keeps getText as original Markdown', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const source = buffer('**Привет 🚀** &amp; `code`', TSB.done);
    await render(<Column><StreamText ref={ref} source={source} nativeID={key} recordDisplayLog format="markdown" /></Column>);
    const result = await waitRendered(key, 'Привет 🚀 & code');
    expect(result.height).toBeGreaterThan(0);
    await expect(ref.current!.getText()).resolves.toBe('**Привет 🚀** &amp; `code`');
  });

  it('projects an unfinished delimiter without raw syntax, then resolves it when completed', async () => {
    const key = newKey();
    const source = buffer('**hello');
    const events: [string, number][] = [];
    await render(<Column><StreamText source={source} nativeID={key} recordDisplayLog format="markdown"
      onStatusChange={(status, bytes) => events.push([status, bytes])} /></Column>);
    await waitRendered(key, 'hello');
    write(source, '**');
    testing.setStatus(source, TSB.done);
    await waitRendered(key, 'hello');
    await waitFor(() => expect(events.at(-1)).toEqual(['done', 9]));
  });

  it('withholds incomplete link and image syntax while open, then restores literals at terminal', async () => {
    const key = newKey();
    const source = buffer('Before [link](https://example.com');
    const view = await render(nativeView(source, key));
    await waitForSnapshot(key, state => {
      expect(state.status).toBe('streaming');
      expect(state.text).toContain('Before link');
      expect(state.text).not.toContain('[');
      expect(state.text).not.toContain('https://');
    });
    testing.setStatus(source, TSB.done);
    await waitRendered(key, 'Before [link](https://example.com');

    const image = buffer('Image ![alt](https://example.com/a.png');
    await view.rerender(nativeView(image, key));
    await waitForSnapshot(key, state => {
      expect(state.text).toContain('Image alt');
      expect(state.text).not.toContain('![');
      expect(state.text).not.toContain('https://');
    });
    testing.setStatus(image, TSB.done);
    await waitRendered(key, 'Image ![alt](https://example.com/a.png');
  });

  it('suppresses an unfinished GFM table tail and restores it after a reset', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const source = buffer('| Name | Value |\n| --- | --- |\n| partial');
    await render(<Column><StreamText ref={ref} source={source} nativeID={key} recordDisplayLog
      format="markdown" markdownFlavor="gfm" /></Column>);
    await waitForSnapshot(key, state => {
      expect(state.status).toBe('streaming');
      expect(state.text).not.toContain('| partial');
    });
    ref.current!.clear();
    await waitForSnapshot(key, state => { expect(state.text).toBe(''); expect(state.height).toBe(0); });
  });

  it('reformats earlier reference text when its definition arrives', async () => {
    const key = newKey();
    const source = buffer('A [reference][id].\n\n');
    await render(nativeView(source, key));
    await waitRendered(key, 'A [reference][id].');
    write(source, '[id]: https://example.com\n');
    testing.setStatus(source, TSB.done);
    await waitRendered(key, 'A reference.');
  });

  it('flushes a small final update even when the producer stays open', async () => {
    const key = newKey();
    const source = buffer('**first**');
    await render(nativeView(source, key));
    await waitRendered(key, 'first');
    write(source, ' and **second**');
    // No terminal status and no later chunk may be required to trigger this update.
    await waitRendered(key, 'first and second');
  });

  it('ends a chunked fenced block with the same layout as one-shot Markdown', async () => {
    const code = '```ts\nconst answer: number = 42;\nconsole.log(answer);\n```\n';
    const oneShotKey = newKey();
    await render(nativeView(buffer(code, TSB.done), oneShotKey));
    const oneShot = await waitForSnapshot(oneShotKey, state => expect(state.status).toBe('done'));

    const streamedKey = newKey();
    const streamed = buffer();
    await render(nativeView(streamed, streamedKey));
    write(streamed, '```ts\nconst answer: number = 42;\n');
    await waitForSnapshot(streamedKey, state => {
      expect(state.status).toBe('streaming');
      expect(state.text).toContain('const answer: number = 42;');
    });
    write(streamed, 'console.log(answer);\n```\n');
    testing.setStatus(streamed, TSB.done);
    const completed = await waitForSnapshot(streamedKey, state => {
      expect(state.status).toBe('done');
      expect(state.text).toContain('const answer: number = 42;');
    });
    expect(completed.height).toBe(oneShot.height);
  });

  it('changes format of an imperatively attached finished source', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const original = buffer('**original**', TSB.done);
    const events: string[] = [];
    const view = (format: 'plain' | 'markdown') => <Column><StreamText ref={ref} source={original} nativeID={key}
      recordDisplayLog format={format} onStatusChange={status => events.push(status)} /></Column>;
    const { rerender } = await render(view('markdown'));
    ref.current!.attach(buffer('**retained**', TSB.done));
    await waitRendered(key, 'retained');
    events.length = 0;
    await rerender(view('plain'));
    await waitRendered(key, '**retained**');
    await waitFor(() => expect(events.at(-1)).toBe('done'));
    events.length = 0;
    await rerender(view('markdown'));
    await waitRendered(key, 'retained');
    await waitFor(() => expect(events.at(-1)).toBe('done'));
    await expect(ref.current!.getText()).resolves.toBe('**retained**');
  });

  it('keeps native updates working while JavaScript is blocked, across UTF-8 splits', async () => {
    const key = newKey();
    const source = buffer();
    await render(nativeView(source, key));
    testing.startProducer(source, arrayBuffer(utf8('**Привет 🚀**')), 1, 3, TSB.done);
    busyWait(800);
    // Snapshot immediately after the blocking interval; no React updates delivered the chunks.
    const current = await snapshot(key);
    expect(current.text.trimEnd()).toBe('Привет 🚀');
    await waitForSnapshot(key, s => expect(s.status).toBe('done'));
  });

  it('renders useful intermediate snapshots during a continuous native stream', async () => {
    const key = newKey();
    const source = buffer();
    const text = '**continuous** paragraph 🚀.\n\n'.repeat(1800);
    await render(nativeView(source, key));
    testing.startProducer(source, arrayBuffer(utf8(text)), 128, 2, TSB.done);
    await waitForSnapshot(key, s => {
      expect(s.status).toBe('streaming');
      expect(s.text).toContain('continuous paragraph');
      expect(s.text.length).toBeGreaterThan(100);
    });
    await waitForSnapshot(key, s => {
      expect(s.status).toBe('done');
      expect(s.text).not.toContain('**');
      expect(s.text.match(/continuous/g)?.length).toBe(1800);
    }, 10000);
  });

  it('switches plain/Markdown on a finished source and updates its size', async () => {
    const key = newKey();
    const source = buffer(sample, TSB.done);
    const { rerender } = await render(nativeView(source, key));
    const first = await waitForSnapshot(key, s => {
      expect(s.text).toContain('Heading');
      expect(s.text).not.toContain('**');
    });
    await rerender(nativeView(source, key, 'plain'));
    await waitForSnapshot(key, s => expect(s.text).toBe(sample));
    await rerender(nativeView(source, key, 'markdown', 28));
    await waitForSnapshot(key, s => {
      expect(s.text).not.toContain('**');
      expect(s.height).toBeGreaterThan(first.height);
    });
  });

  it('drops stale worker results after source replacement and clear', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    await render(<Column><StreamText ref={ref} nativeID={key} recordDisplayLog format="markdown" /></Column>);
    ref.current!.attach(buffer(sample.repeat(800), TSB.done));
    ref.current!.attach(buffer('**new**', TSB.done));
    await waitRendered(key, 'new');
    ref.current!.clear();
    await waitForSnapshot(key, s => { expect(s.text).toBe(''); expect(s.height).toBe(0); });
  });

  it('keeps formatting when a completed source is mounted again', async () => {
    const source = buffer('## Cached\n\n**text**', TSB.done);
    const firstKey = newKey();
    const first = await render(nativeView(source, firstKey));
    const before = await waitForSnapshot(firstKey, s => expect(s.text).toContain('Cached'));
    first.unmount();
    const secondKey = newKey();
    await render(nativeView(source, secondKey));
    await waitForSnapshot(secondKey, s => {
      expect(s.text).toBe(before.text);
      expect(s.height).toBe(before.height);
    });
  });
});
