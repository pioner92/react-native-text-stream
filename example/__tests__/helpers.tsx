import React from 'react';
import { PixelRatio, Platform, View } from 'react-native';
import { describe, expect, it, waitFor } from 'react-native-harness';
import { createStreamTextTesting, type StreamTextSnapshot } from 'text-stream-testing';

export const testing = createStreamTextTesting();

export const TSB = { open: 0, done: 1, error: 2, cancelled: 3, overflow: 4 } as const;

/** Suites and tests of what Android doesn't have yet (named where they are used): skipped there. */
export const describeIOS = Platform.OS === 'ios' ? describe : describe.skip;
export const itIOS = Platform.OS === 'ios' ? it : it.skip;
/** What only Android has, or only Android passes yet (named where used). */
export const describeAndroid = Platform.OS === 'android' ? describe : describe.skip;
export const itAndroid = Platform.OS === 'android' ? it : it.skip;

const encoder = new TextEncoder();
export const utf8 = (text: string) => encoder.encode(text);
/** An exact ArrayBuffer copy of the bytes, for Nitro. */
export const arrayBuffer = (bytes: Uint8Array) => bytes.slice().buffer as ArrayBuffer;

/** A natively-backed TSB1 buffer, optionally pre-filled and finished. */
export function buffer(content?: string | Uint8Array, status: number = TSB.open, capacity = 256 * 1024) {
  const tsb = testing.createBuffer(capacity);
  if (content !== undefined) write(tsb, content);
  if (status !== TSB.open) testing.setStatus(tsb, status);
  return tsb;
}

export function write(tsb: ArrayBuffer, content: string | Uint8Array) {
  testing.append(tsb, arrayBuffer(typeof content === 'string' ? utf8(content) : content));
}

let keys = 0;
/** A fresh nativeID: snapshots find the view by it (with `recordDisplayLog`). */
export const newKey = () => `harness-${Date.now()}-${++keys}`;

export const snapshot = (key: string) => testing.snapshot(key);

/** Waits until the view's snapshot satisfies `check` (which asserts), and returns it. */
export async function waitForSnapshot(
  key: string,
  check: (s: StreamTextSnapshot) => void,
  timeout = 3000,
): Promise<StreamTextSnapshot> {
  let last: StreamTextSnapshot | undefined;
  await waitFor(
    async () => {
      last = await snapshot(key);
      check(last);
    },
    { timeout, interval: 30 },
  );
  return last!;
}

export const waitForText = (key: string, text: string, timeout?: number) =>
  waitForSnapshot(key, (s) => expect(s.text).toBe(text), timeout);

export const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

/** Blocks the JS thread. */
export function busyWait(ms: number) {
  const until = Date.now() + ms;
  while (Date.now() < until) {
    // on purpose
  }
}

/** Collects the latest layout height of a view, by name. */
export function layouts() {
  const heights: Record<string, number> = {};
  return {
    heights,
    onLayout: (name: string) => (e: { nativeEvent: { layout: { height: number } } }) => {
      heights[name] = e.nativeEvent.layout.height;
    },
  };
}

/** A fixed-width column: the width every test lays text out at. */
export function Column({ width = 300, children }: { width?: number; children: React.ReactNode }) {
  return <View style={{ width, alignItems: 'stretch' }}>{children}</View>;
}

export const TEXT_STYLE = { fontSize: 14, lineHeight: 21 } as const;

/** Points from one line to the next at `lineHeight`: Android rounds it up to whole pixels (see below). */
export const linePitch = (lineHeight: number) =>
  Platform.OS === 'android' ? Math.ceil(lineHeight * PixelRatio.get()) / PixelRatio.get() : lineHeight;

/**
 * The height of `lines` lines at `lineHeight`: Android lays lines out on whole pixels, rounding
 * the pitch up as <Text> does (CustomLineHeightSpan), so it is exact only in pixels there.
 */
export function expectLinesHeight(height: number, lines: number, lineHeight: number, { atLeast = false } = {}) {
  const ratio = Platform.OS === 'android' ? PixelRatio.get() : 1;
  const actual = Platform.OS === 'android' ? Math.round(height * ratio) : height;
  const expected = lines * (Platform.OS === 'android' ? Math.ceil(lineHeight * ratio) : lineHeight);
  if (atLeast) expect(actual).toBeGreaterThan(expected);
  else expect(actual).toBe(expected);
}

export const PARAGRAPHS =
  'Streaming text straight from the network into a native view ⚡️ Every token lands in a native buffer.\n\n' +
  'Привет! Это проверка переноса строк с кириллицей и emoji 🚀 внутри длинного абзаца.\n\n' +
  '漢字とかなが混ざった行。 Short line.\nOne more line';
