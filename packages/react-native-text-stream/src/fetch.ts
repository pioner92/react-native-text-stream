import { TurboModuleRegistry, type TurboModule } from 'react-native'
import { bufferText, headerStatus } from './buffer'

/** The response body's cap by default: pages are only committed once written, so a large one is free. */
const DEFAULT_CAPACITY = 16 * 1024 * 1024
/** A model may think for minutes before its first token. */
const DEFAULT_TIMEOUT_MS = 5 * 60 * 1000

type HeadersLike =
  | Record<string, string>
  | ReadonlyArray<readonly [string, string]>
  | { forEach(callback: (value: string, name: string) => void): void }

export interface StreamFetchInit {
  /** Defaults to GET, or POST when there is a body. */
  method?: string
  headers?: HeadersLike
  body?: string
  signal?: AbortSignal
  /** The most bytes of the body to keep; past them the request stops, as `truncated`. Defaults to 16 MB. */
  capacity?: number
  /** The longest wait for the next bytes, in ms. Defaults to 5 minutes. */
  timeout?: number
  /**
   * Streams under this id too: `<StreamText streamId={id} />` shows it, wherever it is mounted and
   * in either order. A later request under the same id replaces it (and cancels this one).
   */
  streamId?: string
}

export interface StreamFetchResponse {
  status: number
  ok: boolean
  /** Lower-case names. */
  headers: Record<string, string>
  /** After redirects. */
  url: string
}

export interface StreamFetchResult {
  status: 'done' | 'error' | 'cancelled' | 'truncated'
  /** Why the request failed, for `error`. */
  error?: string
}

/** A request whose body streams into a native buffer: show it with `<StreamText source={stream} />`. */
export interface TextStream {
  /** The TSB1 buffer the body is written to, as it arrives (no JS per chunk). */
  readonly buffer: ArrayBuffer
  /** The status and headers, once they arrive; rejects if the request fails before them. */
  readonly response: Promise<StreamFetchResponse>
  /** Settles once the request ended; never rejects. */
  readonly done: Promise<StreamFetchResult>
  /** Stops the request; the body so far stays. */
  abort(): void
  /** The body received so far, decoded as UTF-8. */
  text(): string
}

interface StreamTextFetchModule extends TurboModule {
  fetch(
    url: string,
    init: { method: string; headers: string[]; body?: string; capacity: number; timeout: number; streamId?: string },
    onEvent: (type: string, ...args: unknown[]) => void
  ): { id: number; buffer: ArrayBuffer }
  cancel(id: number): void
  abortStream(streamId: string): void
  releaseStream(streamId: string): void
}

const Native = TurboModuleRegistry.get<StreamTextFetchModule>('StreamTextBuffers')

/**
 * Starts an HTTP request whose response body goes straight into a native buffer, on the network
 * thread: no JS runs per chunk. Returns at once, so the view can show the stream in the same frame.
 *
 * ```tsx
 * const stream = streamFetch(url, { method: 'POST', headers, body: JSON.stringify(payload) })
 * <StreamText source={stream} />
 * ```
 */
export function streamFetch(url: string, init: StreamFetchInit = {}): TextStream {
  if (!Native?.fetch) throw new Error('streamFetch: the StreamTextBuffers native module is missing')
  const capacity = init.capacity ?? DEFAULT_CAPACITY
  if (!Number.isInteger(capacity) || capacity < 1) {
    throw new RangeError(`streamFetch: capacity must be a positive whole number, got ${capacity}`)
  }

  let resolveResponse!: (response: StreamFetchResponse) => void
  let rejectResponse!: (error: Error) => void
  const response = new Promise<StreamFetchResponse>((resolve, reject) => {
    resolveResponse = resolve
    rejectResponse = reject
  })
  // Awaiting `response` is optional: a failure is reported through `done` too.
  response.catch(() => {})
  let resolveDone!: (result: StreamFetchResult) => void
  const done = new Promise<StreamFetchResult>((resolve) => (resolveDone = resolve))

  let finished = false
  let responded = false
  let buffer: ArrayBuffer | null = null
  const onAbort = () => abort()

  const native = Native.fetch(
    url,
    {
      method: init.method ?? (init.body != null ? 'POST' : 'GET'),
      headers: flattenHeaders(init.headers),
      body: init.body,
      capacity,
      timeout: init.timeout ?? DEFAULT_TIMEOUT_MS,
      ...(init.streamId ? { streamId: init.streamId } : {}),
    },
    (type, ...args) => {
      if (type === 'response') {
        const [status, headers, finalUrl] = args as [number, Record<string, string>, string]
        responded = true
        resolveResponse({ status, ok: status >= 200 && status < 300, headers, url: finalUrl })
      } else if (type === 'complete') {
        const [error] = args as [string | null]
        finished = true
        init.signal?.removeEventListener?.('abort', onAbort)
        if (!responded) rejectResponse(new Error(error ?? 'streamFetch: the request ended without a response'))
        const status = buffer ? headerStatus(buffer) : 'error'
        resolveDone({
          status: status === 'done' || status === 'cancelled' || status === 'truncated' ? status : 'error',
          ...(error != null ? { error } : {}),
        })
      }
    }
  )
  buffer = native.buffer

  function abort() {
    if (!finished) Native!.cancel(native.id)
  }
  if (init.signal?.aborted) abort()
  else init.signal?.addEventListener?.('abort', onAbort)

  return {
    buffer: native.buffer,
    response,
    done,
    abort,
    text: () => bufferText(native.buffer),
  }
}

/** Stops the request streaming under `streamId`; the text so far stays. */
export function abortStream(streamId: string): void {
  Native?.abortStream(streamId)
}

/**
 * Forgets the stream under `streamId`: a view showing it keeps its text, a view mounted later
 * shows nothing. Finished streams no view shows are forgotten anyway past the last 64.
 */
export function releaseStream(streamId: string): void {
  Native?.releaseStream(streamId)
}

/** Whether a `source` is a stream from `streamFetch()` rather than a buffer. */
export function isTextStream(value: unknown): value is TextStream {
  return typeof value === 'object' && value !== null && !(value instanceof ArrayBuffer) && 'buffer' in value
}

/** `[name, value, name, value, …]`, as the native module takes headers. */
function flattenHeaders(headers: HeadersLike | undefined): string[] {
  const flat: string[] = []
  if (!headers) return flat
  if (Array.isArray(headers)) {
    for (const [name, value] of headers as ReadonlyArray<readonly [string, string]>) flat.push(name, String(value))
  } else if (typeof (headers as { forEach?: unknown }).forEach === 'function') {
    ;(headers as { forEach(cb: (value: string, name: string) => void): void }).forEach((value, name) =>
      flat.push(name, value)
    )
  } else {
    for (const [name, value] of Object.entries(headers as Record<string, string>)) flat.push(name, String(value))
  }
  return flat
}
