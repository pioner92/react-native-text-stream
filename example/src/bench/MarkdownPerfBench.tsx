import React, { useEffect, useRef, useState } from 'react';
import { ScrollView, Text, View } from 'react-native';
import { StreamText, type StreamTextFormat, type StreamTextStatus } from 'react-native-text-stream';
import { createFrameMonitor, createStreamTextTesting } from 'text-stream-testing';

// Dedicated Release entry point. Uses a native producer, so transport clocks/network jitter do
// not contaminate renderer cost. Do not inspect the view tree while this benchmark runs.
const testing = createStreamTextTesting();
const monitor = createFrameMonitor();
const sleep = (ms: number) => new Promise<void>(resolve => setTimeout(resolve, ms));
const encoder = new TextEncoder();
const UNIT = '# Heading\n\n**Bold** and *italic*, `code`, Привет 🚀.\n\n- One\n- Two\n\n```js\nconst x = 42;\n```\n\n';
const exactBuffer = (bytes: Uint8Array) => bytes.slice().buffer as ArrayBuffer;
const RICH_UNIT = UNIT + "| Name | Value | Notes |\n| :--- | :---: | ---: |\n| **Alpha** | 42 | A wrapped table cell with Unicode 世界 |\n| Beta | `code` | More content |\n\n";
function sample(size: number, id: number, rich: boolean) {
  const unit = rich ? RICH_UNIT : UNIT;
  const unitBytes = encoder.encode(unit).length;
  return encoder.encode(`Message ${id}\n\n` + unit.repeat(Math.ceil(size / unitBytes)));
}
type Stage = { id: string; format: StreamTextFormat; sources: ArrayBuffer[]; onDone: () => void };
function Content({ stage }: { stage: Stage }) {
  const completed = useRef(new Set<number>());
  const finish = (index: number, status: StreamTextStatus) => {
    if (status !== 'done') return;
    completed.current.add(index);
    if (completed.current.size === stage.sources.length) stage.onDone();
  };
  return <>{stage.sources.map((source, i) => <StreamText key={i} source={source}
    nativeID={`${stage.id}-${i}`} recordDisplayLog format={stage.format} markdownFlavor="gfm"
    fontSize={16} color="#ECECF1" onStatusChange={status => finish(i, status)} />)}</>;
}

export function MarkdownPerfBench({ rich = false }: { rich?: boolean }) {
  const [stage, setStage] = useState<Stage | null>(null);
  const [label, setLabel] = useState('Preparing renderer benchmark…');
  useEffect(() => {
    let cancelled = false;
    const results: unknown[] = [];
    const run = async (format: StreamTextFormat, size: number, count: number, streaming: boolean,
                       warm: boolean, rep: number, sources: ArrayBuffer[], payloads: Uint8Array[]) => {
      const id = `${format}-${size}-${count}-${streaming}-${warm}-${rep}`;
      setStage(null);
      if (!warm) { testing.clearPreparedLayouts(); testing.drainTextViewPool(); }
      await sleep(250);
      if (cancelled) return;
      setLabel(id);
      monitor.streamTextTotals(true);
      monitor.streamTextPhases(true);
      const beforeMB = monitor.memoryFootprintMb();
      monitor.start();
      await monitor.markPainted();
      let resolveDone!: () => void;
      const done = new Promise<void>(resolve => { resolveDone = resolve; });
      const started = Date.now();
      setStage({ id, format, sources, onDone: resolveDone });
      await monitor.markPainted();
      if (streaming) {
        testing.startProducer(sources[0], exactBuffer(payloads[0]), 512, 4, 1);
      }
      // Competing JS work is identical in both renderer modes.
      const busy = streaming ? setInterval(() => {
        const until = Date.now() + 40;
        while (Date.now() < until) { /* deliberate JS contention */ }
      }, 100) : undefined;
      let timeout: ReturnType<typeof setTimeout> | undefined;
      try {
        await Promise.race([done, new Promise((_, reject) => {
          timeout = setTimeout(() => reject(new Error(`Timeout: ${id}`)), 20000);
        })]);
      } finally {
        if (busy !== undefined) clearInterval(busy);
        if (timeout !== undefined) clearTimeout(timeout);
      }
      const statusMs = Date.now() - started;
      const idleAt = await monitor.waitIdle(3);
      const frames = await monitor.stop();
      const phases = monitor.streamTextPhases(false);
      const totals = monitor.streamTextTotals(false);
      const displayLog = monitor.streamTextDisplayLog(`${id}-0`);
      // This inspection happens after timing; never use repeated snapshots as a completion timer.
      const shown = await testing.snapshot(`${id}-0`);
      const expected = format === 'markdown' ? 'Bold and italic, code' : '**Bold** and *italic*, `code`';
      if (!shown.text.includes(expected)) throw new Error(`Wrong rendered output: ${id}`);
      results.push({ id, format, size, count, streaming, warm, rep, actualBytes: payloads.map(p => p.length),
        statusMs, settledMs: idleAt - started, frames, phases, totals, displayLog,
        beforeMB, afterMB: monitor.memoryFootprintMb(), renderedChars: shown.text.length });
    };
    void (async () => {
      await sleep(1500);
      for (let rep = 1; rep <= 3 && !cancelled; rep++) {
        for (const format of ['plain', 'markdown'] as const) {
          for (const size of [5120, 102400]) {
            const data = sample(size, rep, rich);
            const source = testing.createBuffer(data.length + 16);
            await run(format, size, 1, true, false, rep, [source], [data]);
          }
          for (const count of [1, 20]) {
            const payloads = Array.from({ length: count }, (_, i) => sample(5120, rep * 100 + i, rich));
            const sources = payloads.map(data => {
              const source = testing.createBuffer(data.length + 16);
              testing.append(source, exactBuffer(data)); testing.setStatus(source, 1); return source;
            });
            await run(format, 5120, count, false, false, rep, sources, payloads);
            await run(format, 5120, count, false, true, rep, sources, payloads);
          }
        }
      }
      if (!cancelled) {
        const response = await fetch('http://localhost:19879/results', { method: 'POST',
          headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ workload: rich ? "tables-and-syntax" : "basic-markdown", results }) });
        if (!response.ok) throw new Error(`Result save HTTP ${response.status}`);
        setLabel(`Done · ${results.length} runs saved`);
      }
    })().catch(error => { setLabel(String(error)); console.error(error); });
    return () => { cancelled = true; testing.stopProducers(); };
  }, []);
  return <View style={{ flex: 1, backgroundColor: '#101018', paddingTop: 64 }}>
    <Text style={{ color: '#A78BFA', padding: 16 }}>{label}</Text>
    <ScrollView contentContainerStyle={{ padding: 16 }}>
      <View style={{ width: 350 }}>{stage && <Content key={stage.id} stage={stage} />}</View>
    </ScrollView>
  </View>;
}
