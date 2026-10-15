import React from 'react';
import { newKey, buffer, Column, TSB, testing, waitForSnapshot } from './helpers';
import { describe, expect, it, render } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';

const rich = 'before rich content\n\n| Animal | Sound |\n| --- | --- |\n| cat | meow |\n\n![a sunset over Rome](https://example.invalid/alt.png)\n\nafter rich content\n';

describe('StreamText rich Markdown accessibility', () => {
  it('reads table cell text and image alt text instead of object replacement characters', async () => {
    const key = newKey();
    await render(<Column><StreamText source={buffer(rich, TSB.done)} nativeID={key} recordDisplayLog
      format="markdown" markdownFlavor="gfm" /></Column>);
    await waitForSnapshot(key, snapshot => {
      expect(snapshot.accessibilityValue).toContain('Animal');
      expect(snapshot.accessibilityValue).toContain('cat');
      expect(snapshot.accessibilityValue).toContain('meow');
      expect(snapshot.accessibilityValue).toContain('a sunset over Rome');
      expect(snapshot.accessibilityValue).toContain('before rich content');
      expect(snapshot.accessibilityValue).toContain('after rich content');
      expect(snapshot.accessibilityValue).not.toContain('\uFFFC');
    });
  });

  it('rebuilds table row geometry for a line-height style change', async () => {
    const key = newKey();
    const source = buffer('| Title |\n| --- |\n| a long cell that wraps onto several lines at this width |\n', TSB.done);
    const view = await render(<Column width={120}><StreamText source={source} nativeID={key} recordDisplayLog
      format="markdown" markdownFlavor="gfm" fontSize={14} lineHeight={18} /></Column>);
    const compact = await waitForSnapshot(key, snapshot => expect(snapshot.height).toBeGreaterThan(0));
    const block = JSON.parse(await testing.textAttributes(key)).blocks[0];
    expect(compact.height).toBeGreaterThanOrEqual(block.height);
    await view.rerender(<Column width={120}><StreamText source={source} nativeID={key} recordDisplayLog
      format="markdown" markdownFlavor="gfm" fontSize={14} lineHeight={36} /></Column>);
    await waitForSnapshot(key, snapshot => expect(snapshot.height).toBeGreaterThan(compact.height));
  });

  it('keeps ordinary plain text intact in the accessibility value', async () => {
    const key = newKey();
    await render(<Column><StreamText source={buffer('ordinary accessibility text', TSB.done)} nativeID={key}
      recordDisplayLog /></Column>);
    await waitForSnapshot(key, snapshot => expect(snapshot.accessibilityValue).toBe('ordinary accessibility text'));
  });
});
