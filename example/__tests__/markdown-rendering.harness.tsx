import React from 'react';
import { PixelRatio, Platform } from 'react-native';
import { afterEach, beforeEach, describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import { buffer, Column, newKey, testing, TSB, waitForSnapshot, write } from './helpers';

type TextRun = {
  start: number;
  end: number;
  fontSize: number;
  bold: boolean;
  italic: boolean;
  mono: boolean;
  strike: boolean;
  color: string;
  link: string;
};

type TextAttributes = { text: string; runs: TextRun[] };

async function attributes(key: string): Promise<TextAttributes> {
  return JSON.parse(await testing.textAttributes(key)) as TextAttributes;
}

function covers(attributes: TextAttributes, word: string, check: (run: TextRun) => boolean) {
  const start = attributes.text.indexOf(word);
  expect(start).toBeGreaterThanOrEqual(0);
  const end = start + word.length;
  if (!attributes.runs.some(run => run.start <= start && run.end >= end && check(run))) {
    throw new Error(`Missing expected native style for ${word}: ${JSON.stringify(attributes)}`);
  }
}

async function waitForAttributes(key: string, check: (value: TextAttributes) => void, timeout = 3000) {
  let result: TextAttributes | undefined;
  await waitFor(async () => {
    result = await attributes(key);
    check(result);
  }, { timeout, interval: 30 });
  return result!;
}

// Android rounds font sizes upward to whole physical pixels, as React Native Text does.
const sameFontSize = (actual: number, size: number) => Math.abs(actual -
  (Platform.OS === 'android' ? Math.ceil(size * PixelRatio.get()) / PixelRatio.get() : size)) < 0.001;

const sample = '# Heading\n\n**bold** *italic* ~~strike~~ `code` [link](https://example.com)';

const markdownView = (
  source: ArrayBuffer,
  key: string,
  options: { fontSize?: number; color?: string; format?: 'plain' | 'markdown' } = {},
) => <Column><StreamText source={source} nativeID={key} recordDisplayLog
  format={options.format ?? 'markdown'} markdownFlavor="gfm" fontSize={options.fontSize ?? 16} color={options.color} /></Column>;

/** A cache miss is intentionally prepared off the main thread; remount to exercise the canvas path. */
async function warmPrepared(makeView: (key: string) => React.ReactElement) {
  testing.clearPreparedLayouts();
  const warmKey = newKey();
  const warm = await render(makeView(warmKey));
  await waitForSnapshot(warmKey, state => expect(state.text.length).toBeGreaterThan(0));
  warm.unmount();
  await waitFor(() => expect(testing.preparedCacheCount()).toBeGreaterThan(0), { timeout: 3000, interval: 30 });
}

describe('StreamText Markdown native rendering', () => {
  beforeEach(() => testing.setPreparedLayout(true));
  afterEach(() => testing.setPreparedLayout(true));

  it('stores completed Markdown styles in the prepared native layout', async () => {
    const key = newKey();
    const source = buffer(sample, TSB.done);
    await warmPrepared(warmKey => markdownView(source, warmKey));
    await render(markdownView(source, key));
    await waitForSnapshot(key, state => {
      expect(state.status).toBe('done');
      if (state.mode !== 'canvas') throw new Error(`Expected prepared canvas: ${JSON.stringify(state)}; cache=${testing.preparedCacheDump()}`);
    });
    await waitForAttributes(key, value => {
      expect(value.text).toContain('Heading');
      covers(value, 'Heading', run => run.bold && run.fontSize > 16);
      covers(value, 'bold', run => run.bold);
      covers(value, 'italic', run => run.italic);
      covers(value, 'strike', run => run.strike);
      covers(value, 'code', run => run.mono);
      covers(value, 'link', run => run.link === 'https://example.com');
    });
  });

  it('keeps task controls accessible in cold and warm prepared layouts', async () => {
    const source = buffer('- [x] shipped\n- [ ] pending\n', TSB.done);
    const coldKey = newKey();
    const cold = await render(markdownView(source, coldKey));
    await waitForSnapshot(coldKey, state => {
      expect(state.status).toBe('done');
      expect(state.accessibilityValue).toContain('Checked task');
      expect(state.accessibilityValue).toContain('Unchecked task');
    });
    cold.unmount();

    await warmPrepared(warmKey => markdownView(source, warmKey));
    const key = newKey();
    await render(markdownView(source, key));
    await waitForSnapshot(key, state => {
      expect(state.mode).toBe('canvas');
      expect(state.accessibilityValue).toContain('Checked task');
      expect(state.accessibilityValue).toContain('Unchecked task');
      expect(state.accessibilityValue).not.toContain('\uFFFC');
    });
  });

  it('keeps fenced code styled in a warm prepared canvas', async () => {
    const source = buffer('```ts\nconst answer: number = 42;\n```\n', TSB.done);
    await warmPrepared(warmKey => markdownView(source, warmKey));
    const key = newKey();
    await render(markdownView(source, key));
    await waitForSnapshot(key, state => {
      expect(state.mode).toBe('canvas');
      expect(state.status).toBe('done');
    });
    await waitForAttributes(key, value => {
      covers(value, 'const', run => run.mono);
      covers(value, 'answer', run => run.mono);
    });
  });

  it('applies the same styles to an open native stream', async () => {
    const key = newKey();
    const source = buffer();
    await render(markdownView(source, key));
    write(source, '**bold** *italic* ~~strike~~ `code` [link](https://example.com)');
    await waitForAttributes(key, value => {
      covers(value, 'bold', run => run.bold);
      covers(value, 'italic', run => run.italic);
      covers(value, 'strike', run => run.strike);
      covers(value, 'code', run => run.mono);
      covers(value, 'link', run => run.link === 'https://example.com');
    });
    testing.setStatus(source, TSB.done);
    await waitForSnapshot(key, state => expect(state.status).toBe('done'));
  });

  for (const prepared of [false, true]) {
    it(`keeps Markdown spans through color and font changes (${prepared ? 'prepared' : 'streaming'})`, async () => {
      const key = newKey();
      const source = buffer('**bold** and `code` [link](https://example.com)', prepared ? TSB.done : TSB.open);
      const initial = { fontSize: 14, color: '#112233' };
      if (prepared) await warmPrepared(warmKey => markdownView(source, warmKey, initial));
      const first = await render(markdownView(source, key, initial));
      await waitForSnapshot(key, state => expect(state.mode).toBe(prepared ? 'canvas' : 'textView'));
      const check = (value: TextAttributes, size: number, color: string) => {
        covers(value, 'bold', run => run.bold && sameFontSize(run.fontSize, size) && run.color === color);
        covers(value, 'code', run => run.mono && sameFontSize(run.fontSize, size) && run.color === color);
        covers(value, 'link', run => run.link === 'https://example.com' && run.color === color);
      };
      await waitForAttributes(key, value => check(value, 14, '#112233FF'));
      await first.rerender(markdownView(source, key, { fontSize: 14, color: '#AABBCC' }));
      await waitForAttributes(key, value => check(value, 14, '#AABBCCFF'));
      await first.rerender(markdownView(source, key, { fontSize: 24, color: '#AABBCC' }));
      await waitForAttributes(key, value => check(value, 24, '#AABBCCFF'));
    });
  }

  it('restyles an earlier reference when its definition arrives', async () => {
    const key = newKey();
    const source = buffer('A [reference][id].\n\n');
    await render(markdownView(source, key));
    await waitForSnapshot(key, state => expect(state.text).toContain('[reference][id]'));
    write(source, '[id]: https://example.com\n');
    testing.setStatus(source, TSB.done);
    await waitForAttributes(key, value => {
      expect(value.text).toContain('A reference.');
      covers(value, 'reference', run => run.link === 'https://example.com');
    });
  });

  it('keeps safe links and rejects javascript links in native attributes', async () => {
    const key = newKey();
    const source = buffer();
    await render(markdownView(source, key));
    write(source, '[web](https://example.com) [mail](mailto:hello@example.com) [bad](javascript:alert(1))');
    testing.setStatus(source, TSB.done);
    await waitForAttributes(key, value => {
      covers(value, 'web', run => run.link === 'https://example.com');
      covers(value, 'mail', run => run.link === 'mailto:hello@example.com');
      covers(value, 'bad', run => run.link === '');
    });
  });

  it('keeps native Markdown spans when a prepared canvas falls back on resize', async () => {
    const key = newKey();
    const source = buffer('# Heading\n\n**bold** *italic* ~~strike~~ `code`', TSB.done);
    const element = (width: number) => <Column width={width}>
      <StreamText source={source} nativeID={key} recordDisplayLog format="markdown" markdownFlavor="gfm" fontSize={16} />
    </Column>;
    await warmPrepared(warmKey => <Column width={300}>
      <StreamText source={source} nativeID={warmKey} recordDisplayLog format="markdown" markdownFlavor="gfm" fontSize={16} />
    </Column>);
    const view = await render(element(300));
    await waitForSnapshot(key, state => expect(state.mode).toBe('canvas'));

    testing.setPreparedLayout(false);
    await view.rerender(element(220));
    await waitForSnapshot(key, state => expect(state.mode).toBe('textView'));
    await waitForAttributes(key, value => {
      covers(value, 'Heading', run => run.bold && run.fontSize > 16);
      covers(value, 'bold', run => run.bold);
      covers(value, 'italic', run => run.italic);
      covers(value, 'strike', run => run.strike);
      covers(value, 'code', run => run.mono);
    });
  });

  it('preserves cached prepared spans through remount and format changes', async () => {
    const source = buffer('## Cached\n\n**bold** `code`', TSB.done);
    const firstKey = newKey();
    const first = await render(markdownView(source, firstKey));
    await waitForAttributes(firstKey, value => {
      covers(value, 'Cached', run => run.bold && run.fontSize > 16);
      covers(value, 'bold', run => run.bold);
      covers(value, 'code', run => run.mono);
    });
    first.unmount();

    const key = newKey();
    const mounted = await render(markdownView(source, key));
    await waitForAttributes(key, value => {
      covers(value, 'Cached', run => run.bold && run.fontSize > 16);
      covers(value, 'bold', run => run.bold);
      covers(value, 'code', run => run.mono);
    });
    await mounted.rerender(markdownView(source, key, { format: 'plain' }));
    await waitForAttributes(key, value => {
      expect(value.text).toContain('**bold**');
      covers(value, 'bold', run => !run.bold && !run.mono);
    });
    await mounted.rerender(markdownView(source, key));
    await waitForAttributes(key, value => {
      covers(value, 'bold', run => run.bold);
      covers(value, 'code', run => run.mono);
    });
  });
});
