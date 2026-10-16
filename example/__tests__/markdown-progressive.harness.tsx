import React from 'react';
import { Platform } from 'react-native';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText, type StreamTextHandle } from 'react-native-text-stream';
import { buffer, Column, newKey, sleep, testing, TSB, waitForSnapshot, write } from './helpers';

type Block = {
  kind: string; start: number; width: number; height: number; status: string; url: string; alt: string;
  rowCount: number; columnCount: number;
  cells: { text: string; header: boolean; alignment: number }[];
};
type Attributes = { text: string; blocks: Block[]; runs: { start: number; end: number; mono: boolean; link: string }[] };
const host = Platform.OS === 'android' ? '10.0.2.2' : '127.0.0.1';
const endpoint = `http://${host}:9882`;
async function inspect(key: string, check: (value: Attributes) => void) {
  let value!: Attributes;
  await waitFor(async () => {
    value = JSON.parse(await testing.textAttributes(key));
    check(value);
  }, { timeout: 7000, interval: 40 });
  return value;
}
const element = (key: string, source: ArrayBuffer, width = 300) =>
  <Column width={width}><StreamText source={source} nativeID={key} recordDisplayLog
    format="markdown" markdownFlavor="gfm" fontSize={16} color="#112233" /></Column>;

describe('StreamText progressive rich block seams', () => {
  it('publishes readable inline and structural content without waiting for closing syntax or another chunk', async () => {
    for (const [start, continuation, expected] of [
      ['**Fir', 'st', 'First'], ['*Fir', 'st', 'First'], ['~~Fir', 'st', 'First'],
      ['`Fir', 'st', 'First'], ['[Fir', 'st', 'First'],
      ['## Fir', 'st', 'First'], ['- Fir', 'st', 'First'], ['> Fir', 'st', 'First'],
    ]) {
      const key = newKey();
      const source = buffer(start);
      const mounted = await render(element(key, source));
      await inspect(key, value => expect(value.text).toContain('Fir'));
      write(source, continuation);
      await inspect(key, value => expect(value.text).toContain(expected));
      if (start === '[Fir') {
        write(source, '](https://example.com');
        await inspect(key, value => {
          expect(value.text).toContain('First');
          expect(value.text).not.toContain('https://');
          expect(value.runs.some(run => Boolean(run.link))).toBe(false);
        });
        write(source, ')');
        await inspect(key, value => expect(value.runs.some(run => run.link === 'https://example.com')).toBe(true));
      }
      mounted.unmount();
    }
  });

  it('keeps rejected projection contexts literal as native transport chunks arrive', async () => {
    for (const prefix of [
      '    ![alt', '<!--\n| Name', '<pre>\n| Name',
      '| A |\n| --- |\n| first |\n| ![alt',
    ]) {
      const key = newKey();
      const source = buffer(prefix);
      const mounted = await render(element(key, source));
      const table = prefix.startsWith('| A');
      let terminal = false;
      let expected = prefix.includes('![alt') ? '![alt' : '| Name';
      const check = (value: Attributes) => {
        expect(value.blocks.some(block => block.kind === 'image')).toBe(false);
        if (table) {
          expect(value.blocks[0]!.kind).toBe('table');
          // A still-open image in a table is safely rendered as its readable
          // alt label. Terminal parsing restores the exact source bytes.
          expect(value.blocks[0]!.cells.at(-1)!.text).toContain(
            terminal ? expected : expected.slice(2),
          );
        } else {
          expect(value.blocks).toEqual([]);
          expect(value.text).toContain(expected);
        }
      };
      await inspect(key, check);
      for (const chunk of [' grows', ' 🚀', ' / next']) {
        expected += chunk;
        write(source, chunk);
        await inspect(key, check);
      }
      testing.setStatus(source, TSB.done);
      terminal = true;
      await inspect(key, check);
      await waitForSnapshot(key, value => expect(value.status).toBe('done'));
      mounted.unmount();
    }
  });

  it('forms header cells before the separator, then confirms alignment without losing content', async () => {
    const key = newKey();
    const source = buffer('Intro\n\n| Fea');
    await render(element(key, source));
    await inspect(key, value => {
      expect(value.blocks[0]!.kind).toBe('table');
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Fea']);
    });
    write(source, 'ture | Sta');
    const header = await inspect(key, value => {
      expect(value.blocks[0]!.columnCount).toBe(2);
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Feature', 'Sta']);
    });
    write(source, 'tus |\n| :--');
    await inspect(key, value => {
      expect(value.blocks[0]!.rowCount).toBe(1);
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Feature', 'Status']);
      expect(value.blocks[0]!.width).toBe(header.blocks[0]!.width);
      expect(value.text).not.toContain(':--');
    });
    write(source, '- | ---: |\n| Native | Rea');
    await inspect(key, value => {
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Feature', 'Status', 'Native', 'Rea']);
      expect(value.blocks[0]!.cells.slice(0, 2).map(cell => cell.alignment)).toEqual([1, 3]);
    });
    write(source, 'dy |');
    testing.setStatus(source, TSB.done);
    await inspect(key, value => expect(value.blocks[0]!.cells.map(cell => cell.text))
      .toEqual(['Feature', 'Status', 'Native', 'Ready']));
  });

  it('restores prose when a provisional header receives an invalid separator', async () => {
    const key = newKey();
    const source = buffer('| Name | Value |');
    await render(element(key, source));
    await inspect(key, value => expect(value.blocks[0]!.kind).toBe('table'));
    write(source, '\nThis is ordinary prose.');
    await inspect(key, value => {
      expect(value.blocks ?? []).toEqual([]);
      expect(value.text).toContain('| Name | Value |');
      expect(value.text).toContain('This is ordinary prose.');
    });
  });

  for (const [name, status] of [['done', TSB.done], ['error', TSB.error], ['cancelled', TSB.cancelled], ['truncated', TSB.overflow]] as const) {
    it(`restores an unconfirmed table on ${name} without a new byte`, async () => {
      const key = newKey();
      const raw = '| Name | Value |';
      const source = buffer(raw);
      await render(element(key, source));
      await inspect(key, value => expect(value.blocks[0]!.kind).toBe('table'));
      testing.setStatus(source, status);
      await inspect(key, value => {
        expect(value.blocks ?? []).toEqual([]);
        expect(value.text).toBe(raw + '\n');
      });
      await waitForSnapshot(key, value => expect(value.status).toBe(name));
    });
  }

  it('reflows a provisional header and drops it on source replacement', async () => {
    const key = newKey();
    const source = buffer('| Long descriptive feature | Current status');
    const mounted = await render(element(key, source));
    const first = await inspect(key, value => expect(value.blocks[0]!.columnCount).toBe(2));
    await mounted.rerender(element(key, source, 160));
    await inspect(key, value => {
      expect(value.blocks[0]!.width).toBeLessThanOrEqual(160);
      expect(value.blocks[0]!.cells).toEqual(first.blocks[0]!.cells);
    });
    await mounted.rerender(element(key, buffer('Replacement', TSB.done), 160));
    await inspect(key, value => {
      expect(value.blocks ?? []).toEqual([]);
      expect(value.text).toBe('Replacement\n');
    });
  });

  for (const fence of ['```', '~~~']) {
    it(`lays out an empty code surface for ${fence} before language or content arrives`, async () => {
      const key = newKey();
      const source = buffer(fence);
      const mounted = await render(element(key, source));
      await inspect(key, value => {
        expect(value.text).not.toContain(fence);
        expect(value.runs.some(run => run.mono && run.end > run.start)).toBe(true);
      });
      const empty = await waitForSnapshot(key, value => expect(value.height).toBeGreaterThan(0));
      // "py" is a valid alias, but "pyt" is not. The label must not flash
      // on/off while the still-incomplete language token grows.
      for (const fragment of ['py', 't', 'hon']) {
        write(source, fragment);
        await inspect(key, value => {
          expect(value.text).not.toContain('py');
          expect(value.runs.some(run => run.mono && run.end > run.start)).toBe(true);
        });
        await waitForSnapshot(key, value => expect(value.height).toBe(empty.height));
      }
      await inspect(key, value => {
        expect(value.text).not.toContain('python');
        expect(value.runs.some(run => run.mono && run.end > run.start)).toBe(true);
      });
      write(source, '\n');
      await inspect(key, value => {
        expect(value.runs.some(run => run.mono && run.end > run.start)).toBe(true);
        expect(value.text).not.toContain('python');
      });
      await waitForSnapshot(key, value => expect(value.height).toBeGreaterThan(0));
      write(source, 'const value = 42;');
      await inspect(key, value => expect(value.text).toContain('const value = 42;'));
      write(source, '\n' + fence + '\n\nAfter code.');
      testing.setStatus(source, TSB.done);
      await inspect(key, value => {
        expect(value.text).toContain('After code.');
        expect(value.text).not.toContain(fence);
      });
      await mounted.rerender(element(key, source, 160));
      await inspect(key, value => expect(value.text).toContain('const value = 42;'));
    });
  }

  it('shows an image before its label or URL finishes and does not fetch an incomplete URL', async () => {
    const key = newKey();
    const source = buffer('Before\n\n![');
    const url = `${endpoint}/image.png?delay=1000&key=${key}`;
    await render(element(key, source));
    const first = await inspect(key, value => {
      expect(value.blocks.length).toBe(1);
      expect(value.blocks[0]!.kind).toBe('image');
      expect(value.blocks[0]!.status).toBe('pending');
      expect(value.blocks[0]!.url).toBe('');
      expect(value.blocks[0]!.height).toBeGreaterThan(0);
    });
    write(source, 'Concept 🚀');
    await inspect(key, value => {
      expect(value.blocks[0]!.alt).toBe('Concept 🚀');
      expect(value.blocks[0]!.status).toBe('pending');
      expect(value.blocks[0]!.width).toBe(first.blocks[0]!.width);
      expect(value.blocks[0]!.height).toBe(first.blocks[0]!.height);
    });
    await waitForSnapshot(key, value => expect(value.accessibilityValue).toContain('Concept 🚀'));
    write(source, `](${url}`);
    await inspect(key, value => {
      expect(value.blocks[0]!.status).toBe('pending');
      expect(value.blocks[0]!.url).toBe('');
      expect(value.text).not.toContain('http');
    });
    // Leave the render thread time to attach: checking immediately after append
    // could miss an erroneous asynchronous download.
    await sleep(150);
    expect((await (await fetch(`${endpoint}/counts?key=${key}`)).json()).count).toBe(0);
    write(source, ')\n\nText keeps streaming while the image loads.');
    await inspect(key, value => {
      expect(value.blocks[0]!.status).toBe('loading');
      expect(value.blocks[0]!.width).toBe(first.blocks[0]!.width);
      expect(value.blocks[0]!.height).toBe(first.blocks[0]!.height);
      expect(value.text).toContain('Text keeps streaming');
    });
    testing.setStatus(source, TSB.done);
    await inspect(key, value => expect(value.blocks[0]!.status).toBe('loaded'));
    expect((await (await fetch(`${endpoint}/counts?key=${key}`)).json()).count).toBe(1);
  });

  it('shows the header before any data row and streams cells without a trailing newline', async () => {
    const key = newKey();
    const source = buffer('Intro\n\n| Feature | State |\n| :--- | ---: |\n');
    await render(element(key, source));
    const header = await inspect(key, value => {
      expect(value.blocks.length).toBe(1);
      const block = value.blocks[0]!;
      expect(block.kind).toBe('table');
      expect(block.rowCount).toBe(1);
      expect(block.columnCount).toBe(2);
      expect(block.cells.map(cell => cell.text)).toEqual(['Feature', 'State']);
      expect(block.cells.map(cell => cell.alignment)).toEqual([1, 3]);
      expect(block.height).toBeGreaterThan(0);
    });
    write(source, '| Fir');
    await inspect(key, value => {
      expect(value.blocks[0]!.rowCount).toBe(2);
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Feature', 'State', 'Fir', '']);
      expect(value.blocks[0]!.width).toBe(header.blocks[0]!.width);
    });
    write(source, 'st | Rea');
    await inspect(key, value => {
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Feature', 'State', 'First', 'Rea']);
    });
    write(source, 'dy |\n| Sec');
    await inspect(key, value => {
      expect(value.blocks[0]!.rowCount).toBe(3);
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Feature', 'State', 'First', 'Ready', 'Sec', '']);
    });
    write(source, 'ond | Next |\n\nAfter the table.');
    testing.setStatus(source, TSB.done);
    await inspect(key, value => {
      expect(value.blocks[0]!.cells.map(cell => cell.text)).toEqual(['Feature', 'State', 'First', 'Ready', 'Second', 'Next']);
      expect(value.text).toContain('After the table.');
    });
  });

  for (const [name, status] of [['done', TSB.done], ['error', TSB.error], ['cancelled', TSB.cancelled], ['truncated', TSB.overflow]] as const) {
    it(`restores literal unfinished image syntax on ${name} without a new byte`, async () => {
      const key = newKey();
      const raw = '![unfinished](https://example.invalid/image';
      const source = buffer(raw);
      await render(element(key, source));
      await inspect(key, value => expect(value.blocks[0]!.status).toBe('pending'));
      testing.setStatus(source, status);
      await inspect(key, value => {
        expect(value.blocks ?? []).toEqual([]);
        expect(value.text).toBe(raw + '\n');
      });
      await waitForSnapshot(key, value => expect(value.status).toBe(name));
    });
  }

  it('reflows a pending placeholder on resize without starting a load', async () => {
    const key = newKey();
    const source = buffer('![A long image description that must fit the available column');
    const view = await render(element(key, source));
    await inspect(key, value => expect(value.blocks[0]!.status).toBe('pending'));
    await view.rerender(element(key, source, 90));
    await inspect(key, value => {
      expect(value.blocks[0]!.status).toBe('pending');
      expect(value.blocks[0]!.width).toBeLessThanOrEqual(90);
      expect(value.blocks[0]!.height).toBeGreaterThan(0);
    });
  });

  it('treats a closed empty image destination as a final fallback, not an unfinished URL', async () => {
    const key = newKey();
    await render(element(key, buffer('![empty]()', TSB.done)));
    await inspect(key, value => {
      expect(value.blocks.length).toBe(1);
      expect(value.blocks[0]!.kind).toBe('image');
      expect(value.blocks[0]!.status).toBe('blocked');
      expect(value.blocks[0]!.alt).toBe('empty');
    });
  });

  it('does not retain a pending image after an imperative source replacement', async () => {
    const key = newKey();
    const ref = React.createRef<StreamTextHandle>();
    await render(<Column><StreamText ref={ref} source={buffer('![pending')} nativeID={key}
      recordDisplayLog format="markdown" /></Column>);
    await inspect(key, value => expect(value.blocks[0]!.status).toBe('pending'));
    ref.current!.attach(buffer('**replacement**', TSB.done));
    await inspect(key, value => {
      expect(value.blocks ?? []).toEqual([]);
      expect(value.text).toBe('replacement\n');
    });
    await sleep(100);
    await inspect(key, value => expect(value.blocks ?? []).toEqual([]));
  });

  it('retains a loaded image while a second pending image grows after it', async () => {
    const key = newKey();
    const firstUrl = `${endpoint}/image.png?key=${key}-first`;
    const secondUrl = `${endpoint}/image.png?key=${key}-second`;
    const source = buffer(`![first](${firstUrl})\n\n`);
    await render(element(key, source));
    await inspect(key, value => expect(value.blocks[0]!.status).toBe('loaded'));
    write(source, '![sec');
    await inspect(key, value => {
      expect(value.blocks.length).toBe(2);
      expect(value.blocks[0]!.status).toBe('loaded');
      expect(value.blocks[0]!.url).toBe(firstUrl);
      expect(value.blocks[1]!.status).toBe('pending');
      expect(value.blocks[1]!.alt).toBe('sec');
    });
    write(source, `ond](${secondUrl})`);
    testing.setStatus(source, TSB.done);
    await inspect(key, value => expect(value.blocks.map(block => block.status)).toEqual(['loaded', 'loaded']));
    expect((await (await fetch(`${endpoint}/counts?key=${key}-first`)).json()).count).toBe(1);
    expect((await (await fetch(`${endpoint}/counts?key=${key}-second`)).json()).count).toBe(1);
  });
});
