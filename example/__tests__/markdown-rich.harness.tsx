import React from 'react';
import { Platform } from 'react-native';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText, streamFetch, type StreamTextHandle } from 'react-native-text-stream';
import { buffer, busyWait, Column, itIOS, newKey, sleep, testing, TSB, waitForSnapshot, write } from './helpers';

type Block = {
  kind: 'table' | 'image'; start: number; width: number; height: number;
  rowCount: number; columnCount: number;
  cells: { row: number; column: number; text: string; header: boolean; alignment: number }[];
  url: string; alt: string; status: string; pixelWidth: number; pixelHeight: number;
};
type Attributes = { text: string; runs: { start: number; end: number; color: string; mono: boolean }[]; blocks: Block[] };
const host = Platform.OS === 'android' ? '10.0.2.2' : '127.0.0.1';
const endpoint = `http://${host}:9882`;
const table = '| Name | Value | Notes |\n| :--- | :---: | ---: |\n' +
  '| **Alpha** | 42 | A long description that must wrap without losing its final word ZEBRA |\n' +
  '| Привет | 🚀 | 世界 |\n';
async function attributes(key: string): Promise<Attributes> {
  return JSON.parse(await testing.textAttributes(key));
}
async function waitAttributes(key: string, check: (value: Attributes) => void) {
  let value!: Attributes;
  await waitFor(async () => { value = await attributes(key); check(value); }, { timeout: 7000, interval: 40 });
  return value;
}
async function imageStatus(key: string, status: string) {
  return waitAttributes(key, value => {
    expect(value.blocks.length).toBe(1);
    expect(value.blocks[0]!.kind).toBe('image');
    expect(value.blocks[0]!.status).toBe(status);
  });
}
const element = (key: string, source: ArrayBuffer, width = 300, format: 'markdown' | 'plain' = 'markdown') =>
  <Column width={width}><StreamText source={source} nativeID={key} recordDisplayLog
    format={format} markdownFlavor="gfm" fontSize={16} color="#112233" /></Column>;

describe('StreamText rich Markdown native seams', () => {
  it('lays out an aligned grid with styled Unicode cells and wraps on resize', async () => {
    const key = newKey();
    const source = buffer(table, TSB.done);
    const view = await render(element(key, source));
    const wide = await waitAttributes(key, value => {
      expect(value.blocks.length).toBe(1);
      const block = value.blocks[0]!;
      expect(block.kind).toBe('table');
      expect(block.rowCount).toBe(3);
      expect(block.columnCount).toBe(3);
      expect(block.cells.map(cell => cell.alignment).slice(0, 3)).toEqual([1, 2, 3]);
      expect(block.cells.slice(0, 3).every(cell => cell.header)).toBe(true);
      expect(block.cells[3]!.text).toBe('Alpha');
      expect(block.cells[5]!.text.endsWith('ZEBRA')).toBe(true);
      expect(block.cells[7]!.text).toBe('🚀');
      expect(block.width).toBeGreaterThan(0);
      expect(block.width).toBeLessThanOrEqual(301);
      expect(block.height).toBeGreaterThan(60);
    });
    await view.rerender(element(key, source, 190));
    await waitAttributes(key, value => {
      expect(value.blocks[0]!.width).toBeLessThanOrEqual(191);
      expect(value.blocks[0]!.height).toBeGreaterThan(wide.blocks[0]!.height);
      expect(value.blocks[0]!.cells).toEqual(wide.blocks[0]!.cells);
    });
  });

  it('materializes a large table across native language boundaries', async () => {
    const key = newKey();
    const header = '| ' + Array.from({ length: 8 }, (_, i) => `H${i}`).join(' | ') + ' |\n';
    const separator = '| ' + Array(8).fill('---').join(' | ') + ' |\n';
    const rows = Array.from({ length: 256 }, (_, row) => '| ' +
      Array.from({ length: 8 }, (_, column) => `R${row}C${column}🚀`).join(' | ') + ' |\n').join('');
    await render(element(key, buffer(header + separator + rows, TSB.done)));
    await waitAttributes(key, value => {
      const block = value.blocks[0]!;
      expect(block.rowCount).toBe(257);
      expect(block.cells.length).toBe(2056);
      expect(block.cells[2055]!.text).toBe('R255C7🚀');
    });
  });

  it('updates table metadata even when its document placeholder stays identical', async () => {
    const key = newKey();
    const first = buffer('| Old |\n| --- |\n| 111 |\n', TSB.done);
    const second = buffer('| New |\n| --- |\n| 222 |\n', TSB.done);
    const view = await render(element(key, first));
    await waitAttributes(key, value => expect(value.blocks[0]!.cells[1]!.text).toBe('111'));
    await view.rerender(element(key, second));
    await waitAttributes(key, value => expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['New', '222']));
  });

  it('styles fenced syntax while preserving raw source and unknown languages', async () => {
    const key = newKey();
    const raw = '```typescript\nconst answer: string = "hello"; // note\n```\n\n```unknown\nconst plain = 42\n```\n';
    const source = buffer();
    const view = await render(element(key, source));
    for (const part of [raw.slice(0, 7), raw.slice(7, 26), raw.slice(26)]) write(source, part);
    testing.setStatus(source, TSB.done);
    await waitAttributes(key, value => {
      const covers = (word: string, different: boolean) => {
        const start = value.text.indexOf(word);
        expect(start).toBeGreaterThanOrEqual(0);
        expect(value.runs.some(run => run.start <= start && run.end >= start + word.length && run.mono &&
          (different ? run.color !== '#112233FF' : run.color === '#112233FF'))).toBe(true);
      };
      covers('const', true);
      covers('string', true);
      covers('"hello"', true);
      covers('// note', true);
      covers('const plain = 42', false);
    });
    await view.rerender(element(key, source, 300, 'plain'));
    await waitForSnapshot(key, state => expect(state.text).toBe(raw));
  });

  it('loads and measures images natively after source completion, without another terminal event', async () => {
    const key = newKey();
    const events: string[] = [];
    const url = `${endpoint}/image.png?delay=1200&key=${key}`;
    await render(<Column><StreamText source={buffer(`![checkerboard](${url})`, TSB.done)}
      nativeID={key} recordDisplayLog format="markdown" onStatusChange={status => events.push(status)} /></Column>);
    await waitForSnapshot(key, state => expect(state.status).toBe('done'));
    expect((await attributes(key)).blocks[0]!.status).not.toBe('loaded');
    const loaded = await imageStatus(key, 'loaded');
    expect(loaded.blocks[0]!.pixelWidth).toBe(160);
    expect(loaded.blocks[0]!.pixelHeight).toBe(80);
    expect(loaded.blocks[0]!.width / loaded.blocks[0]!.height).toBe(2);
    expect(events.filter(status => status === 'done')).toEqual(['done']);
  });

  itIOS('does not crop an inline loaded image to an explicit text lineHeight', async () => {
    const key = newKey();
    const url = `${endpoint}/image.png?key=${key}`;
    await render(<Column width={300}><StreamText source={buffer(`Before ![checkerboard](${url}) after`, TSB.done)}
      nativeID={key} recordDisplayLog format="markdown" markdownFlavor="gfm" fontSize={16} lineHeight={28} /></Column>);
    const image = await imageStatus(key, 'loaded');
    expect(image.blocks[0]!.height).toBeGreaterThanOrEqual(80);
    await waitForSnapshot(key, state => expect(state.height).toBeGreaterThanOrEqual(image.blocks[0]!.height));
  });

  itIOS('keeps quote indentation while measuring an inline image beyond text lineHeight', async () => {
    const key = newKey();
    const url = `${endpoint}/large.png?key=${key}`;
    await render(<Column width={300}><StreamText source={buffer(`> Before ![checkerboard](${url}) after`, TSB.done)}
      nativeID={key} recordDisplayLog format="markdown" markdownFlavor="gfm" fontSize={16} lineHeight={28} /></Column>);
    const image = await imageStatus(key, 'loaded');
    // The quote adds an indent; the attachment must fit its text column, not the full container.
    expect(image.blocks[0]!.width).toBeLessThan(300);
    expect(image.blocks[0]!.height).toBeGreaterThan(100);
    await waitForSnapshot(key, state => expect(state.height).toBeGreaterThanOrEqual(image.blocks[0]!.height));
  });

  it('finishes image loading while JavaScript is blocked', async () => {
    const key = newKey();
    await render(element(key, buffer(`![native](${endpoint}/image.png?delay=600&key=${key})`, TSB.done)));
    await waitAttributes(key, value => expect(value.blocks.length).toBe(1));
    busyWait(1600);
    expect((await attributes(key)).blocks[0]!.status).toBe('loaded');
  });

  it('keeps a pending image subscription when later text appends after its unchanged prefix', async () => {
    const key = newKey();
    const source = buffer(`![prefix](${endpoint}/image.png?delay=1000&key=${key})\n\n`);
    await render(element(key, source));
    await imageStatus(key, 'loading');
    write(source, '**tail one**\n\n');
    await waitForSnapshot(key, state => expect(state.text).toContain('tail one'));
    write(source, 'tail two');
    testing.setStatus(source, TSB.done);
    await imageStatus(key, 'loaded');
    const counts = await (await fetch(`${endpoint}/counts?key=${key}`)).json();
    expect(counts.count).toBe(1);
  });

  it('keeps image completion layout live across a pending font-style change', async () => {
    const key = newKey();
    const source = buffer(`![style](${endpoint}/image.png?delay=1200&key=${key})`, TSB.done);
    const view = await render(element(key, source));
    await imageStatus(key, 'loading');
    await view.rerender(<Column width={300}><StreamText source={source} nativeID={key} recordDisplayLog
      format="markdown" markdownFlavor="gfm" fontSize={22} lineHeight={28} /></Column>);
    const value = await imageStatus(key, 'loaded');
    await waitForSnapshot(key, state => expect(state.height).toBeGreaterThanOrEqual(value.blocks[0]!.height));
    const counts = await (await fetch(`${endpoint}/counts?key=${key}`)).json();
    expect(counts.count).toBe(1);
  });

  for (const [path, status] of [['broken.png', 'error'], ['invalid.png', 'error'], ['oversize.png', 'oversize'], ['oversize-chunked.png', 'oversize']] as const) {
    it(`shows a stable fallback for ${path}`, async () => {
      const key = newKey();
      await render(element(key, buffer(`![fallback](${endpoint}/${path}?key=${key})`, TSB.done)));
      const value = await imageStatus(key, status);
      expect(value.blocks[0]!.alt).toBe('fallback');
      expect(value.blocks[0]!.height).toBeGreaterThan(0);
      await waitForSnapshot(key, state => expect(state.status).toBe('done'));
    });
  }

  it('blocks non-HTTP images', async () => {
    const key = newKey();
    await render(element(key, buffer('![blocked](file:///private/tmp/image.png)', TSB.done)));
    await imageStatus(key, 'blocked');
  });

  it('downsamples large images within decoded pixel limits', async () => {
    const key = newKey();
    await render(element(key, buffer(`![large](${endpoint}/large.png?key=${key})`, TSB.done)));
    const value = await imageStatus(key, 'loaded');
    const image = value.blocks[0]!;
    expect(image.pixelWidth).toBeLessThanOrEqual(2048);
    expect(image.pixelHeight).toBeLessThanOrEqual(2048);
    expect(image.pixelWidth * image.pixelHeight).toBeLessThanOrEqual(4000000);
  });

  it('reuses a decoded image across remounts and does not send request credentials', async () => {
    const key = newKey();
    const source = buffer(`![cached](${endpoint}/image.png?key=${key})`, TSB.done);
    const first = await render(element(key, source));
    await imageStatus(key, 'loaded');
    first.unmount();
    const secondKey = newKey();
    await render(element(secondKey, source));
    await imageStatus(secondKey, 'loaded');
    const counts = await (await fetch(`${endpoint}/counts?key=${key}`)).json();
    expect(counts).toEqual({ count: 1, authorization: false, cookie: false });
  });

  it('isolates image credentials from an authenticated stream and its response cookies', async () => {
    const key = newKey();
    const source = streamFetch(`${endpoint}/credential-markdown?key=${key}`, {
      headers: { Authorization: 'Bearer fixture-token', Cookie: 'fixture=session' },
    });
    expect((await source.response).status).toBe(200);
    await render(<Column><StreamText source={source} nativeID={key} recordDisplayLog format="markdown" /></Column>);
    await imageStatus(key, 'loaded');
    const streamCounts = await (await fetch(`${endpoint}/counts?key=${key}-stream`)).json();
    expect(streamCounts).toEqual({ count: 1, authorization: true, cookie: true });
    const imageCounts = await (await fetch(`${endpoint}/counts?key=${key}`)).json();
    expect(imageCounts).toEqual({ count: 1, authorization: false, cookie: false });
    expect((await source.done).status).toBe('done');
  });

  it('shares an in-flight download between subscribers and limits active requests to two', async () => {
    const group = newKey();
    const keys = Array.from({ length: 5 }, () => newKey());
    const urls = [0, 0, 1, 2, 3].map(index => `${endpoint}/image.png?delay=500&key=${group}-${index}&group=${group}`);
    await render(<Column>{keys.map((key, index) => <StreamText key={key} nativeID={key} recordDisplayLog
      source={buffer(`![shared](${urls[index]})`, TSB.done)} format="markdown" />)}</Column>);
    for (const key of keys) await imageStatus(key, 'loaded');
    const counts = await (await fetch(`${endpoint}/counts?key=${group}-0`)).json();
    expect(counts.count).toBe(1);
    const activity = await (await fetch(`${endpoint}/activity?key=${group}`)).json();
    expect(activity.peak).toBeLessThanOrEqual(2);
    expect(activity.peak).toBeGreaterThan(0);
  });

  for (const action of ['replace', 'clear', 'plain', 'unmount'] as const) {
    it(`ignores delayed image completion after ${action}`, async () => {
      const key = newKey();
      const ref = React.createRef<StreamTextHandle>();
      const raw = `![stale](${endpoint}/image.png?delay=1000&key=${key})`;
      const source = buffer(raw, TSB.done);
      const view = await render(<Column><StreamText ref={ref} source={source} nativeID={key}
        recordDisplayLog format="markdown" /></Column>);
      await waitAttributes(key, value => expect(value.blocks.length).toBe(1));
      if (action === 'replace') ref.current!.attach(buffer('**replacement**', TSB.done));
      else if (action === 'clear') ref.current!.clear();
      else if (action === 'plain') await view.rerender(element(key, source, 300, 'plain'));
      else view.unmount();
      await sleep(1400);
      if (action !== 'unmount') {
        const value = await attributes(key);
        expect(value.blocks ?? []).toEqual([]);
        expect(value.text).toBe(action === 'replace' ? 'replacement\n' : action === 'plain' ? raw : '');
      } else {
        const nextKey = newKey();
        await render(element(nextKey, buffer('**alive**', TSB.done)));
        await waitForSnapshot(nextKey, state => expect(state.text).toBe('alive\n'));
      }
    });
  }
});
