import React from 'react';
import { TurboModuleRegistry, type TurboModule } from 'react-native';
import { describe, expect, it, render } from 'react-native-harness';
import { StreamText, streamFetch, type StreamTextHandle } from 'react-native-text-stream';
import { Column, itAndroid, sleep, testing, TEXT_STYLE, waitForSnapshot } from './helpers';

/** Available only in a STREAMTEXT_TESTING native build. */
interface RegistryDiagnostics extends TurboModule {
  registryStats(): { entries: number; bytes: number };
  collectGarbageForTesting(): void;
}

const diagnostics = TurboModuleRegistry.get<RegistryDiagnostics>('StreamTextBuffers');

describe('StreamText: Fabric attach handoff teardown', () => {
  itAndroid('R6: a real Fabric surface stop after ref.attach() releases its registry handoff', async () => {
    if (!diagnostics) throw new Error('registryStats is unavailable: rebuild with STREAMTEXT_TESTING')
    const before = diagnostics.registryStats();
    const key = `r6-${Date.now()}`;
    const ref = React.createRef<StreamTextHandle>();
    await render(<Column><StreamText ref={ref} nativeID={key} recordDisplayLog {...TEXT_STYLE} /></Column>);
    await waitForSnapshot(key, () => {});

    // The public ref path puts this native buffer in the registry then enqueues Fabric `attach`.
    // stopSurface is dispatched before the next UI frame, so Fabric drops that real command.
    // Use the production buffer allocator: the general test producer intentionally retains every
    // buffer it creates, which would be an unrelated live owner during the collection assertion.
    const controller = new AbortController();
    controller.abort();
    ref.current!.attach(streamFetch('http://127.0.0.1/not-sent', {
      signal: controller.signal,
      capacity: 4 * 1024 * 1024,
    }).buffer);
    const queued = diagnostics.registryStats();
    expect(queued.entries).toBe(before.entries + 1);
    expect(queued.bytes).toBeGreaterThanOrEqual(before.bytes + 4 * 1024 * 1024);

    const surfaceId = await testing.stopSurface(key);
    // A live JS ref remains a legitimate owner even when Fabric has stopped. Release that owner
    // through the public API, then collect the actual Hermes ArrayBuffer. A strong handoff map
    // still retains the 4 MiB buffer after this; a non-owning handoff must not.
    ref.current!.clear();
    await sleep(50);
    diagnostics.collectGarbageForTesting();
    await sleep(100);
    const after = diagnostics.registryStats();
    console.info(`R6_REGISTRY ${JSON.stringify({ surfaceId, before, queued, after })}`);

    expect(after).toEqual(before);
  });
});
