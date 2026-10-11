import React, { useCallback, useLayoutEffect, useRef, useState } from 'react';
import {
  FlatList,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { fetch } from 'react-native-nitro-fetch';
import { TextDecoder } from 'react-native-nitro-text-decoder';
import {
  createFrameMonitor,
  StreamText,
  type FrameStats,
  type StreamTextStatus,
} from 'react-native-text-stream';
import { SERVER, TAG } from './config';
import { median } from './stats';

// Library-only benchmark of a chat history made of finished StreamTexts: what mounting costs
// (in an empty chat and in a long one, against RN <Text>), memory per bubble, remounts in a
// virtualized list, and the cost of growing the last bubble below a long history.
const COUNT = 120;
const ADDS = 10;
const SIZES = [600, 2_000, 6_000, 20_000];
const STREAM = { bytes: 100_000, chunk: 256, tps: 80 };
const SCROLL_PT_PER_FRAME = 40;
/** Scrolled down, then back up: a fixed distance, so runs compare. */
const SCROLL_DISTANCE = 40_000;
/** Phases to run; RN <Text> is a fixed reference (6.5 GB on the simulator), so it is opt-in. */
const PHASES = {
  mount: true,
  mountText: false,
  scroll: true,
  streamBelow: true,
};
const STAGE_HEIGHT = 420;

/** `empty`: StreamText without a source; `stream`: with a finished buffer; `text`: RN <Text>. */
type Kind = 'empty' | 'stream' | 'text';
const KINDS: Kind[] = PHASES.mountText
  ? ['empty', 'stream', 'text']
  : ['empty', 'stream'];

type Mode =
  | { kind: 'none' }
  | { kind: 'mount'; bubble: Kind; base: number; extra: number }
  | { kind: 'list' }
  | { kind: 'stream'; url: string; key: string };

interface Item {
  id: string;
  buffer: ArrayBuffer;
  text: string;
  bytes: number;
}

const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const frame = () => new Promise<number>(r => requestAnimationFrame(r));
const round = (v: number | null) =>
  v == null ? null : Math.round(v * 10) / 10;

async function load(count: number): Promise<Item[]> {
  const decoder = new TextDecoder('utf-8');
  const items: Item[] = [];
  for (let i = 0; i < count; i += 8) {
    const batch = await Promise.all(
      Array.from({ length: Math.min(8, count - i) }, async (_, k) => {
        const bytes = SIZES[(i + k) % SIZES.length];
        const res = await fetch(
          `${SERVER}/?fmt=raw&bytes=${bytes}&chunk=65536&tps=0`,
          { stream: true, nativeBuffer: true },
        );
        const buffer = res.nativeBuffer as ArrayBuffer;
        const header = new DataView(buffer, 0, 64);
        while (header.getUint32(24, true) === 0) await sleep(20);
        const committed = header.getUint32(8, true);
        const text = decoder.decode(new Uint8Array(buffer, 64, committed));
        return { id: `m${i + k}`, buffer, text, bytes: committed };
      }),
    );
    items.push(...batch);
  }
  return items;
}

export function HistoryBench({ onBack }: { onBack: () => void }) {
  const [mode, setMode] = useState<Mode>({ kind: 'none' });
  const [status, setStatus] = useState('Ready');
  const [summary, setSummary] = useState('');
  const items = useRef<Item[]>([]);
  const monitor = useRef(createFrameMonitor());
  const running = useRef(false);
  const counts = useRef({ streaming: 0, done: 0 });
  const listRef = useRef<FlatList<Item>>(null);
  const scrollRef = useRef<ScrollView>(null);
  const scrollMax = useRef(Infinity);
  const contentHeight = useRef<((h: number) => void) | null>(null);
  const offsetReached = useRef(0);
  const painted = useRef<((t: number) => void) | null>(null);

  const onStatus = useCallback((s: StreamTextStatus) => {
    if (s === 'streaming') counts.current.streaming++;
    else if (s !== 'idle') counts.current.done++;
  }, []);
  // Runs in a layout effect after each commit of the stage: resolves with the first painted frame.
  const onCommitted = useCallback(() => {
    const resolve = painted.current;
    painted.current = null;
    if (resolve) monitor.current.markPainted().then(resolve);
  }, []);

  const m = monitor.current;
  const mem = () => round(m.memoryFootprintMb())!;
  const waitIdle = () => Promise.race([m.waitIdle(10), sleep(20_000)]);
  /** Lets the UI settle between phases. */
  const settle = async () => {
    m.start();
    await frame();
    await waitIdle();
    await m.stop();
    await sleep(500);
  };
  const measure = async <T,>(
    body: () => Promise<T>,
  ): Promise<{ ui: FrameStats; ms: number; value: T }> => {
    m.start();
    // Wait for the monitor's first frame: a stall before it would not count as a frame.
    await m.markPainted();
    await m.markPainted();
    const t0 = Date.now();
    const value = await body();
    const ms = Date.now() - t0;
    const ui = await m.stop();
    return { ui, ms, value };
  };
  const waitDone = async (n: number, timeoutMs = 60_000) => {
    const until = Date.now() + timeoutMs;
    while (counts.current.done < n && Date.now() < until) await sleep(5);
    return counts.current.done >= n;
  };
  /** Commits `next` and resolves with ms from setState to its first painted frame. */
  const commit = (next: Mode) =>
    new Promise<number>(resolve => {
      const t0 = Date.now();
      painted.current = t => resolve(t - t0);
      setMode(next);
    });
  const ui = (s: FrameStats) => ({
    hitches: s.hitches,
    dropped: s.droppedFrames,
    maxFrameMs: Math.round(s.maxFrameMs),
  });
  const totals = (reset: boolean) => {
    const [
      inits,
      initMs,
      ticks,
      tickMs,
      layoutMs,
      commitMs,
      attaches,
      attachMs,
      widthLayouts,
      widthLayoutMs,
    ] = m.streamTextTotals(reset);
    return {
      inits,
      initMsPerView: inits ? round(initMs / inits) : null,
      ticks,
      tickMs: round(tickMs),
      layoutMs: round(layoutMs),
      commitMs: round(commitMs),
      attaches,
      attachMs: round(attachMs),
      widthLayouts,
      widthLayoutMs: round(widthLayoutMs),
    };
  };

  /** Bulk mount of COUNT bubbles, then ADDS one at a time into an empty chat and after COUNT. */
  const mountCost = async (bubble: Kind) => {
    const needsDone = bubble === 'stream';
    const out: Record<string, unknown> = {};

    const memBefore = mem();
    counts.current = { streaming: 0, done: 0 };
    totals(true);
    const bulk = await measure(async () => {
      const paintMs = await commit({
        kind: 'mount',
        bubble,
        base: COUNT,
        extra: 0,
      });
      const t0 = Date.now();
      if (needsDone) await waitDone(COUNT);
      const doneMs = Date.now() - t0 + paintMs;
      await waitIdle();
      return { paintMs, doneMs };
    });
    await sleep(1_000);
    const memAfter = mem();
    out.bulk = {
      count: COUNT,
      paintMs: bulk.value.paintMs,
      allShownMs: needsDone ? bulk.value.doneMs : bulk.value.paintMs,
      ...ui(bulk.ui),
      ...totals(true),
      memBeforeMb: memBefore,
      memAfterMb: memAfter,
      memPerBubbleKb: Math.round(((memAfter - memBefore) * 1024) / COUNT),
    };

    for (const base of [0, COUNT]) {
      counts.current = { streaming: 0, done: 0 };
      await commit({ kind: 'mount', bubble, base, extra: 0 });
      // The history must be fully shown, or its events would be taken for the added bubble's.
      if (needsDone) await waitDone(base);
      await settle();
      totals(true);
      const paints: number[] = [];
      const dones: number[] = [];
      const run = await measure(async () => {
        for (let i = 1; i <= ADDS; i++) {
          counts.current = { streaming: 0, done: 0 };
          const t0 = Date.now();
          paints.push(await commit({ kind: 'mount', bubble, base, extra: i }));
          if (needsDone) {
            await waitDone(1);
            dones.push(Date.now() - t0);
          }
          await frame();
          await frame();
        }
        await waitIdle();
        return null;
      });
      out[base ? 'addAfterHistory' : 'addToEmptyChat'] = {
        history: base,
        paintMsP50: median(paints),
        paintMsMax: Math.max(...paints),
        shownMsP50: needsDone ? median(dones) : median(paints),
        ...ui(run.ui),
        ...totals(true),
      };
    }
    setMode({ kind: 'none' });
    await settle();
    out.memAfterUnmountMb = mem();
    return out;
  };

  const run = useCallback(async () => {
    if (running.current) return;
    running.current = true;
    const result: Record<string, unknown> = {
      tag: TAG,
      phases: PHASES,
      scrollDistancePt: SCROLL_DISTANCE,
      count: COUNT,
      adds: ADDS,
      sizes: SIZES,
      startedAt: new Date().toISOString(),
    };
    try {
      setStatus('Fetching history…');
      if (!items.current.length) items.current = await load(COUNT + ADDS);
      result.historyBytes = items.current
        .slice(0, COUNT)
        .reduce((a, b) => a + b.bytes, 0);
      await sleep(2_000);

      // 1. Mount cost per bubble kind.
      if (PHASES.mount) {
        const mount: Record<string, unknown> = {};
        for (const kind of KINDS) {
          setStatus(`Mount · ${kind}…`);
          mount[kind] = await mountCost(kind);
        }
        result.mount = mount;
      }

      // 2. A virtualized list scrolled down and back up: cells remount finished bubbles.
      if (PHASES.scroll) {
        setStatus('Scroll FlatList…');
        counts.current = { streaming: 0, done: 0 };
        setMode({ kind: 'list' });
        await settle();
        totals(true);
        const scroll = await measure(async () => {
          let offset = 0;
          for (let dir = 1; dir >= -1; dir -= 2) {
            for (;;) {
              offset += dir * SCROLL_PT_PER_FRAME;
              if (
                dir > 0 &&
                offset >= Math.min(SCROLL_DISTANCE, scrollMax.current)
              )
                break;
              if (dir < 0 && offset <= 0) break;
              listRef.current?.scrollToOffset({ offset, animated: false });
              offsetReached.current = Math.max(offsetReached.current, offset);
              await frame();
            }
          }
          await waitIdle();
          return null;
        });
        result.listScroll = {
          ...ui(scroll.ui),
          ms: scroll.ms,
          distancePt: Math.round(offsetReached.current),
          remounts: counts.current.done,
          ...totals(true),
          memMb: mem(),
        };
        setMode({ kind: 'none' });
        await settle();
      }

      // 3. Stream a long answer below the whole history, kept scrolled to the end.
      if (PHASES.streamBelow) {
        setStatus('Stream below history…');
        const key = `hist-${Date.now()}`;
        const url = `${SERVER}/?fmt=raw&bytes=${STREAM.bytes}&chunk=${STREAM.chunk}&tps=${STREAM.tps}&id=${key}`;
        counts.current = { streaming: 0, done: 0 };
        await commit({
          kind: 'mount',
          bubble: 'stream',
          base: COUNT,
          extra: 0,
        });
        await waitDone(COUNT);
        await settle();
        totals(true);
        const streamRun = await measure(async () => {
          counts.current = { streaming: 0, done: 0 };
          setMode({ kind: 'stream', url, key });
          const ok = await waitDone(1);
          await waitIdle();
          return ok;
        });
        result.streamBelowHistory = {
          completed: streamRun.value,
          ms: streamRun.ms,
          ...ui(streamRun.ui),
          render: m.streamTextRenderStats(key),
          ...totals(true),
          memMb: mem(),
        };
        setMode({ kind: 'none' });
        await settle();
      }

      const name = `history-${TAG}-${String(result.startedAt).replace(
        /[:.]/g,
        '-',
      )}`;
      await globalThis.fetch(`${SERVER}/results?name=${name}`, {
        method: 'POST',
        body: JSON.stringify(result, null, 2),
      });
      setSummary(JSON.stringify(result, null, 1));
      setStatus(`Done · saved bench-results/${name}.json`);
    } catch (e) {
      setStatus(`Failed: ${String(e)}`);
    } finally {
      running.current = false;
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Mounts the whole history (StreamText through the `source` prop, then RN <Text>, alternately)
  // and leaves it on screen; the status shows the content height, so the two can be compared.
  // Mounts the history through the `source` prop (all of it, then the first 12) and the first 12
  // as RN <Text> (all 120 can exhaust the simulator's memory); posts the content heights.
  const show = useCallback(async () => {
    if (running.current) return;
    running.current = true;
    try {
      setStatus('Fetching history…');
      if (!items.current.length) items.current = await load(COUNT + ADDS);
      const heights: Record<string, number> = {};
      const steps: [Kind, number][] = [
        ['stream', COUNT],
        ['stream', 12],
        ['text', 12],
      ];
      for (const [bubble, base] of steps) {
        setStatus(`Showing ${base} × ${bubble}…`);
        setMode({ kind: 'none' });
        await sleep(500);
        let last = 0;
        let changedAt = Date.now();
        contentHeight.current = h => {
          last = h;
          changedAt = Date.now();
        };
        setMode({ kind: 'mount', bubble, base, extra: 0 });
        const until = Date.now() + 30_000;
        while ((last === 0 || Date.now() - changedAt < 1_500) && Date.now() < until) await sleep(100);
        heights[`${bubble}${base}`] = last;
      }
      contentHeight.current = null;
      setMode({ kind: 'none' });
      const name = `show-heights-${TAG}-${Date.now()}`;
      await globalThis.fetch(`${SERVER}/results?name=${name}`, {
        method: 'POST',
        body: JSON.stringify(heights),
      });
      setStatus(`Heights saved · ${JSON.stringify(heights)}`);
    } finally {
      running.current = false;
    }
  }, []);

  const renderItem = useCallback(
    ({ item }: { item: Item }) => (
      <Bubble kind="stream" item={item} onStatus={onStatus} />
    ),
    [onStatus],
  );

  return (
    <View style={styles.screen}>
      <View style={styles.header}>
        <Pressable onPress={onBack} hitSlop={12}>
          <Text style={styles.back}>‹ Bench</Text>
        </Pressable>
        <Text style={styles.title}>History</Text>
        <Pressable onPress={show} style={styles.run}>
          <Text style={styles.runText}>Show</Text>
        </Pressable>
        <Pressable onPress={run} style={styles.run}>
          <Text style={styles.runText}>Run</Text>
        </Pressable>
      </View>
      <Text style={styles.status}>{status}</Text>
      <View style={styles.stage}>
        <Stage
          mode={mode}
          items={items.current}
          onStatus={onStatus}
          onCommitted={onCommitted}
          listRef={listRef}
          scrollRef={scrollRef}
          scrollMax={scrollMax}
          contentHeight={contentHeight}
          renderItem={renderItem}
        />
      </View>
      <ScrollView style={styles.results}>
        <Text style={styles.summary}>{summary}</Text>
      </ScrollView>
    </View>
  );
}

function Stage({
  mode,
  items,
  onStatus,
  onCommitted,
  listRef,
  scrollRef,
  scrollMax,
  contentHeight,
  renderItem,
}: {
  mode: Mode;
  items: Item[];
  onStatus: (s: StreamTextStatus) => void;
  onCommitted: () => void;
  listRef: React.RefObject<FlatList<Item> | null>;
  scrollRef: React.RefObject<ScrollView | null>;
  scrollMax: React.MutableRefObject<number>;
  contentHeight: React.MutableRefObject<((h: number) => void) | null>;
  renderItem: (info: { item: Item }) => React.ReactElement;
}) {
  useLayoutEffect(onCommitted, [mode, onCommitted]);

  if (mode.kind === 'none') return null;
  if (mode.kind === 'list') {
    return (
      <FlatList
        ref={listRef}
        data={items.slice(0, COUNT)}
        keyExtractor={i => i.id}
        renderItem={renderItem}
        windowSize={3}
        initialNumToRender={4}
        maxToRenderPerBatch={2}
        contentContainerStyle={styles.chat}
        onContentSizeChange={(_, h) => (scrollMax.current = h - STAGE_HEIGHT)}
      />
    );
  }
  const bubble = mode.kind === 'mount' ? mode.bubble : 'stream';
  const base = mode.kind === 'mount' ? mode.base : COUNT;
  const extra = mode.kind === 'mount' ? mode.extra : 0;
  return (
    <ScrollView
      ref={scrollRef}
      contentContainerStyle={styles.chat}
      onContentSizeChange={(_, h) => {
        contentHeight.current?.(h);
        scrollRef.current?.scrollToEnd({ animated: false });
      }}
    >
      {items.slice(0, base).map(item => (
        <Bubble key={item.id} kind={bubble} item={item} onStatus={onStatus} />
      ))}
      {items.slice(COUNT, COUNT + extra).map(item => (
        <Bubble key={item.id} kind={bubble} item={item} onStatus={onStatus} />
      ))}
      {mode.kind === 'stream' ? (
        <StreamingBubble
          key={mode.key}
          url={mode.url}
          logKey={mode.key}
          onStatus={onStatus}
        />
      ) : null}
    </ScrollView>
  );
}

const Bubble = React.memo(function Bubble({
  kind,
  item,
  onStatus,
}: {
  kind: Kind;
  item: Item;
  onStatus: (s: StreamTextStatus) => void;
}) {
  return (
    <View style={styles.bubble}>
      {kind === 'text' ? (
        <Text style={styles.text}>{item.text}</Text>
      ) : (
        <StreamText
          source={kind === 'stream' ? item.buffer : undefined}
          fontSize={14}
          lineHeight={21}
          color="#ECECF1"
          onStatusChange={onStatus}
        />
      )}
    </View>
  );
});

function StreamingBubble({
  url,
  logKey,
  onStatus,
}: {
  url: string;
  logKey: string;
  onStatus: (s: StreamTextStatus) => void;
}) {
  const [buffer, setBuffer] = useState<ArrayBuffer>();
  React.useEffect(() => {
    fetch(url, { stream: true, nativeBuffer: true }).then(res =>
      setBuffer(res.nativeBuffer),
    );
  }, [url]);
  return (
    <View style={styles.bubble}>
      <StreamText
        source={buffer}
        fontSize={14}
        lineHeight={21}
        color="#ECECF1"
        nativeID={logKey}
        recordDisplayLog
        onStatusChange={s => s !== 'idle' && onStatus(s)}
      />
    </View>
  );
}

const C = {
  bg: '#0A0A0F',
  border: '#24242E',
  text: '#ECECF1',
  muted: '#8B8B9A',
  accent: '#8B5CF6',
};

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: C.bg, paddingHorizontal: 16 },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingTop: 8,
  },
  back: { color: C.accent, fontSize: 16, fontWeight: '600' },
  title: { color: C.text, fontSize: 20, fontWeight: '800' },
  run: {
    backgroundColor: C.accent,
    borderRadius: 10,
    paddingHorizontal: 14,
    paddingVertical: 8,
  },
  runText: { color: '#fff', fontWeight: '700' },
  status: { color: C.muted, marginTop: 10, fontVariant: ['tabular-nums'] },
  stage: {
    height: STAGE_HEIGHT,
    marginTop: 10,
    borderRadius: 14,
    borderWidth: 1,
    borderColor: C.border,
    overflow: 'hidden',
  },
  chat: { padding: 12, gap: 10 },
  bubble: {
    width: '85%',
    borderRadius: 18,
    paddingHorizontal: 14,
    paddingVertical: 10,
    backgroundColor: '#24242E',
  },
  text: { color: C.text, fontSize: 14, lineHeight: 21 },
  results: { flex: 1, marginTop: 12 },
  summary: { color: C.text, fontSize: 11, fontFamily: 'Menlo' },
});
