import { SERVER } from './config';

export interface ClockOffset {
  /** Add to this device's Date.now() to get the stream server's (whose clock times the send logs). */
  offsetMs: number;
  /** Round trip of the sample used: the offset is right to within half of it. */
  rttMs: number;
}

/**
 * The stream server's clock against this device's, from the round trip with the least delay (as
 * NTP does). About 0 on a simulator, which shares the host's clock; a phone has its own.
 */
export async function clockOffset(samples = 12): Promise<ClockOffset> {
  let best: ClockOffset = { offsetMs: 0, rttMs: Infinity };
  for (let i = 0; i < samples; i++) {
    const sentAt = Date.now();
    const { now } = await (await globalThis.fetch(`${SERVER}/time`)).json();
    const receivedAt = Date.now();
    const rttMs = receivedAt - sentAt;
    if (rttMs < best.rttMs) best = { offsetMs: now - (sentAt + receivedAt) / 2, rttMs };
  }
  return best;
}
