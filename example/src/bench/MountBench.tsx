import React, { useCallback, useLayoutEffect, useRef, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';
import { fetch } from 'react-native-nitro-fetch';
import { TextDecoder } from 'react-native-nitro-text-decoder';
import { StreamdownText } from 'react-native-streamdown';
import { createFrameMonitor, StreamText, type StreamTextStatus } from 'react-native-text-stream';
import { createStreamTextTesting } from 'react-native-text-stream/testing';
import { SERVER, TAG } from './config';
import { median } from './stats';

// The component alone, against the other ways to show the same text: what mounting K of them
// costs, with the text already there (a finished answer) or, for StreamText, not yet (a bubble
// mounted before its first token). Each sits in a plain View; nothing scrolls.
// stream: prepared layouts with CoreText; streamTextKit: prepared with TextKit;
// streamLegacy: no prepared layouts (the text laid out on the main thread on mount).
// Every StreamText mount starts with no prepared layout, and each rep has texts of its own, so no
// layout cache (ours or RN's for <Text>) carries over between runs.
// streamPlain (Peek only): no color prop, so the text follows the system appearance.
type MountVariant = 'stream' | 'streamTextKit' | 'streamLegacy' | 'streamPlain' | 'streamEmpty' | 'text' | 'textInput' | 'streamdown';
const VARIANTS: MountVariant[] = ['stream', 'streamTextKit', 'streamLegacy', 'streamEmpty', 'text', 'textInput', 'streamdown'];
const isStream = (v: MountVariant) => v === 'stream' || v === 'streamTextKit' || v === 'streamLegacy' || v === 'streamPlain';

function selectVariant(variant: MountVariant) {
  testing.clearPreparedLayouts();
  testing.setPreparedLayout(variant !== 'streamLegacy');
  testing.setPreparedCoreText(variant !== 'streamTextKit');
}
const testing = createStreamTextTesting();
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
/** The width StreamText gets in the stage on the benchmark simulator (iPhone 17 Pro Max). */
const STAGE_TEXT_WIDTH = 350;

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

/** `count` copies of a sample, each in its own buffer and starting with its own tag ("07c" + "eaming…"). */
async function loadSamples(bytes: number, count: number, tag = 0): Promise<Sample[]> {
  if (bytes < 0) return Array.from({ length: count }, () => mixedSample(-bytes));
  const samples: Sample[] = [];
  for (let i = 0; i < count; i++) {
    const { buffer } = await loadSample(bytes);
    const body = new Uint8Array(buffer, 64, 3);
    body[0] = 48 + Math.floor(i / 10);
    body[1] = 48 + (i % 10);
    body[2] = 97 + tag;
    const committed = new DataView(buffer, 0, 64).getUint32(8, true);
    samples.push({ buffer, text: new TextDecoder('utf-8').decode(new Uint8Array(buffer, 64, committed)) });
  }
  return samples;
}

const MIXED =
  'Стриминг ответа модели 🚀 идёт прямо в нативный буфер, без JS. 数据流直接写入缓冲区。 Mixed English text, ' +
  'числа 12345 и emoji 👩‍💻🎉, 日本語のテキストも少し。\n';

function mixedText(bytes: number) {
  let text = '';
  while (new TextEncoder().encode(text).length < bytes) text += MIXED;
  return text;
}

/** A finished buffer of mixed text. */
function mixedSample(bytes: number): Sample {
  const text = mixedText(bytes);
  const data = new TextEncoder().encode(text);
  const buffer = testing.createBuffer(data.byteLength);
  testing.append(buffer, data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer);
  testing.setStatus(buffer, 1);
  return { buffer, text };
}

interface Mounted {
  key: string;
  variant: MountVariant;
  samples: Sample[];
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
      for (let rep = 1; rep <= REPS; rep++) {
        setStatus(`rep ${rep}/${REPS} · fetching samples…`);
        const samples = new Map<string, Sample[]>();
        for (const [bytes, count] of CELLS) samples.set(`${bytes}x${count}`, await loadSamples(bytes, count, rep));
        await sleep(2_000);
        // Rotate the order each rep so no variant always runs first.
        const order = [...VARIANTS];
        for (let k = 0; k < rep - 1; k++) order.push(order.shift()!);
        for (const [bytes, count] of CELLS) {
          for (const variant of order) {
            setStatus(`rep ${rep}/${REPS} · ${count} × ${bytes} B · ${variant}`);
            selectVariant(variant);
            const memBefore = m.memoryFootprintMb();
            m.streamTextTotals(true);
            m.streamTextPhases(true);
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
            setMounted({ key: `${variant}-${bytes}-${count}-${rep}`, variant, samples: samples.get(`${bytes}x${count}`)!, count });
            const paintMs = (await paintedAt) - wallStart;
            // StreamText with a source is shown once it reports its final status.
            let shownMs = paintMs;
            if (isStream(variant)) {
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
            const phases = m.streamTextPhases(true);

            setMounted(null);
            selectVariant('stream');
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
              phases,
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

  // What showing a finished text costs on the main thread, step by step, for each size.
  // Keeps one variant mounted to look at (screenshots, selection): each tap shows the next one.
  const peekStep = useRef(0);
  const peek = useCallback(async () => {
    if (running.current) return;
    const steps: [MountVariant, number, number][] = [
      ['stream', -5_000, 1],
      ['streamTextKit', -5_000, 1],
      ['streamLegacy', -5_000, 1],
      ['stream', 50_000, 1],
      ['streamLegacy', 50_000, 1],
      ['stream', 600, 20],
      ['streamLegacy', 600, 20],
      ['streamPlain', 5_000, 1],
    ];
    const step = peekStep.current++ % (steps.length + 1);
    if (step === steps.length) {
      setMounted(null);
      setStatus('Peek: off');
      return;
    }
    const [variant, bytes, count] = steps[step];
    setMounted(null);
    const samples = await loadSamples(bytes, count);
    selectVariant(variant);
    setMounted({ key: `peek-${step}`, variant, samples, count });
    setStatus(`Peek: ${variant} · ${count} × ${bytes} B`);
    await sleep(500);
    selectVariant('stream');
  }, []);

  const probe = useCallback(async () => {
    if (running.current) return;
    running.current = true;
    try {
      setStatus('Probe: fetching samples…');
      const results: Record<string, unknown>[] = [];
      for (const bytes of [600, 5_000, 50_000, -50_000]) {
        // Negative: the same size of mixed text (Cyrillic, emoji, CJK), where font fallback kicks in.
        const text = bytes > 0 ? (await loadSample(bytes)).text : mixedText(-bytes);
        for (let rep = 1; rep <= 5; rep++) {
          setStatus(`Probe: ${bytes} B · rep ${rep}/5`);
          await sleep(300);
          results.push({ bytes, rep, ...(await testing.measureMount(text, STAGE_TEXT_WIDTH)) });
        }
      }
      const startedAt = new Date().toISOString();
      const name = `probe-${TAG}-${startedAt.replace(/[:.]/g, '-')}`;
      await globalThis.fetch(`${SERVER}/results?name=${name}`, {
        method: 'POST',
        body: JSON.stringify({ startedAt, tag: TAG, width: STAGE_TEXT_WIDTH, results }, null, 2),
      });
      setStatus(`Probe done · saved bench-results/${name}.json`);
    } catch (e) {
      setStatus(`Probe failed: ${String(e)}`);
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
        <Pressable onPress={peek} style={styles.run}>
          <Text style={styles.runText}>Peek</Text>
        </Pressable>
        <Pressable onPress={probe} style={styles.run}>
          <Text style={styles.runText}>Probe</Text>
        </Pressable>
        <Pressable onPress={run} style={styles.run}>
          <Text style={styles.runText}>Run</Text>
        </Pressable>
      </View>
      <Text style={styles.status}>{status}</Text>
      {mounted?.key.startsWith('peek') ? (
        // Peek scrolls, to look at tiles during a fling.
        <ScrollView style={styles.stage}>
          <MountStage key={mounted.key} mounted={mounted} onStatus={onStatus} onCommitted={onCommitted} />
        </ScrollView>
      ) : (
        <View style={styles.stage}>
          {mounted ? (
            <MountStage key={mounted.key} mounted={mounted} onStatus={onStatus} onCommitted={onCommitted} />
          ) : null}
        </View>
      )}
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
  const { variant, samples, count } = mounted;
  return (
    <View>
      {Array.from({ length: count }, (_, i) => (
        <View key={i} style={styles.block}>
          {isStream(variant) || variant === 'streamEmpty' ? (
            <StreamText
              source={variant === 'streamEmpty' ? undefined : samples[i].buffer}
              fontSize={14}
              lineHeight={21}
              color={variant === 'streamPlain' ? undefined : TEXT_COLOR}
              onStatusChange={onStatus}
            />
          ) : variant === 'text' ? (
            <Text style={styles.text}>{samples[i].text}</Text>
          ) : variant === 'textInput' ? (
            <TextInput editable={false} multiline scrollEnabled={false} value={samples[i].text} style={[styles.text, styles.input]} />
          ) : (
            <StreamdownText markdown={samples[i].text} markdownStyle={STREAMDOWN_STYLE} />
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
