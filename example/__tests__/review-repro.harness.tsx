import React from 'react';
import { Platform, Text, View as NativeView } from 'react-native';
import { beforeAll, describe, expect, it, render, waitFor } from 'react-native-harness';
import {
  StreamText,
  streamFetch,
  releaseStream,
  type StreamTextHandle,
  type StreamTextStatus,
} from 'react-native-text-stream';
import {
  buffer,
  Column,
  newKey,
  sleep,
  snapshot,
  TEXT_STYLE,
  TSB,
  waitForSnapshot,
  waitForText,
} from './helpers';

// These are device reproductions for findings R1–R5 and R9 in
// RENDER_FETCH_REVIEW_2026-10-13.md. They deliberately assert the intended
// contract and are required CI regressions after the fixes. Each test records the
// native snapshot, ref state and status events immediately before its decisive
// assertion; that evidence is useful when a Fabric race makes the screen and
// the JS ref disagree.
// `server/review-test-server.mjs`; kept separate from the normal
// fetch fixture so its side-effect counter cannot be polluted by other suites.
const SERVER = 'http://127.0.0.1:19878';
let serial = 0;
const id = (name: string) => `review-${name}-${Date.now()}-${++serial}`;

type StatusEvent = [StreamTextStatus, number];
type LifecycleCounts = { setups: number; cleanups: number };
const evidence = async (
  label: string,
  key: string,
  ref: React.RefObject<StreamTextHandle | null>,
  events: StatusEvent[],
  extra?: Record<string, unknown>,
) => {
  const record = {
    timestamp: new Date().toISOString(),
    platform: Platform.OS,
    label,
    native: await snapshot(key),
    ref: {
      status: ref.current?.getStatus(),
      text: await ref.current?.getText(),
    },
    events,
    ...extra,
  };
  // JSON makes the harness log copyable as a single artifact for both devices.
  console.info(`REVIEW_REPRO ${JSON.stringify(record)}`);
  const response = await fetch(`${SERVER}/evidence`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(record),
  });
  if (!response.ok) throw new Error(`could not save device evidence for ${label}`);
};

function View({
  streamId,
  source,
  streamRef,
  viewKey,
  onStatusChange,
  caption,
}: {
  streamId?: string;
  source?: ArrayBuffer;
  streamRef?: React.RefObject<StreamTextHandle | null>;
  viewKey: string;
  onStatusChange?: (status: StreamTextStatus, bytes: number) => void;
  caption?: string;
}) {
  return (
    <NativeView style={{ flex: 1, backgroundColor: '#ffffff', paddingTop: 80, paddingHorizontal: 16 }}>
    <Column>
      {caption ? <Text>{caption}</Text> : null}
      <StreamText
        ref={streamRef}
        streamId={streamId}
        source={source}
        nativeID={viewKey}
        recordDisplayLog
        onStatusChange={onStatusChange}
        {...TEXT_STYLE}
        color="#000000"
      />
    </Column>
    </NativeView>
  );
}

function LifecycleProbe({ counts }: { counts: LifecycleCounts }) {
  React.useEffect(() => {
    counts.setups += 1;
    return () => {
      counts.cleanups += 1;
    };
  }, [counts]);
  return null;
}

beforeAll(async () => {
  const ready = await fetch(`${SERVER}/count?key=ready`);
  if (!ready.ok) throw new Error(`review reproductions need ${SERVER}`);
});

async function resetCount(key: string) {
  const response = await fetch(`${SERVER}/reset?key=${encodeURIComponent(key)}`, { method: 'POST' });
  if (!response.ok) throw new Error(`could not reset review counter ${key}`);
}

async function count(key: string) {
  const response = await fetch(`${SERVER}/count?key=${encodeURIComponent(key)}`);
  if (!response.ok) throw new Error(`could not read review counter ${key}`);
  return (await response.json()) as { count: number };
}

describe('review regressions: native source ownership', () => {
  it('R1: a late named publication cannot replace a buffer attached through the ref', async () => {
    const streamId = id('r1-attach');
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    await render(
      <View
        streamId={streamId}
        streamRef={ref}
        viewKey={viewKey}
        onStatusChange={(status, bytes) => events.push([status, bytes])}
      />,
    );
    ref.current!.attach(buffer('manual winner', TSB.done));
    await waitForText(viewKey, 'manual winner');

    const late = streamFetch(`${SERVER}/stream?text=late%20publication`, { streamId });
    expect(await late.done).toEqual({ status: 'done' });
    await sleep(120);
    await evidence('R1 attach then named publication', viewKey, ref, events);

    expect((await snapshot(viewKey)).text).toBe('manual winner');
    await expect(ref.current!.getText()).resolves.toBe('manual winner');
  });

  it('R1: clear remains idle when its old streamId publishes afterwards', async () => {
    const streamId = id('r1-clear');
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    await render(
      <View
        streamId={streamId}
        streamRef={ref}
        viewKey={viewKey}
        onStatusChange={(status, bytes) => events.push([status, bytes])}
      />,
    );
    ref.current!.clear();
    await waitForSnapshot(viewKey, s => {
      expect(s.text).toBe('');
      expect(s.height).toBe(0);
    });

    const late = streamFetch(`${SERVER}/stream?text=must%20stay%20cleared`, { streamId });
    expect(await late.done).toEqual({ status: 'done' });
    await sleep(120);
    await evidence('R1 clear then named publication', viewKey, ref, events);

    const s = await snapshot(viewKey);
    expect([s.text, s.status, s.height]).toEqual(['', 'idle', 0]);
    expect(ref.current!.getStatus()).toEqual({ status: 'idle', bytes: 0 });
  });

  it('R1: an explicit source stays the single owner when the same streamId publishes', async () => {
    const streamId = id('r1-source');
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    await render(
      <View
        streamId={streamId}
        source={buffer('explicit source', TSB.done)}
        streamRef={ref}
        viewKey={viewKey}
        onStatusChange={(status, bytes) => events.push([status, bytes])}
      />,
    );
    await waitForText(viewKey, 'explicit source');
    const late = streamFetch(`${SERVER}/stream?text=named%20replacement`, { streamId });
    expect(await late.done).toEqual({ status: 'done' });
    await sleep(120);
    await evidence('R1 explicit source plus named publication', viewKey, ref, events);

    expect((await snapshot(viewKey)).text).toBe('explicit source');
    await expect(ref.current!.getText()).resolves.toBe('explicit source');
  });

  it('R1: switching from an id to an explicit source invalidates its old subscription', async () => {
    const streamId = id('r1-id-to-source');
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    const onStatusChange = (status: StreamTextStatus, bytes: number) => { events.push([status, bytes]); };
    const mounted = await render(<View streamId={streamId} streamRef={ref} viewKey={viewKey} onStatusChange={onStatusChange} />);
    const first = streamFetch(`${SERVER}/stream?text=first`, { streamId });
    await first.done;
    await waitForText(viewKey, 'first');
    const source = buffer('explicit replacement', TSB.done);
    await mounted.rerender(<View streamId={streamId} source={source} streamRef={ref} viewKey={viewKey} onStatusChange={onStatusChange} />);
    await waitForText(viewKey, 'explicit replacement');
    const before = await snapshot(viewKey);
    const eventCount = events.length;
    const late = streamFetch(`${SERVER}/stream?text=stale%20publication`, { streamId });
    await late.done;
    await sleep(120);
    await evidence('R1 id to explicit source then publication', viewKey, ref, events);
    const after = await snapshot(viewKey);
    expect([after.text, after.status, after.height]).toEqual([before.text, before.status, before.height]);
    await expect(ref.current!.getText()).resolves.toBe('explicit replacement');
    expect(events.length).toBe(eventCount);
  });

  it('R2: removing source immediately shows the already-published named stream', async () => {
    const streamId = id('r2-remove-source');
    const named = streamFetch(`${SERVER}/stream?text=named%20buffer`, { streamId });
    expect(await named.done).toEqual({ status: 'done' });
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    const explicit = buffer('explicit buffer', TSB.done);
    const mounted = await render(
      <View
        streamId={streamId}
        source={explicit}
        streamRef={ref}
        viewKey={viewKey}
        onStatusChange={(status, bytes) => events.push([status, bytes])}
      />,
    );
    // If this is already `named buffer`, R1 has won before the R2 transition; retain the
    // structured evidence and still exercise the source-removal edge below.
    await waitForSnapshot(viewKey, s => expect(s.text.length).toBeGreaterThan(0));
    await evidence('R2 before removing explicit source', viewKey, ref, events);
    await mounted.rerender(
      <View
        streamId={streamId}
        streamRef={ref}
        viewKey={viewKey}
        onStatusChange={(status, bytes) => events.push([status, bytes])}
      />,
    );
    await sleep(120);
    await evidence('R2 remove explicit source', viewKey, ref, events);

    expect((await snapshot(viewKey)).text).toBe('named buffer');
    await expect(ref.current!.getText()).resolves.toBe('named buffer');
  });

  it('R9: switching from a filled id to an empty id clears the native screen immediately', async () => {
    const firstId = id('r9-first');
    const secondId = id('r9-empty');
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    const mounted = await render(
      <View
        streamId={firstId}
        streamRef={ref}
        viewKey={viewKey}
        onStatusChange={(status, bytes) => events.push([status, bytes])}
      />,
    );
    const first = streamFetch(`${SERVER}/stream?text=previous%20message`, { streamId: firstId });
    expect(await first.done).toEqual({ status: 'done' });
    await waitForText(viewKey, 'previous message');
    await mounted.rerender(
      <View
        streamId={secondId}
        streamRef={ref}
        viewKey={viewKey}
        caption="R9: current stream B is empty. Expected: no text below."
        onStatusChange={(status, bytes) => events.push([status, bytes])}
      />,
    );
    await sleep(120);
    await evidence('R9 switch to an empty streamId', viewKey, ref, events);

    const switched = await snapshot(viewKey);
    expect([switched.text, switched.status, switched.height]).toEqual(['', 'idle', 0]);
    expect(ref.current!.getStatus()).toEqual({ status: 'idle', bytes: 0 });
    await expect(ref.current!.getText()).resolves.toBe('');

    const second = streamFetch(`${SERVER}/stream?text=new%20message`, { streamId: secondId });
    expect(await second.done).toEqual({ status: 'done' });
    await waitForText(viewKey, 'new message');
  });
  it('R9: late writes from the old id cannot repopulate an empty or removed id', async () => {
    const firstId = id('r9-open-first');
    const secondId = id('r9-open-empty');
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    const onStatusChange = (status: StreamTextStatus, bytes: number) => { events.push([status, bytes]); };
    const mounted = await render(<View streamId={firstId} streamRef={ref} viewKey={viewKey} onStatusChange={onStatusChange} />);
    const prefix = 'old prefix\n'.repeat(128);
    const first = streamFetch(`${SERVER}/paused?key=${firstId}&first=${encodeURIComponent(prefix)}`, { streamId: firstId });
    await waitForText(viewKey, prefix);
    await mounted.rerender(<View streamId={secondId} streamRef={ref} viewKey={viewKey} onStatusChange={onStatusChange} />);
    await waitForSnapshot(viewKey, s => expect([s.text, s.height, s.status]).toEqual(['', 0, 'idle']));
    const eventCount = events.length;
    const completion = await fetch(`${SERVER}/complete?key=${firstId}&tail=late%20tail`, { method: 'POST' });
    expect(completion.ok).toBe(true);
    expect(await first.done).toEqual({ status: 'done' });
    await sleep(120);
    await evidence('R9 old stream completed after identity switch', viewKey, ref, events);
    expect(events.length).toBe(eventCount);
    expect((await snapshot(viewKey)).text).toBe('');
    const second = streamFetch(`${SERVER}/stream?text=current`, { streamId: secondId });
    await second.done;
    await waitForText(viewKey, 'current');
    await mounted.rerender(<View streamRef={ref} viewKey={viewKey} onStatusChange={onStatusChange} />);
    await waitForSnapshot(viewKey, s => expect([s.text, s.height, s.status]).toEqual(['', 0, 'idle']));
    await expect(ref.current!.getText()).resolves.toBe('');
  });
});

describe('review regressions: declarative request lifecycle', () => {
  it('R3: an already-aborted owned request reaches the renderer as cancelled once', async () => {
    const controller = new AbortController();
    controller.abort();
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    await render(
      <Column>
        <StreamText
          ref={ref}
          request={{ url: `${SERVER}/stream?text=must-not-start`, signal: controller.signal }}
          nativeID={viewKey}
          recordDisplayLog
          onStatusChange={(status, bytes) => events.push([status, bytes])}
          {...TEXT_STYLE}
        />
      </Column>,
    );
    await sleep(150);
    await evidence('R3 owned pre-abort', viewKey, ref, events);

    expect((await snapshot(viewKey)).status).toBe('cancelled');
    expect(ref.current!.getStatus()).toEqual({ status: 'cancelled', bytes: 0 });
    expect(events).toEqual([['cancelled', 0]]);
  });

  it('R3: an already-aborted shared request preserves the existing named stream', async () => {
    const streamId = id('r3-shared');
    const shared = streamFetch(`${SERVER}/stream?text=shared%20winner`, { streamId });
    await shared.done;
    const controller = new AbortController();
    controller.abort();
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    await render(
      <Column><StreamText ref={ref} streamId={streamId}
        request={{ url: `${SERVER}/stream?text=must-not-replace`, signal: controller.signal }}
        nativeID={viewKey} recordDisplayLog {...TEXT_STYLE}
        onStatusChange={(status, bytes) => { events.push([status, bytes]); }} /></Column>,
    );
    await waitForText(viewKey, 'shared winner');
    await evidence('R3 shared pre-abort preserves existing stream', viewKey, ref, events);
    await expect(ref.current!.getText()).resolves.toBe('shared winner');
    expect(ref.current!.getStatus().status).toBe('done');
    expect(events.some(([status]) => status === 'cancelled')).toBe(false);
  });

  it('R3: switching an owned pre-abort to a shared pre-abort never hides the shared buffer', async () => {
    const ownedAbort = new AbortController();
    ownedAbort.abort();
    const sharedAbort = new AbortController();
    sharedAbort.abort();
    const streamId = id('r3-owned-to-shared');
    const shared = streamFetch(`${SERVER}/stream?text=shared%20after%20owned`, { streamId });
    await shared.done;
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    const onStatusChange = (status: StreamTextStatus, bytes: number) => { events.push([status, bytes]); };
    const mounted = await render(<Column><StreamText ref={ref}
      request={{ url: `${SERVER}/stream?text=owned`, signal: ownedAbort.signal }}
      nativeID={viewKey} recordDisplayLog {...TEXT_STYLE} onStatusChange={onStatusChange} /></Column>);
    await waitForSnapshot(viewKey, s => expect(s.status).toBe('cancelled'));
    events.length = 0;
    await mounted.rerender(<Column><StreamText ref={ref} streamId={streamId}
      request={{ url: `${SERVER}/stream?text=must-not-replace`, signal: sharedAbort.signal }}
      nativeID={viewKey} recordDisplayLog {...TEXT_STYLE} onStatusChange={onStatusChange} /></Column>);
    await waitForText(viewKey, 'shared after owned');
    await evidence('R3 owned to shared pre-abort', viewKey, ref, events);
    await expect(ref.current!.getText()).resolves.toBe('shared after owned');
    expect(events.some(([status]) => status === 'cancelled')).toBe(false);
  });

  it('R4: StrictMode issues an owned POST side effect exactly once', async () => {
    const effectKey = id('r4-strict-post');
    await resetCount(effectKey);
    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    const lifecycle: LifecycleCounts = { setups: 0, cleanups: 0 };
    await render(
      <React.StrictMode>
        <Column>
          <LifecycleProbe counts={lifecycle} />
          <StreamText
            ref={ref}
            request={{
              url: `${SERVER}/effect?key=${encodeURIComponent(effectKey)}&delay=120`,
              method: 'POST',
              body: 'one paid mutation',
            }}
            nativeID={viewKey}
            recordDisplayLog
            onStatusChange={(status, bytes) => events.push([status, bytes])}
            {...TEXT_STYLE}
          />
        </Column>
      </React.StrictMode>,
    );
    await waitFor(async () => expect((await count(effectKey)).count).toBeGreaterThanOrEqual(1), { timeout: 5000 });
    // Give the replayed effect enough time to reach the loopback server too.
    await sleep(500);
    await evidence('R4 StrictMode owned POST', viewKey, ref, events, {
      effectCount: (await count(effectKey)).count,
      lifecycle,
    });

    expect((await count(effectKey)).count).toBe(1);
  });

  it('R5: after native eviction, a named POST is still never reissued without releaseStream', async () => {
    const streamId = id('r5-post-stream');
    const effectKey = id('r5-post-effect');
    await resetCount(effectKey);
    const request = {
      url: `${SERVER}/effect?key=${encodeURIComponent(effectKey)}`,
      method: 'POST',
      body: 'must not implicitly replay',
    } as const;
    const firstKey = newKey();
    const first = await render(
      <Column>
        <StreamText streamId={streamId} request={request} nativeID={firstKey} recordDisplayLog {...TEXT_STYLE} />
      </Column>,
    );
    await waitFor(async () => expect((await count(effectKey)).count).toBe(1), { timeout: 5000 });
    await waitForSnapshot(firstKey, s => expect(s.status).toBe('done'), 5000);
    await first.unmount();

    // Native bounds completed entries and safe-request tombstones; mutation keys must survive. These
    // are real declarative requests (not direct streamFetch), so each native entry has the key
    // whose retention R5 is about. The 204 body bounds data and renderer work.
    let completed = 0;
    for (let batch = 0; batch < 66; batch++) {
      const size = batch === 65 ? 1 : 64;
      const streamIds = Array.from({ length: size }, (_, index) => id(`r5-pressure-${batch}-${index}`));
      const terminals = new Set<string>();
      const pressure = await render(
        <Column>
          {streamIds.map(pressureId => (
            <StreamText
              key={pressureId}
              streamId={pressureId}
              request={{ url: `${SERVER}/empty`, capacity: 1 }}
              onStatusChange={status => {
                if (status === 'done' || status === 'error' || status === 'cancelled' || status === 'truncated') {
                  terminals.add(pressureId);
                }
              }}
              {...TEXT_STYLE}
              color="#000000"
            />
          ))}
        </Column>,
      );
      await waitFor(() => expect(terminals.size).toBe(size), { timeout: 20_000, interval: 20 });
      await pressure.unmount();
      completed += size;
      if (completed % 512 === 0 || completed === 4161) {
        const progress = { timestamp: new Date().toISOString(), platform: Platform.OS, label: 'R5 pressure', completed };
        console.info(`REVIEW_REPRO ${JSON.stringify(progress)}`);
        await fetch(`${SERVER}/evidence`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(progress),
        });
      }
    }

    const viewKey = newKey();
    const ref = React.createRef<StreamTextHandle>();
    const events: StatusEvent[] = [];
    const remounted = await render(
      <Column>
        <StreamText
          ref={ref}
          streamId={streamId}
          request={request}
          nativeID={viewKey}
          recordDisplayLog
          onStatusChange={(status, bytes) => events.push([status, bytes])}
          {...TEXT_STYLE}
        />
      </Column>,
    );
    await sleep(500);
    await evidence('R5 remount after native tombstone pressure', viewKey, ref, events, { effectCount: (await count(effectKey)).count });

    expect((await count(effectKey)).count).toBe(1);
    await remounted.unmount();
    releaseStream(streamId);
    const retriedKey = newKey();
    await render(<Column><StreamText streamId={streamId} request={request} nativeID={retriedKey}
      recordDisplayLog {...TEXT_STYLE} /></Column>);
    await waitFor(async () => expect((await count(effectKey)).count).toBe(2), { timeout: 5000 });
    await waitForSnapshot(retriedKey, s => expect(s.status).toBe('done'));
  });
});
