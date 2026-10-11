import React, { useCallback, useLayoutEffect, useRef, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';
import { fetch } from 'react-native-nitro-fetch';
import { TextDecoder } from 'react-native-nitro-text-decoder';
import { StreamdownText } from 'react-native-streamdown';
import { createFrameMonitor, StreamText, type StreamTextStatus } from 'react-native-text-stream';
import { SERVER, TAG } from './config';
import { median } from './stats';

// The component alone, against the other ways to show the same text: what mounting K of them
// costs, with the text already there (a finished answer) or, for StreamText, not yet (a bubble
// mounted before its first token). Each sits in a plain View; nothing scrolls.
type MountVariant = 'stream' | 'streamEmpty' | 'text' | 'textInput' | 'streamdown';
const VARIANTS: MountVariant[] = ['stream', 'streamEmpty', 'text', 'textInput', 'streamdown'];
/** [bytes, count]: 20 × 50 KB of <Text> is too much for the simulator's memory. */
const CELLS: [number, number][] = [
  [600, 1],
  [5_000, 1],
  [50_000, 1],
  [600, 20],
  [5_000, 20],
];
const REPS = 3;
const TEXT_COLOR = '#ECECF1';

interface Sample {
  buffer: ArrayBuffer;
  text: string;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function loadSample(bytes: number): Promise<Sample> {
  const res = await fetch(`${SERVER}/?fmt=raw&bytes=${bytes}&chunk=65536&tps=0`, { stream: true, nativeBuffer: true });
  const buffer = res.nativeBuffer as ArrayBuffer;
  const header = new DataView(buffer, 0, 64);
  while (header.getUint32(24, true) === 0) await sleep(20);
  const committed = header.getUint32(8, true);
  return { buffer, text: new TextDecoder('utf-8').decode(new Uint8Array(buffer, 64, committed)) };
}

interface Mounted {
  key: string;
  variant: MountVariant;
  sample: Sample;
  count: number;
}

export function MountBench({ onBack }: { onBack: () => void }) {
  const [mounted, setMounted] = useState<Mounted | null>(null);
  const [status, setStatus] = useState('Ready');
  const [summary, setSummary] = useState('');
  const monitor = useRef(createFrameMonitor());
  const running = useRef(false);
  const committed = useRef<(() => void) | null>(null);
  const done = useRef(0);

  const onStatus = useCallback((s: StreamTextStatus) => {
    if (s !== 'idle' && s !== 'streaming') done.current++;
  }, []);
  const onCommitted = useCallback(() => {
    const resolve = committed.current;
    committed.current = null;
    resolve?.();
  }, []);

  const run = useCallback(async () => {
    if (running.current) return;
    running.current = true;
    const m = monitor.current;
    const runs: Record<string, unknown>[] = [];
    const startedAt = new Date().toISOString();
    try {
      setStatus('Fetching samples…');
      const samples = new Map<number, Sample>();
      for (const [bytes] of CELLS) if (!samples.has(bytes)) samples.set(bytes, await loadSample(bytes));
      await sleep(2_000);

      for (let rep = 1; rep <= REPS; rep++) {
        // Rotate the order each rep so no variant always runs first.
        const order = [...VARIANTS];
        for (let k = 0; k < rep - 1; k++) order.push(order.shift()!);
        for (const [bytes, count] of CELLS) {
          for (const variant of order) {
            setStatus(`rep ${rep}/${REPS} · ${count} × ${bytes} B · ${variant}`);
            const memBefore = m.memoryFootprintMb();
            m.streamTextTotals(true);
            m.start();
            await m.markPainted();
            await m.markPainted();
            done.current = 0;

            const wallStart = Date.now();
            const jsStart = performance.now();
            let jsCommitMs = 0;
            const paintedAt = new Promise<number>((resolve) => {
              committed.current = () => {
                jsCommitMs = performance.now() - jsStart;
                m.markPainted().then(resolve);
              };
            });
            setMounted({ key: `${variant}-${bytes}-${count}-${rep}`, variant, sample: samples.get(bytes)!, count });
            const paintMs = (await paintedAt) - wallStart;
            // StreamText with a source is shown once it reports its final status.
            let shownMs = paintMs;
            if (variant === 'stream') {
              const until = Date.now() + 10_000;
              while (done.current < count && Date.now() < until) await sleep(5);
              shownMs = Math.max(paintMs, Date.now() - wallStart);
            }
            await Promise.race([m.waitIdle(10), sleep(15_000)]);
            const ui = await m.stop();
            await sleep(300);
            const memAfter = m.memoryFootprintMb();
            const [inits, initMs, ticks, tickMs, , commitMs, attaches, attachMs, widthLayouts, widthLayoutMs] =
              m.streamTextTotals(true);

            setMounted(null);
            await sleep(600);
            runs.push({
              rep,
              variant,
              bytes,
              count,
              jsCommitMs: Math.round(jsCommitMs * 10) / 10,
              paintMs: Math.round(paintMs),
              shownMs: Math.round(shownMs),
              hitches: ui.hitches,
              maxFrameMs: Math.round(ui.maxFrameMs),
              memDeltaMb: Math.round((memAfter - memBefore) * 10) / 10,
              native: { inits, initMs, ticks, tickMs, commitMs, attaches, attachMs, widthLayouts, widthLayoutMs },
            });
          }
        }
      }

      const name = `mount-${TAG}-${startedAt.replace(/[:.]/g, '-')}`;
      await globalThis.fetch(`${SERVER}/results?name=${name}`, {
        method: 'POST',
        body: JSON.stringify({ startedAt, tag: TAG, reps: REPS, cells: CELLS, runs }, null, 2),
      });
      setSummary(summarize(runs));
      setStatus(`Done · saved bench-results/${name}.json`);
    } catch (e) {
      setStatus(`Failed: ${String(e)}`);
    } finally {
      running.current = false;
    }
  }, []);

  return (
    <View style={styles.screen}>
      <View style={styles.header}>
        <Pressable onPress={onBack} hitSlop={12}>
          <Text style={styles.back}>‹ Bench</Text>
        </Pressable>
        <Text style={styles.title}>Mount</Text>
        <Pressable onPress={run} style={styles.run}>
          <Text style={styles.runText}>Run</Text>
        </Pressable>
      </View>
      <Text style={styles.status}>{status}</Text>
      <View style={styles.stage}>
        {mounted ? (
          <MountStage key={mounted.key} mounted={mounted} onStatus={onStatus} onCommitted={onCommitted} />
        ) : null}
      </View>
      <ScrollView style={styles.results}>
        <Text style={styles.summary}>{summary}</Text>
      </ScrollView>
    </View>
  );
}

function MountStage({
  mounted,
  onStatus,
  onCommitted,
}: {
  mounted: Mounted;
  onStatus: (s: StreamTextStatus) => void;
  onCommitted: () => void;
}) {
  useLayoutEffect(onCommitted, [onCommitted]);
  const { variant, sample, count } = mounted;
  return (
    <View>
      {Array.from({ length: count }, (_, i) => (
        <View key={i} style={styles.block}>
          {variant === 'stream' || variant === 'streamEmpty' ? (
            <StreamText
              source={variant === 'stream' ? sample.buffer : undefined}
              fontSize={14}
              lineHeight={21}
              color={TEXT_COLOR}
              onStatusChange={onStatus}
            />
          ) : variant === 'text' ? (
            <Text style={styles.text}>{sample.text}</Text>
          ) : variant === 'textInput' ? (
            <TextInput editable={false} multiline scrollEnabled={false} value={sample.text} style={[styles.text, styles.input]} />
          ) : (
            <StreamdownText markdown={sample.text} markdownStyle={STREAMDOWN_STYLE} />
          )}
        </View>
      ))}
    </View>
  );
}

function summarize(runs: Record<string, any>[]) {
  const lines: string[] = [];
  for (const [bytes, count] of CELLS) {
    lines.push(`${count} × ${bytes} B   paint / shown / maxFrame / mem`);
    for (const v of VARIANTS) {
      const rs = runs.filter((r) => r.variant === v && r.bytes === bytes && r.count === count);
      const f = (k: string) => Math.round(median(rs.map((r) => r[k])) ?? NaN);
      lines.push(`  ${v.padEnd(12)} ${f('paintMs')} / ${f('shownMs')} / ${f('maxFrameMs')} ms / ${median(rs.map((r) => r.memDeltaMb))} MB`);
    }
  }
  return lines.join('\n');
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
  block: { padding: 12 },
  text: { color: C.text, fontSize: 14, lineHeight: 21 },
  input: { padding: 0 },
  results: { flex: 1, marginTop: 12 },
  summary: { color: C.text, fontSize: 11, fontFamily: 'Menlo' },
});
