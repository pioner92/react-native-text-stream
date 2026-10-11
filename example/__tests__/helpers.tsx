import React from 'react';
import { View } from 'react-native';
import { expect, waitFor } from 'react-native-harness';
import { createStreamTextTesting, type StreamTextSnapshot } from 'text-stream-testing';

export const testing = createStreamTextTesting();

export const TSB = { open: 0, done: 1, error: 2, cancelled: 3, overflow: 4 } as const;

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

export const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

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

export const PARAGRAPHS =
  'Streaming text straight from the network into a native view ⚡️ Every token lands in a native buffer.\n\n' +
  'Привет! Это проверка переноса строк с кириллицей и emoji 🚀 внутри длинного абзаца.\n\n' +
  '漢字とかなが混ざった行。 Short line.\nOne more line';
