import React from 'react';
import { Text } from 'react-native';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';
import { bufferText } from 'react-native-text-stream/testing';
import {
  buffer,
  Column,
  layouts,
  newKey,
  PARAGRAPHS,
  snapshot,
  TEXT_STYLE,
  testing,
  TSB,
  utf8,
  waitForSnapshot,
  waitForText,
  write,
} from './helpers';

describe('StreamText: text and height', () => {
  it('an empty view has no height and no text view', async () => {
    const key = newKey();
    const { heights, onLayout } = layouts();
    await render(
      <Column>
        <StreamText nativeID={key} recordDisplayLog onLayout={onLayout('stream')} {...TEXT_STYLE} />
      </Column>,
    );
    await waitFor(() => expect(heights.stream).toBe(0));
    const s = await snapshot(key);
    expect(s.text).toBe('');
    expect(s.height).toBe(0);
    expect(s.hasTextView).toBe(false);
  });

  it('shows appended bytes as they arrive and grows', async () => {
    const key = newKey();
    const tsb = buffer();
    await render(
      <Column>
        <StreamText source={tsb} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    write(tsb, 'Hello');
    const one = await waitForText(key, 'Hello');
    write(tsb, ', world. '.repeat(30));
    const many = await waitForText(key, 'Hello' + ', world. '.repeat(30));
    expect(one.height).toBeGreaterThan(0);
    expect(many.height).toBeGreaterThan(one.height);
  });

  for (const width of [300, 301.5]) it(`lays out exactly like <Text> at width ${width}`, async () => {
    const key = newKey();
    const { heights, onLayout } = layouts();
    await render(
      <Column width={width}>
        <StreamText
          source={buffer(PARAGRAPHS, TSB.done)}
          nativeID={key}
          recordDisplayLog
          onLayout={onLayout('stream')}
          {...TEXT_STYLE}
        />
        <Text onLayout={onLayout('text')} style={TEXT_STYLE}>
          {PARAGRAPHS}
        </Text>
      </Column>,
    );
    await waitForText(key, PARAGRAPHS);
    await waitFor(() => {
      expect(heights.text).toBeGreaterThan(0);
      expect(heights.stream).toBe(heights.text);
    });
  });

  it('lineHeight sets the line pitch', async () => {
    const key = newKey();
    const { heights, onLayout } = layouts();
    await render(
      <Column>
        <StreamText source={buffer('a\nb\nc', TSB.done)} nativeID={key} recordDisplayLog onLayout={onLayout('s')} fontSize={14} lineHeight={30} />
      </Column>,
    );
    await waitFor(() => expect(heights.s).toBe(90));
  });

  it('reassembles UTF-8 sequences split across appends, at every split point', async () => {
    const text = 'ж漢🚀';
    const bytes = utf8(text);
    for (let cut = 1; cut < bytes.length; cut++) {
      const key = newKey();
      const tsb = buffer();
      await render(
        <Column>
          <StreamText source={tsb} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
        </Column>,
      );
      write(tsb, bytes.subarray(0, cut));
      await waitForSnapshot(key, (s) => expect(s.text.length).toBeLessThanOrEqual(text.length));
      write(tsb, bytes.subarray(cut));
      await waitForText(key, text);
    }
  });

  it('shows invalid bytes and a truncated tail as U+FFFD, like getText()', async () => {
    const key = newKey();
    const bytes = new Uint8Array([0x41, 0xc3, 0x41, 0xff, 0xe2, 0x82, 0x42, 0xf0, 0x9f, 0x9a]);
    const tsb = buffer(bytes);
    await render(
      <Column>
        <StreamText source={tsb} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    testing.setStatus(tsb, TSB.done);
    const expected = bufferText(tsb);
    expect(expected).toBe('A�A��B�');
    await waitForText(key, expected);
  });

  it('restyling keeps the text and changes the height', async () => {
    const key = newKey();
    const tsb = buffer(PARAGRAPHS);
    const element = (fontSize: number) => (
      <Column>
        <StreamText source={tsb} nativeID={key} recordDisplayLog fontSize={fontSize} color="#FF0000" />
      </Column>
    );
    const { rerender } = await render(element(14));
    const small = await waitForText(key, PARAGRAPHS);
    await rerender(element(22));
    const big = await waitForSnapshot(key, (s) => expect(s.fontSize).toBe(22));
    expect(big.text).toBe(PARAGRAPHS);
    expect(big.color).toBe('#FF0000FF');
    expect(big.height).toBeGreaterThan(small.height);
  });

  it('re-wraps when its width changes', async () => {
    const key = newKey();
    const tsb = buffer(PARAGRAPHS, TSB.done);
    const element = (width: number) => (
      <Column width={width}>
        <StreamText source={tsb} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>
    );
    const { rerender } = await render(element(320));
    const wide = await waitForText(key, PARAGRAPHS);
    await rerender(element(160));
    const narrow = await waitForSnapshot(key, (s) => expect(s.height).toBeGreaterThan(wide.height));
    expect(narrow.text).toBe(PARAGRAPHS);
  });

  it('fills a finished buffer over 64 KB progressively, to the full text', async () => {
    const key = newKey();
    const big = 'Lorem ipsum dolor sit amet, ✨ consectetur. '.repeat(3000); // ~132 KB
    await render(
      <Column>
        <StreamText source={buffer(big, TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    const s = await waitForText(key, big, 10_000);
    expect(s.status).toBe('done');
  });

  it('exposes only the tail of a long text to accessibility', async () => {
    const key = newKey();
    const long = 'x'.repeat(5000) + 'THE END';
    await render(
      <Column>
        <StreamText source={buffer(long, TSB.done)} nativeID={key} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    const s = await waitForText(key, long);
    expect(s.accessibilityValue.startsWith('…')).toBe(true);
    expect(s.accessibilityValue.endsWith('THE END')).toBe(true);
    expect(s.accessibilityValue.length).toBeLessThanOrEqual(2001);
  });
});

