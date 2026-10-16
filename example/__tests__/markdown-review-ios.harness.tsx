import React from 'react';
import { Platform } from 'react-native';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import { buffer, Column, newKey, testing, TSB, waitForSnapshot, write, sleep } from './helpers';

const describeIOS = Platform.OS === 'ios' ? describe : describe.skip;
const preparedSample = '- [x] shipped\n\n```ts\nconst value = 1;\n```\n';
const tableSample = '| A | B |\n| --- | --- |\n| ~~old~~ | new |\n';
const imageEndpoint = 'http://127.0.0.1:9882';
const view = (key: string, source: ArrayBuffer, width = 300, fontSize = 16, color = '#222222') => <Column width={width}><StreamText
  source={source} nativeID={key} recordDisplayLog format="markdown" markdownFlavor="gfm" fontSize={fontSize} color={color} /></Column>;

type Diagnostics = { markdownRevision: number; markdownPatchCount: number; markdownLastPatchStart: number };
const diagnostics = async (key: string) => JSON.parse(await testing.textAttributes(key)) as Diagnostics;

describeIOS('iOS Markdown review regressions', () => {
  it('restyles color during streaming without restarting the parser or accepting stale worker colors', async () => {
    testing.setPreparedLayout(false);
    const key = newKey();
    const source = buffer('Initial **bold** text.\n\n');
    const mounted = await render(view(key, source));
    await waitForSnapshot(key, state => expect(state.text).toContain('Initial bold text.'));
    const before = await diagnostics(key);
    write(source, 'New streamed words.');
    await mounted.rerender(view(key, source, 300, 16, '#F0F0F0'));
    await waitForSnapshot(key, state => {
      expect(state.text).toContain('New streamed words.');
      expect(state.color).toBe('#F0F0F0FF');
    });
    await sleep(100);
    expect((await diagnostics(key)).markdownRevision).toBe(before.markdownRevision);
    const styled = JSON.parse(await testing.textAttributes(key)) as { runs: Array<{ color: string }> };
    expect(styled.runs.every(run => run.color === '#F0F0F0FF')).toBe(true);
  });

  it('resizes rich blocks without parsing the document again', async () => {
    testing.setPreparedLayout(false);
    const key = newKey();
    const source = buffer(tableSample, TSB.done);
    const mounted = await render(view(key, source));
    await waitForSnapshot(key, state => expect(state.status).toBe('done'));
    const before = await diagnostics(key);
    for (const width of [290, 270, 250, 220]) {
      await mounted.rerender(view(key, source, width));
      await waitFor(async () => {
        const value = JSON.parse(await testing.textAttributes(key)) as { blocks: Array<{ width: number }> };
        expect(value.blocks[0]!.width).toBe(width);
      });
    }
    const after = await diagnostics(key);
    expect(after.markdownRevision).toBe(before.markdownRevision);
    expect(after.markdownPatchCount).toBe(before.markdownPatchCount);
  });

  it('keeps a small append to a long open Unicode paragraph out of the retained prefix', async () => {
    testing.setPreparedLayout(false);
    const key = newKey();
    const prefix = '漢字とかな 👩🏽‍💻 streaming text. '.repeat(400) + 'stable';
    const source = buffer(prefix);
    await render(view(key, source));
    await waitForSnapshot(key, state => expect(state.text).toBe(prefix + '\n'));
    write(source, 'tail');
    await waitForSnapshot(key, state => expect(state.text).toBe(prefix + 'tail\n'));
    const after = await diagnostics(key);
    expect(after.markdownLastPatchStart).toBeGreaterThan(prefix.length - 1024);
  });

  it('keeps the same in-flight image request while its geometry is resized', async () => {
    testing.setPreparedLayout(false);
    const key = newKey();
    const source = buffer(`> ![resizing](${imageEndpoint}/image.png?delay=1500&key=${key})`, TSB.done);
    const mounted = await render(view(key, source));
    await waitFor(async () => {
      const value = JSON.parse(await testing.textAttributes(key)) as { blocks: Array<{ status: string }> };
      expect(value.blocks[0]!.status).toBe('loading');
      expect((await (await fetch(`${imageEndpoint}/counts?key=${key}`)).json()).count).toBe(1);
    });
    for (const width of [280, 250, 220]) await mounted.rerender(view(key, source, width));
    await waitFor(async () => {
      const value = JSON.parse(await testing.textAttributes(key)) as { blocks: Array<{ status: string; width: number }> };
      expect(value.blocks[0]!.status).toBe('loaded');
      expect(value.blocks[0]!.width).toBeLessThan(220);
    }, { timeout: 7000, interval: 40 });
    expect((await (await fetch(`${imageEndpoint}/counts?key=${key}`)).json()).count).toBe(1);
  });

  it('keeps tasks and code attributes when a prepared canvas promotes to TextKit', async () => {
    testing.setPreparedLayout(true);
    testing.clearPreparedLayouts();
    const source = buffer(preparedSample, TSB.done);
    const warmKey = newKey();
    const warm = await render(view(warmKey, source));
    await waitForSnapshot(warmKey, state => expect(state.status).toBe('done'));
    warm.unmount();
    await waitFor(() => expect(testing.preparedCacheCount()).toBeGreaterThan(0), { timeout: 3000, interval: 30 });
    const key = newKey();
    const mounted = await render(view(key, source));
    await waitForSnapshot(key, state => expect(state.mode).toBe('canvas'));
    testing.setPreparedLayout(false);
    await mounted.rerender(view(key, source, 220, 20));
    await waitForSnapshot(key, state => expect(state.mode).toBe('textView'));
    await waitFor(async () => {
      const value = JSON.parse(await testing.textAttributes(key)) as {
        text: string;
        attachments: Array<{ kind: string; checked?: boolean }>;
        runs: Array<{ start: number; end: number; mono: boolean; codeBlock: boolean; paragraphSpacingBefore: number }>;
      };
      expect(value.text).toContain('const value');
      expect(value.attachments).toEqual(expect.arrayContaining([{ kind: 'task', checked: true }]));
      const codeStart = value.text.indexOf('const value');
      expect(value.runs.some(run => run.start <= codeStart && run.end > codeStart && run.mono && run.codeBlock &&
        run.paragraphSpacingBefore >= 26)).toBe(true);
    }, { timeout: 5000, interval: 40 });
    await waitForSnapshot(key, state => expect(state.accessibilityValue).toContain('Checked task shipped'));
  });

  it('preserves table strike attributes and a loaded quote image through TextKit rebuilds', async () => {
    testing.setPreparedLayout(false);
    const tableKey = newKey();
    const table = buffer(tableSample, TSB.done);
    const tableView = await render(view(tableKey, table));
    await tableView.rerender(view(tableKey, table, 220, 20));
    await waitFor(async () => {
      const value = JSON.parse(await testing.textAttributes(tableKey)) as {
        blocks: Array<{ kind: string; strikeCount?: number }>;
      };
      expect(value.blocks.some(block => block.kind === 'table')).toBe(true);
      expect(value.blocks.some(block => block.kind === 'table' && (block.strikeCount ?? 0) > 0)).toBe(true);
    }, { timeout: 5000, interval: 40 });

    const plainTableKey = newKey();
    await render(view(plainTableKey, buffer('| A | B |\n| --- | --- |\n| plain | text |\n', TSB.done)));
    await waitFor(async () => {
      const value = JSON.parse(await testing.textAttributes(plainTableKey)) as { blocks: Array<{ kind: string; strikeCount?: number }> };
      expect(value.blocks[0]).toEqual(expect.objectContaining({ kind: 'table', strikeCount: 0 }));
    }, { timeout: 5000, interval: 40 });

    const requestKey = newKey();
    const source = buffer(`> ![quote image](${imageEndpoint}/image.png?key=${requestKey})`, TSB.done);
    const key = newKey();
    const mounted = await render(view(key, source));
    await waitFor(async () => {
      const value = JSON.parse(await testing.textAttributes(key)) as { blocks: Array<{ kind: string; status: string }> };
      expect(value.blocks[0]).toEqual(expect.objectContaining({ kind: 'image', status: 'loaded' }));
    }, { timeout: 7000, interval: 40 });
    await mounted.rerender(view(key, source, 220, 20));
    await waitFor(async () => {
      const value = JSON.parse(await testing.textAttributes(key)) as { blocks: Array<{ kind: string; status: string; width: number }> };
      expect(value.blocks[0]).toEqual(expect.objectContaining({ kind: 'image', status: 'loaded' }));
      expect(value.blocks[0]!.width).toBeLessThan(220);
    }, { timeout: 7000, interval: 40 });
    const counts = await (await fetch(`${imageEndpoint}/counts?key=${requestKey}`)).json();
    expect(counts.count).toBe(1);
  });
});
