import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { ScrollView, StyleSheet, Text, TextInput } from 'react-native';
import Animated, { useAnimatedProps, useSharedValue } from 'react-native-reanimated';
import { fetch } from 'react-native-nitro-fetch';
import { TextDecoder } from 'react-native-nitro-text-decoder';
import { StreamText, type StreamTextHandle } from 'react-native-text-stream';
import { StreamdownText } from 'react-native-streamdown';
import { pairs, type Point } from './stats';
import type { VariantId } from './config';

export interface VariantResult {
  /**
   * [wallClockMs, bytesShown]: the first frame after the main thread finished the
   * work for those bytes (FrameMonitor.markPainted for JS, the view's own log for C).
   */
  shown: Point[];
  /** Network chunks that reached JS (0 for the native path). */
  jsChunks: number;
  error?: string;
}

export interface VariantProps {
  url: string;
  /** Resolves with the wall-clock ms of the first frame after pending main-thread work. */
  markPainted: () => Promise<number>;
  onFinish: (result: VariantResult) => void;
}

const TEXT_COLOR = '#ECECF1';

/** Reads the body through JS: stock nitro-fetch ReadableStream + simdutf TextDecoder. */
async function readJs(url: string, onChunk: (text: string, bytes: number) => void, isCancelled: () => boolean) {
  const res = await fetch(url, { stream: true });
  const reader = (res as any).body.getReader();
  const decoder = new TextDecoder('utf-8');
  let chunks = 0;
  for (;;) {
    const { value, done } = await reader.read();
    if (done || isCancelled()) break;
    chunks++;
    onChunk(decoder.decode(value, { stream: true }), value.byteLength);
  }
  return chunks;
}

/** Resolves after the next two frames, so the last setState has been committed. */
const afterCommit = () =>
  new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r())));

/** Marks every committed update as painted on the first frame after it is mounted. */
function usePaintMarks(bytes: number, markPainted: () => Promise<number>) {
  const marks = useRef<Promise<Point>[]>([]);
  useLayoutEffect(() => {
    if (bytes > 0) marks.current.push(markPainted().then((t): Point => [t, bytes]));
  }, [bytes, markPainted]);
  return marks;
}

// A1: setState for every network chunk.
function JsPerChunk({ url, markPainted, onFinish }: VariantProps) {
  const [state, setState] = useState({ text: '', bytes: 0 });
  const marks = usePaintMarks(state.bytes, markPainted);

  useEffect(() => {
    let cancelled = false;
    readJs(url, (s, n) => setState((p) => ({ text: p.text + s, bytes: p.bytes + n })), () => cancelled)
      .then(async (jsChunks) => {
        await afterCommit();
        const shown = await Promise.all(marks.current);
        if (!cancelled) onFinish({ shown, jsChunks });
      })
      .catch((e) => onFinish({ shown: [], jsChunks: 0, error: String(e) }));
    return () => {
      cancelled = true;
    };
  }, [url, onFinish, marks]);

  return <JsTextView text={state.text} />;
}

// A2: chunks accumulate in locals, one setState per frame (the usual best practice).
function JsPerFrame({ url, markPainted, onFinish }: VariantProps) {
  const [state, setState] = useState({ text: '', bytes: 0 });
  const marks = usePaintMarks(state.bytes, markPainted);

  useEffect(() => {
    let cancelled = false;
    let text = '';
    let bytes = 0;
    let flushed = 0;
    let raf = 0;
    const loop = () => {
      if (bytes !== flushed) {
        flushed = bytes;
        setState({ text, bytes });
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    readJs(url, (s, n) => {
      text += s;
      bytes += n;
    }, () => cancelled)
      .then(async (jsChunks) => {
        // One frame for the final flush, one for its commit.
        await afterCommit();
        await afterCommit();
        cancelAnimationFrame(raf);
        const shown = await Promise.all(marks.current);
        if (!cancelled) onFinish({ shown, jsChunks });
      })
      .catch((e) => onFinish({ shown: [], jsChunks: 0, error: String(e) }));
    return () => {
      cancelled = true;
      cancelAnimationFrame(raf);
    };
  }, [url, onFinish, marks]);

  return <JsTextView text={state.text} />;
}

function JsTextView({ text }: { text: string }) {
  const scroll = useRef<ScrollView>(null);
  return (
    <ScrollView
      ref={scroll}
      style={StyleSheet.absoluteFill}
      contentContainerStyle={styles.content}
      onContentSizeChange={() => scroll.current?.scrollToEnd({ animated: false })}
    >
      <Text style={styles.text}>{text}</Text>
    </ScrollView>
  );
}

// B1: "ReText" — JS writes a shared value, Reanimated updates TextInput.text on the
// UI thread without a React render.
const AnimatedTextInput = Animated.createAnimatedComponent(TextInput);

function ReanimatedText({ url, markPainted, onFinish }: VariantProps) {
  const text = useSharedValue('');
  const animatedProps = useAnimatedProps(() => ({ text: text.value, defaultValue: text.value }) as any);

  useEffect(() => {
    let cancelled = false;
    let acc = '';
    let bytes = 0;
    const marks: Promise<Point>[] = [];
    readJs(url, (s, n) => {
      acc += s;
      bytes += n;
      text.value = acc;
      const b = bytes;
      // Reanimated applies the text in its frame callback on the frame the first mark resolves
      // on, so it is on screen one frame later (the same "first frame after drawn" as A and C).
      marks.push(markPainted().then(() => markPainted()).then((t): Point => [t, b]));
    }, () => cancelled)
      .then(async (jsChunks) => {
        await afterCommit();
        const shown = await Promise.all(marks);
        if (!cancelled) onFinish({ shown, jsChunks });
      })
      .catch((e) => onFinish({ shown: [], jsChunks: 0, error: String(e) }));
    return () => {
      cancelled = true;
    };
  }, [url, markPainted, onFinish, text]);

  return (
    <AnimatedTextInput
      editable={false}
      multiline
      scrollEnabled
      animatedProps={animatedProps}
      style={[StyleSheet.absoluteFill, styles.content, styles.text]}
    />
  );
}

// B2: react-native-streamdown (remend on a worklet + enriched-markdown native view), fed once
// per frame like A2. Its own async work may land after the mark, so latency is a lower bound.
function StreamdownVariant({ url, markPainted, onFinish }: VariantProps) {
  const [state, setState] = useState({ text: '', bytes: 0 });
  const marks = useRef<Promise<Point>[]>([]);
  useLayoutEffect(() => {
    const b = state.bytes;
    if (b > 0) marks.current.push(markPainted().then(() => markPainted()).then((t): Point => [t, b]));
  }, [state.bytes, markPainted]);

  useEffect(() => {
    let cancelled = false;
    let text = '';
    let bytes = 0;
    let flushed = 0;
    let raf = 0;
    const loop = () => {
      if (bytes !== flushed) {
        flushed = bytes;
        setState({ text, bytes });
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    readJs(url, (s, n) => {
      text += s;
      bytes += n;
    }, () => cancelled)
      .then(async (jsChunks) => {
        await afterCommit();
        await afterCommit();
        cancelAnimationFrame(raf);
        const shown = await Promise.all(marks.current);
        if (!cancelled) onFinish({ shown, jsChunks });
      })
      .catch((e) => onFinish({ shown: [], jsChunks: 0, error: String(e) }));
    return () => {
      cancelled = true;
      cancelAnimationFrame(raf);
    };
  }, [url, onFinish]);

  return (
    <ScrollView style={StyleSheet.absoluteFill} contentContainerStyle={styles.content}>
      <StreamdownText markdown={state.text} markdownStyle={STREAMDOWN_STYLE} />
    </ScrollView>
  );
}

const STREAMDOWN_STYLE = { paragraph: { color: TEXT_COLOR, fontSize: 14, lineHeight: 21 } } as any;

// C: the native path; JS only hands the buffer over and polls for completion.
function NativeStream({ url, onFinish }: VariantProps) {
  const [buffer, setBuffer] = useState<ArrayBuffer>();
  const view = useRef<StreamTextHandle>(null);

  useEffect(() => {
    fetch(url, { stream: true, nativeBuffer: true })
      .then((res) => setBuffer(res.nativeBuffer))
      .catch((e) => onFinish({ shown: [], jsChunks: 0, error: String(e) }));
  }, [url, onFinish]);

  useEffect(() => {
    if (!buffer) return;
    // TSB1 header: committed (u64 @ 8), status (u32 @ 24), little-endian.
    const header = new DataView(buffer, 0, 64);
    const id = setInterval(() => {
      const status = header.getUint32(24, true);
      const committed = header.getUint32(8, true) + header.getUint32(12, true) * 2 ** 32;
      const log = view.current?.getDisplayLog() ?? [];
      const lastShown = log.length ? log[log.length - 1] : 0;
      if (status !== 0 && lastShown >= committed) {
        clearInterval(id);
        onFinish({ shown: pairs(log), jsChunks: 0 });
      }
    }, 100);
    return () => clearInterval(id);
  }, [buffer, onFinish]);

  return (
    <StreamText
      source={buffer}
      fontSize={14}
      color={TEXT_COLOR}
      recordDisplayLog
      ref={view}
      style={StyleSheet.absoluteFill}
    />
  );
}

export const VARIANT_COMPONENTS: Record<VariantId, React.ComponentType<VariantProps>> = {
  A1: JsPerChunk,
  A2: JsPerFrame,
  B1: ReanimatedText,
  B2: StreamdownVariant,
  C: NativeStream,
};

const styles = StyleSheet.create({
  content: { paddingHorizontal: 12, paddingTop: 14, paddingBottom: 40 },
  text: { color: TEXT_COLOR, fontSize: 14, lineHeight: 21 },
});
