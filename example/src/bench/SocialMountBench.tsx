import React, { useCallback, useLayoutEffect, useRef, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, TextInput, View, type LayoutChangeEvent } from 'react-native';
import Animated, { useAnimatedProps, useSharedValue } from 'react-native-reanimated';
import { StreamdownText } from 'react-native-streamdown';
import { StreamText, streamFetch, type StreamTextStatus } from 'react-native-text-stream';
import { createFrameMonitor, createStreamTextTesting } from 'text-stream-testing';
import { SERVER } from './config';
import { median } from './stats';

/** The one finished-message workload shown in the social comparison. */
const BYTES = 5_000;
const COUNT = 20;
const REPS = 3;
const TIMEOUT_MS = 15_000;
const TEXT_COLOR = '#ECECF1';
const testing = createStreamTextTesting();
const AnimatedTextInput = Animated.createAnimatedComponent(TextInput);

type Variant = 'streamText' | 'reactText' | 'reanimated' | 'streamdown';
const VARIANTS: Variant[] = ['streamText', 'reactText', 'reanimated', 'streamdown'];

type Sample = { buffer: ArrayBuffer; text: string; byteLength: number };
type Result = {
  variant: Variant;
  rep: number;
  shownMs: number | null;
  jsCommitMs: number | null;
  maxFrameMs: number;
  actualBytesEach: number[];
  complete: boolean;
  error?: string;
};
type Mounted = { key: string; variant: Variant; samples: Sample[]; onReady: () => void; onFinal: (error?: string) => void };

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

async function loadSamples(rep: number): Promise<Sample[]> {
  const loaded: Sample[] = [];
  for (let index = 0; index < COUNT; index++) {
    const stream = streamFetch(`${SERVER}/?fmt=raw&bytes=${BYTES}&chunk=65536&tps=0`);
    const result = await stream.done;
    if (result.status !== 'done') throw new Error(result.error ?? `sample ${index + 1} did not finish`);
    // Distinct prefixes keep both the RN and StreamText prepared-layout caches out of the next cell.
    const data = new Uint8Array(stream.buffer, 64, 3);
    data[0] = 48 + Math.floor(index / 10);
    data[1] = 48 + (index % 10);
    data[2] = 97 + rep;
    const text = stream.text();
    loaded.push({ buffer: stream.buffer, text, byteLength: new TextEncoder().encode(text).byteLength });
  }
  return loaded;
}

function MountContent({ mounted }: { mounted: Mounted }) {
  const { variant, samples, onReady, onFinal } = mounted;
  const done = useRef(0);
  const onStatus = useCallback((status: StreamTextStatus) => {
    if (status === 'idle' || status === 'streaming') return;
    if (status !== 'done') {
      onFinal(`StreamText finished as ${status}`);
      return;
    }
    done.current++;
    if (done.current === COUNT) onFinal();
  }, [onFinal]);
  useLayoutEffect(onReady, [onReady]);

  if (variant === 'streamText') {
    return <>{samples.map((sample, index) => <View key={index} style={styles.block}><StreamText source={sample.buffer} fontSize={14} lineHeight={21} color={TEXT_COLOR} onStatusChange={onStatus} /></View>)}</>;
  }
  if (variant === 'reanimated') return <>{samples.map((sample, index) => <ReanimatedFinished key={index} text={sample.text} />)}</>;
  if (variant === 'streamdown') return <>{samples.map((sample, index) => <View key={index} style={styles.block}><StreamdownText markdown={sample.text} markdownStyle={STREAMDOWN_STYLE} /></View>)}</>;
  return <>{samples.map((sample, index) => <View key={index} style={styles.block}><Text style={styles.text}>{sample.text}</Text></View>)}</>;
}

/** Matches B1's UI-thread TextInput approach, but begins with already completed text. */
function ReanimatedFinished({ text }: { text: string }) {
  const value = useSharedValue('');
  const animatedProps = useAnimatedProps(() => ({ text: value.value, defaultValue: value.value }) as any);
  useLayoutEffect(() => { value.value = text; }, [text, value]);
  return <View style={styles.block}><AnimatedTextInput editable={false} multiline scrollEnabled={false} animatedProps={animatedProps} style={[styles.text, styles.input]} /></View>;
}

export function SocialMountBench({ onBack }: { onBack: () => void }) {
  const monitor = useRef(createFrameMonitor());
  const running = useRef(false);
  const stageWidth = useRef<number | null>(null);
  const [mounted, setMounted] = useState<Mounted | null>(null);
  const [status, setStatus] = useState('Ready · mounts 20 finished messages × 5 KB');
  const [results, setResults] = useState<Result[]>([]);

  const runOne = useCallback(async (variant: Variant, rep: number): Promise<Result> => {
    setStatus(`${label(variant)} · rep ${rep}/${REPS} · loading samples…`);
    const samples = await loadSamples(rep);
    testing.clearPreparedLayouts();
    testing.setPreparedLayout(true);
    testing.setPreparedCoreText(true);
    await sleep(800);

    const m = monitor.current;
    m.start();
    await m.markPainted();
    await m.markPainted();
    const started = Date.now();
    const jsStarted = performance.now();
    let jsCommitMs: number | null = null;
    let resolveFinal: ((error?: string) => void) | null = null;
    const final = new Promise<string | undefined>((resolve) => { resolveFinal = resolve; });
    let resolveReady: (() => void) | null = null;
    const ready = new Promise<void>((resolve) => { resolveReady = resolve; });
    setMounted({
      key: `${variant}-${rep}-${started}`,
      variant,
      samples,
      onReady: () => {
        jsCommitMs = performance.now() - jsStarted;
        resolveReady?.();
      },
      onFinal: (error) => resolveFinal?.(error),
    });

    let error: string | undefined;
    let shownAt: number | null = null;
    try {
      await Promise.race([ready, sleep(TIMEOUT_MS).then(() => { throw new Error('mount commit timeout'); })]);
      if (variant === 'streamText') {
        const terminalError = await Promise.race([final, sleep(TIMEOUT_MS).then(() => { throw new Error('StreamText final-status timeout'); })]);
        if (terminalError) throw new Error(terminalError);
      }
      // The result is a native frame timestamp, not a JS clock sampled after the callback. The
      // same two-frame settle window is used for all four implementations.
      await m.markPainted();
      shownAt = await m.markPainted();
    } catch (caught) {
      error = String(caught);
    }
    const shownMs = error || shownAt == null ? null : Math.round(shownAt - started);
    await Promise.race([m.waitIdle(10), sleep(TIMEOUT_MS)]);
    const ui = await m.stop();
    setMounted(null);
    await sleep(700);
    return { variant, rep, shownMs, jsCommitMs: jsCommitMs == null ? null : Math.round(jsCommitMs * 10) / 10, maxFrameMs: Math.round(ui.maxFrameMs), actualBytesEach: samples.map((sample) => sample.byteLength), complete: !error, error };
  }, []);

  const run = useCallback(async () => {
    if (running.current) return;
    running.current = true;
    const all: Result[] = [];
    const startedAt = new Date().toISOString();
    setResults([]);
    try {
      for (let rep = 1; rep <= REPS; rep++) {
        const order = [...VARIANTS];
        for (let n = 0; n < rep - 1; n++) order.push(order.shift()!);
        for (const variant of order) {
          const result = await runOne(variant, rep);
          all.push(result);
          console.info(`SOCIAL_MOUNT_RESULT ${JSON.stringify(result)}`);
          setResults([...all]);
          await sleep(1_500);
        }
      }
      const name = `social-mount-ios-v2-${startedAt.replace(/[:.]/g, '-')}`;
      await globalThis.fetch(`${SERVER}/results?name=${name}`, {
        method: 'POST',
        body: JSON.stringify({
          startedAt,
          tag: 'social-ios-v2',
          build: 'release',
          workload: { messages: COUNT, requestedBytesEach: BYTES, actualBytesEach: [...new Set(all.flatMap((result) => result.actualBytesEach))], state: 'finished', stageWidthPt: stageWidth.current },
          reps: REPS,
          variants: VARIANTS,
          marker: 'native FrameMonitor timestamp after two frames following the React commit; StreamText also waits for every view to report done',
          caveats: ['Samples are unique per repetition but shared by every variant within that repetition.', 'streamdown rendering may finish after its frame marker.'],
          runs: all,
        }, null, 2),
      });
      setStatus(`Done · saved bench-results/${name}.json`);
    } catch (caught) {
      setStatus(`Failed: ${String(caught)}`);
    } finally {
      running.current = false;
    }
  }, [runOne]);

  return <View style={styles.screen}>
    <View style={styles.header}>
      <Pressable onPress={onBack} hitSlop={12}><Text style={styles.back}>‹ Streaming</Text></Pressable>
      <Text style={styles.title}>Mount</Text>
      <Pressable onPress={run} style={styles.run}><Text style={styles.runText}>Run mount</Text></Pressable>
    </View>
    <Text style={styles.status}>{status}</Text>
    <View style={styles.stage} onLayout={(event: LayoutChangeEvent) => { stageWidth.current = event.nativeEvent.layout.width; }}>{mounted ? <MountContent key={mounted.key} mounted={mounted} /> : null}</View>
    <ScrollView style={styles.results}><ResultsTable results={results} /></ScrollView>
  </View>;
}

function ResultsTable({ results }: { results: Result[] }) {
  return <>{VARIANTS.map((variant) => {
    const runs = results.filter((result) => result.variant === variant);
    const done = runs.filter((result) => result.complete);
    return <View key={variant} style={styles.resultRow}>
      <Text style={styles.resultName}>{label(variant)}</Text>
      <Text style={styles.resultValue}>{done.length ? `${Math.round(median(done.map((result) => result.shownMs!)) ?? NaN)} ms` : '—'}</Text>
      <Text style={styles.resultMeta}>{done.length}/{REPS} · frame {Math.round(median(done.map((result) => result.maxFrameMs)) ?? NaN)} ms</Text>
    </View>;
  })}</>;
}

function label(variant: Variant) {
  return ({ streamText: 'StreamText', reactText: 'React <Text>', reanimated: 'Reanimated', streamdown: 'streamdown*' } as const)[variant];
}

const STREAMDOWN_STYLE = { paragraph: { color: TEXT_COLOR, fontSize: 14, lineHeight: 21 } } as any;
const C = { bg: '#0A0A0F', border: '#24242E', text: '#ECECF1', muted: '#8B8B9A', accent: '#8B5CF6' };
const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: C.bg, paddingHorizontal: 16 },
  header: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingTop: 8 },
  back: { color: C.accent, fontSize: 16, fontWeight: '600' },
  title: { color: C.text, fontSize: 20, fontWeight: '800' },
  run: { backgroundColor: C.accent, borderRadius: 10, paddingHorizontal: 14, paddingVertical: 8 },
  runText: { color: '#fff', fontWeight: '700' },
  status: { color: C.muted, marginTop: 10, fontVariant: ['tabular-nums'] },
  stage: { height: 300, marginTop: 10, borderRadius: 14, borderWidth: 1, borderColor: C.border, overflow: 'hidden' },
  block: { paddingHorizontal: 12, paddingVertical: 6 },
  text: { color: C.text, fontSize: 14, lineHeight: 21 },
  input: { padding: 0 },
  results: { flex: 1, marginTop: 12 },
  resultRow: { flexDirection: 'row', alignItems: 'center', borderBottomWidth: 1, borderColor: C.border, paddingVertical: 10 },
  resultName: { color: C.text, fontWeight: '700', flex: 1 },
  resultValue: { color: C.accent, fontVariant: ['tabular-nums'], fontWeight: '800', width: 70, textAlign: 'right' },
  resultMeta: { color: C.muted, fontVariant: ['tabular-nums'], marginLeft: 12, width: 112, textAlign: 'right' },
});
