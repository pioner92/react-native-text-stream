import React, { useCallback, useRef, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { createFrameMonitor, type FrameMonitor } from 'text-stream-testing';
import {
  COOLDOWN_MS,
  FOCUS,
  RUN_TIMEOUT_MS,
  SCENARIOS as ALL_SCENARIOS,
  SERVER,
  VARIANTS as ALL_VARIANTS,
  streamUrl,
  type Scenario,
  type VariantId,
  LAYOUT,
  TAG,
} from './config';
import { clockOffset } from './clock';
import { startJsLoad, startJsProbe } from './probes';
import { latencyStats, median, type LatencyStats, type Point } from './stats';
import { BlockFrame, ChatFrame, VARIANT_COMPONENTS, type VariantResult } from './variants';

const SCENARIOS = ALL_SCENARIOS.filter((s) => !FOCUS.scenarios || FOCUS.scenarios.includes(s.id));
const VARIANTS = ALL_VARIANTS.filter((v) => !FOCUS.variants || FOCUS.variants.includes(v.id));
const REPS = FOCUS.reps ?? 3;

export interface RunMetrics extends LatencyStats {
  scenario: string;
  variant: VariantId;
  rep: number;
  /** Server clock minus the device's, and the round trip it was measured with (see clock.ts). */
  clockOffsetMs: number;
  clockRttMs: number;
  completed: boolean;
  error?: string;
  totalBytes: number;
  jsChunks: number;
  jsLongTasks: number;
  jsMaxGapMs: number;
  uiFrames: number;
  uiHitches: number;
  uiDroppedFrames: number;
  uiMaxFrameMs: number;
  /** First send -> UI smooth again (10 on-time frames) after the variant finished. */
  uiIdleMs: number | null;
  /** Main-thread time to tear the view down after the run (not part of the run's UI stats). */
  teardownMs: number;
  /** C only: [ticks, appendMs, layoutMs, commitMs, maxTickMs, heightReports, maxTickBytes, slowTicks, grows]. */
  render?: number[];
  memBeforeMb: number;
  memAfterMb: number;
}

interface Mounted {
  key: string;
  url: string;
  variant: VariantId;
  onFinish: (r: VariantResult) => void;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const START_DELAY_MS = 3_000;
const IDLE_FRAMES = 10;
const IDLE_TIMEOUT_MS = 20_000;
/** Resolves when the UI is smooth again, or null after the timeout. */
const waitIdle = (m: FrameMonitor) =>
  Promise.race([m.waitIdle(IDLE_FRAMES), sleep(IDLE_TIMEOUT_MS).then(() => null)]);
const afterCommit = () =>
  new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r())));

export function BenchScreen({
  onBack,
  onOpenHistory,
  onOpenMount,
}: {
  onBack: () => void;
  onOpenHistory: () => void;
  onOpenMount: () => void;
}) {
  const [mounted, setMounted] = useState<Mounted | null>(null);
  const [status, setStatus] = useState('Ready');
  const [results, setResults] = useState<RunMetrics[]>([]);
  const running = useRef(false);
  const monitor = useRef(createFrameMonitor());
  const markPainted = useCallback(() => monitor.current.markPainted(), []);

  const runOnce = useCallback(async (s: Scenario, variant: VariantId, rep: number): Promise<RunMetrics> => {
    const runId = `${s.id}-${variant}-${rep}-${Date.now()}`;
    const url = streamUrl(s, runId);

    // Latency compares the server's send log with when this device showed the bytes.
    const clock = await clockOffset();
    const memBeforeMb = monitor.current.memoryFootprintMb();
    const stopProbe = startJsProbe();
    const stopLoad = s.jsLoad ? startJsLoad(s.jsLoad.busyMs, s.jsLoad.everyMs) : () => {};
    monitor.current.start();
    // Wait for the monitor's first frame: a stall before it would not count as a frame.
    await monitor.current.markPainted();
    await monitor.current.markPainted();

    const result = await new Promise<VariantResult | null>((resolve) => {
      const timer = setTimeout(() => resolve(null), RUN_TIMEOUT_MS);
      setMounted({
        key: runId,
        url,
        variant,
        onFinish: (r) => {
          clearTimeout(timer);
          resolve(r);
        },
      });
    });

    // Keep monitoring until the UI is smooth again, so deferred work lands in this run's stats.
    const idleAt = await waitIdle(monitor.current);
    const ui = await monitor.current.stop();
    const memAfterMb = monitor.current.memoryFootprintMb();
    stopLoad();
    const js = stopProbe();
    if (FOCUS.holdMs) await sleep(FOCUS.holdMs);

    // Wait until the view is torn down and the UI is smooth, so the next run doesn't pay for it.
    const unmountedAt = Date.now();
    setMounted(null);
    await afterCommit();
    monitor.current.start();
    const settledAt = (await waitIdle(monitor.current)) ?? Date.now();
    await monitor.current.stop();
    const teardownMs = Math.max(0, settledAt - unmountedAt);

    const log: { total: number; events: Point[] } = await (
      await globalThis.fetch(`${SERVER}/log?id=${encodeURIComponent(runId)}`)
    ).json();
    const shown: Point[] = (result?.shown ?? []).map(([at, bytes]) => [at + clock.offsetMs, bytes]);
    const lat = latencyStats(log.events, shown, log.total);
    return {
      scenario: s.id,
      variant,
      rep,
      clockOffsetMs: Math.round(clock.offsetMs * 10) / 10,
      clockRttMs: clock.rttMs,
      completed: lat.complete,
      error: result?.error ?? (result ? undefined : `timeout ${RUN_TIMEOUT_MS / 1000}s`),
      totalBytes: log.total,
      jsChunks: result?.jsChunks ?? 0,
      ...lat,
      ...js,
      uiFrames: ui.frames,
      uiHitches: ui.hitches,
      uiDroppedFrames: ui.droppedFrames,
      uiMaxFrameMs: Math.round(ui.maxFrameMs),
      uiIdleMs: idleAt != null && log.events.length ? Math.round(idleAt + clock.offsetMs - log.events[0][0]) : null,
      teardownMs: Math.round(teardownMs),
      render: result?.render,
      memBeforeMb: Math.round(memBeforeMb),
      memAfterMb: Math.round(memAfterMb),
    };
  }, []);

  const runAll = useCallback(async () => {
    if (running.current) return;
    running.current = true;
    setResults([]);
    const all: RunMetrics[] = [];
    const startedAt = new Date().toISOString();
    try {
      // Let any accessibility snapshot triggered by the tap (UI test tools) finish first:
      // walking a view that holds 1 MB of text stalls the main thread for seconds.
      setStatus('Starting…');
      await sleep(START_DELAY_MS);
      for (const s of SCENARIOS) {
        for (let rep = 1; rep <= REPS; rep++) {
          // Rotate the order each rep so no variant always runs first.
          const order = VARIANTS.map((v) => v.id);
          for (let k = 0; k < rep - 1; k++) order.push(order.shift()!);
          for (const variant of order) {
            setStatus(`${s.id} · ${variant} · rep ${rep}/${REPS}`);
            const m = await runOnce(s, variant, rep);
            all.push(m);
            setResults([...all]);
            await sleep(COOLDOWN_MS);
          }
        }
      }
      const name = `bench-${LAYOUT}-${TAG}-${startedAt.replace(/[:.]/g, '-')}`;
      await globalThis.fetch(`${SERVER}/results?name=${name}`, {
        method: 'POST',
        body: JSON.stringify({ startedAt, tag: TAG, layout: LAYOUT, reps: REPS, scenarios: SCENARIOS, runs: all }, null, 2),
      });
      setStatus(`Done · saved bench-results/${name}.json`);
    } catch (e) {
      setStatus(`Failed: ${String(e)}`);
    } finally {
      running.current = false;
    }
  }, [runOnce]);

  const Variant = mounted ? VARIANT_COMPONENTS[mounted.variant] : null;

  return (
    <View style={styles.screen}>
      <View style={styles.header}>
        <Pressable onPress={onBack} hitSlop={12}>
          <Text style={styles.back}>‹ Demo</Text>
        </Pressable>
        <Pressable onPress={onOpenMount} hitSlop={12}>
          <Text style={styles.back}>Mount ›</Text>
        </Pressable>
        <Pressable onPress={onOpenHistory} hitSlop={12}>
          <Text style={styles.back}>History ›</Text>
        </Pressable>
        <Pressable onPress={runAll} style={styles.run}>
          <Text style={styles.runText}>Run all</Text>
        </Pressable>
      </View>
      <Text style={styles.status}>{status}</Text>

      <View style={styles.stage}>
        {mounted && Variant ? (
          LAYOUT === 'bubble' ? (
            <ChatFrame key={mounted.key}>
              <Variant url={mounted.url} markPainted={markPainted} onFinish={mounted.onFinish} />
            </ChatFrame>
          ) : LAYOUT === 'block' ? (
            <BlockFrame key={mounted.key}>
              <Variant url={mounted.url} markPainted={markPainted} onFinish={mounted.onFinish} />
            </BlockFrame>
          ) : (
            <Variant key={mounted.key} url={mounted.url} markPainted={markPainted} onFinish={mounted.onFinish} />
          )
        ) : null}
      </View>

      <ScrollView style={styles.results} contentContainerStyle={styles.resultsContent}>
        <ResultsTable results={results} />
      </ScrollView>
    </View>
  );
}

function ResultsTable({ results }: { results: RunMetrics[] }) {
  return (
    <>
      {SCENARIOS.map((s) => {
        const rows = VARIANTS.map((v) => {
          const runs = results.filter((r) => r.scenario === s.id && r.variant === v.id);
          return { v, runs };
        }).filter((r) => r.runs.length);
        if (!rows.length) return null;
        return (
          <View key={s.id} style={styles.block}>
            <Text style={styles.blockTitle}>
              {s.id} · {s.title}
            </Text>
            <View style={styles.row}>
              {['', 'done', 'idle', 'p50', 'p95', 'hitch', 'JS gap'].map((h) => (
                <Text key={h} style={[styles.cell, styles.head, h === '' && styles.nameCell]}>
                  {h}
                </Text>
              ))}
            </View>
            {rows.map(({ v, runs }) => {
              // Medians over finished runs only: a run cut short has no tail to measure.
              const done = runs.filter((r) => r.completed);
              const dnf = runs.length - done.length;
              return (
                <View key={v.id} style={styles.row}>
                  <Text style={[styles.cell, styles.nameCell]}>{v.id}</Text>
                  <Text style={styles.cell}>
                    {dnf ? `DNF ${dnf}` : fmt(median(done.map((r) => r.completionMs)))}
                  </Text>
                  <Text style={styles.cell}>{fmt(median(done.map((r) => r.uiIdleMs)))}</Text>
                  <Text style={styles.cell}>{fmt(median(done.map((r) => r.latP50Ms)))}</Text>
                  <Text style={styles.cell}>{fmt(median(done.map((r) => r.latP95Ms)))}</Text>
                  <Text style={styles.cell}>{fmt(median(done.map((r) => r.uiHitches)), '')}</Text>
                  <Text style={styles.cell}>{fmt(median(done.map((r) => r.jsMaxGapMs)))}</Text>
                </View>
              );
            })}
          </View>
        );
      })}
    </>
  );
}

const fmt = (v: number | null, unit = 'ms') => (v == null ? '—' : `${Math.round(v)}${unit}`);

const C = { bg: '#0A0A0F', card: '#14141B', border: '#24242E', text: '#ECECF1', muted: '#8B8B9A', accent: '#8B5CF6' };

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: C.bg, paddingHorizontal: 16 },
  header: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingTop: 8 },
  back: { color: C.accent, fontSize: 16, fontWeight: '600' },
  title: { color: C.text, fontSize: 20, fontWeight: '800' },
  run: { backgroundColor: C.accent, borderRadius: 10, paddingHorizontal: 14, paddingVertical: 8 },
  runText: { color: '#fff', fontWeight: '700' },
  status: { color: C.muted, marginTop: 10, fontVariant: ['tabular-nums'] },
  stage: {
    height: 200,
    marginTop: 10,
    borderRadius: 14,
    borderWidth: 1,
    borderColor: C.border,
    backgroundColor: '#0E0E14',
    overflow: 'hidden',
  },
  results: { flex: 1, marginTop: 12 },
  resultsContent: { paddingBottom: 24 },
  block: { backgroundColor: C.card, borderColor: C.border, borderWidth: 1, borderRadius: 14, padding: 12, marginBottom: 10 },
  blockTitle: { color: C.text, fontWeight: '700', marginBottom: 6 },
  row: { flexDirection: 'row', paddingVertical: 3 },
  cell: { flex: 1, color: C.text, fontSize: 12, fontVariant: ['tabular-nums'], textAlign: 'right' },
  nameCell: { flex: 0.6, textAlign: 'left', fontWeight: '700' },
  head: { color: C.muted, fontWeight: '600' },
});
