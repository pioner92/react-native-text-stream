import { Native, type NativeFetchInit, type NativeStreamParser } from './native'
import { bufferText, headerStatus } from './buffer'

/** The response body's cap by default: pages are only committed once written, so a large one is free. */
const DEFAULT_CAPACITY = 16 * 1024 * 1024
/** A model may think for minutes before its first token. */
const DEFAULT_TIMEOUT_MS = 5 * 60 * 1000

type HeadersLike =
  | Record<string, string>
  | ReadonlyArray<readonly [string, string]>
  | { forEach(callback: (value: string, name: string) => void): void }

export type StreamFetchProvider = 'openai-chat' | 'openai-responses' | 'anthropic' | 'ollama'

/** Native framing and text extraction. JSON Pointer follows RFC 6901 (e.g. `/delta/text`). */
export type StreamFetchParser = {
  /** Maximum incoming line/record or SSE data payload size; defaults to 1 MiB, at most 16 MiB. */
  maxEventBytes?: number
} & (
  | { provider: StreamFetchProvider; format?: never; textPath?: never }
  | { format: 'sse'; provider?: never; textPath?: string }
  | { format: 'ndjson'; provider?: never; textPath: string }
)

export interface StreamFetchInit {
  /** Defaults to GET, or POST when there is a body. */
  method?: string
  headers?: HeadersLike
  body?: string
  signal?: AbortSignal
  /** The most bytes of the body to keep; past them the request stops, as `truncated`. Defaults to 16 MB. */
  capacity?: number
  /** Deadline for initial response (including queue/connect/upload), then idle read timeout, in ms. Defaults to 5 minutes. */
  timeout?: number
  /**
   * Streams under this id too: `<StreamText streamId={id} />` shows it, wherever it is mounted and
   * in either order. A later request under the same id replaces it (and cancels this one).
   */
  streamId?: string
  /** Decode provider events into text on the network thread. Omitted: original response bytes. */
  parser?: StreamFetchParser
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
  /** The text received so far, decoded as UTF-8; extracted text when `parser` is set. */
  text(): string
}

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
  return startFetch(url, init, normalizeRequest(init))
}

/** Internal entry point for the request prop; its key lives with the native stream, not in a JS cache. */
export function startFetch(url: string, init: StreamFetchInit, request: NativeFetchInit): TextStream {
  // URLSession/JNI use C strings; embedded NUL must never silently change the target URL.
  if (typeof url !== 'string') throw new TypeError('streamFetch: URL must be a string')
  for (let i = 0; i < url.length; i++) {
    const code = url.charCodeAt(i)
    if (code < 32 || code === 127) throw new TypeError('streamFetch: URL cannot contain control characters')
  }
  if (!Native?.fetch) throw new Error('streamFetch: the StreamTextBuffers native module is missing')
  const signal = init.signal
  if (signal?.aborted) {
    const error =
      (signal as AbortSignal & { reason?: unknown }).reason ??
      Object.assign(new Error('The request was aborted'), {
        name: 'AbortError',
      })
    const response = Promise.reject<StreamFetchResponse>(error)
    response.catch(() => {})
    return {
      buffer: Native.cancelledBuffer(request.capacity),
      response,
      done: Promise.resolve({ status: 'cancelled' }),
      abort() {},
      text: () => '',
    }
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
  let abortRequested = false
  let buffer: ArrayBuffer | null = null
  const onAbort = () => abort()

  const native = Native.fetch(url, request, (type, ...args) => {
    if (finished) return
    if (type === 'response') {
      const [status, headers, finalUrl] = args as [number, Record<string, string>, string]
      responded = true
      resolveResponse({
        status,
        ok: status >= 200 && status < 300,
        headers,
        url: finalUrl,
      })
    } else if (type === 'complete') {
      const [error] = args as [string | null]
      finished = true
      signal?.removeEventListener('abort', onAbort)
      if (!responded) rejectResponse(new Error(error ?? 'streamFetch: the request ended without a response'))
      const status = buffer ? headerStatus(buffer) : 'error'
      resolveDone({
        status: status === 'done' || status === 'cancelled' || status === 'truncated' ? status : 'error',
        ...(error != null ? { error } : {}),
      })
    }
  })
  buffer = native.buffer

  function abort() {
    if (!finished && !abortRequested) {
      abortRequested = true
      Native!.cancel(native.id)
    }
  }
  if (signal?.aborted) abort()
  else signal?.addEventListener('abort', onAbort, { once: true })

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
 * shows nothing. Finished streams no view shows are forgotten anyway past the last 64 or 64 MiB of retained data and request keys.
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

/** Shared, explicit bounds for JS, C++, Java ByteBuffer and the native view. */
export const MAX_CAPACITY = 0x7fffffff - 64
export const MAX_TIMEOUT_MS = 0x7fffffff

const MAX_EVENT_BYTES = 16 * 1024 * 1024

function normalizeParser(parser: StreamFetchParser | undefined): NativeStreamParser | undefined {
  if (parser === undefined) return undefined
  if (parser === null || typeof parser !== 'object' || Array.isArray(parser))
    throw new TypeError('streamFetch: parser must be an object')
  const maxEventBytes = parser.maxEventBytes ?? 1024 * 1024
  if (!Number.isInteger(maxEventBytes) || maxEventBytes < 1 || maxEventBytes > MAX_EVENT_BYTES)
    throw new RangeError(`streamFetch: maxEventBytes must be an integer between 1 and ${MAX_EVENT_BYTES}`)
  if (parser.provider !== undefined) {
    if (!['openai-chat', 'openai-responses', 'anthropic', 'ollama'].includes(parser.provider) ||
        parser.format !== undefined || parser.textPath !== undefined)
      throw new TypeError('streamFetch: choose a supported provider or a custom format/textPath')
    return { format: parser.provider === 'ollama' ? 'ndjson' : 'sse', extractor: parser.provider, maxEventBytes }
  }
  if (parser.format !== 'sse' && parser.format !== 'ndjson')
    throw new TypeError('streamFetch: parser.format must be sse or ndjson')
  const path = parser.textPath
  if (path !== undefined && (typeof path !== 'string' || path.length > 1024 || path.includes('\0') ||
      (path !== '' && !path.startsWith('/')) || /~(?:[^01]|$)/.test(path)))
    throw new TypeError('streamFetch: textPath must be a JSON Pointer (at most 1024 characters, without NUL)')
  if (parser.format === 'ndjson' && path === undefined)
    throw new TypeError('streamFetch: ndjson requires textPath or a provider')
  return {
    format: parser.format,
    extractor: path === undefined ? 'data' : 'json-pointer',
    ...(path === undefined ? {} : { textPath: path }),
    maxEventBytes,
  }
}

export function normalizeRequest(init: StreamFetchInit): NativeFetchInit {
  const parser = normalizeParser(init.parser)
  const capacity = init.capacity ?? DEFAULT_CAPACITY
  const timeout = init.timeout ?? DEFAULT_TIMEOUT_MS
  if (!Number.isInteger(capacity) || capacity < 1 || capacity > MAX_CAPACITY)
    throw new RangeError(`streamFetch: capacity must be an integer between 1 and ${MAX_CAPACITY}`)
  if (!Number.isInteger(timeout) || timeout < 1 || timeout > MAX_TIMEOUT_MS)
    throw new RangeError(`streamFetch: timeout must be an integer between 1 and ${MAX_TIMEOUT_MS} ms`)
  const method = (init.method ?? (init.body != null ? 'POST' : 'GET')).toUpperCase()
  const token = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/
  if (!token.test(method)) throw new TypeError('streamFetch: invalid HTTP method')
  if (init.body != null && typeof init.body !== 'string') throw new TypeError('streamFetch: body must be a string')
  if ((method === 'GET' || method === 'HEAD') && init.body != null)
    throw new TypeError('streamFetch: GET and HEAD cannot have a body')
  const flat = flattenHeaders(init.headers)
  const pairs: [string, string][] = []
  for (let i = 0; i < flat.length; i += 2) {
    const name = flat[i]!
    const value = flat[i + 1]!
    if (!token.test(name) || !/^[\t\x20-\x7e]*$/.test(value))
      throw new TypeError('streamFetch: invalid header (names must be HTTP tokens, values must be ASCII without CR/LF)')
    if (name.toLowerCase() === 'accept-encoding' && !/^(identity|gzip)$/i.test(value.trim()))
      throw new TypeError('streamFetch: explicit Accept-Encoding must be identity or gzip')
    pairs.push([name.toLowerCase(), value.trim()])
  }
  // Stable sort: header-name order is immaterial, duplicate-value order is preserved.
  pairs.sort((a, b) => a[0].localeCompare(b[0]))
  return {
    method,
    headers: pairs.flat(),
    body: init.body,
    capacity,
    timeout,
    ...(init.streamId ? { streamId: init.streamId } : {}),
    ...(parser ? { parser } : {}),
  }
}

export function requestKey(url: string, request: NativeFetchInit): string {
  const key: unknown[] = [url, request.method, request.headers, request.body, request.capacity, request.timeout]
  if (request.parser) key.push([request.parser.format, request.parser.extractor, request.parser.textPath, request.parser.maxEventBytes])
  return JSON.stringify(key)
}
