import React, { forwardRef, useCallback, useEffect, useImperativeHandle, useLayoutEffect, useMemo, useRef } from 'react'
import {
  codegenNativeCommands,
  processColor,
  type ColorValue,
  type NativeSyntheticEvent,
  type ViewProps,
} from 'react-native'
import * as NativeComponentRegistry from 'react-native/Libraries/NativeComponent/NativeComponentRegistry'
import type { StreamTextAlign, StreamTextFontWeight, StreamTextState, StreamTextStatus } from './types'
import { bufferText, bufferSerial, committedBytes, TSB_HEADER_SIZE } from './buffer'
import {
  isTextStream,
  normalizeRequest,
  requestKey,
  releaseStream,
  startFetch,
  type StreamFetchInit,
  type TextStream,
} from './fetch'
import { Native as Buffers } from './native'

// `generation`: which source the event is about (see `sourceGeneration`).
type StatusEvent = NativeSyntheticEvent<{
  status: StreamTextStatus
  bytes: number
  generation: number
  serial?: number
}>

type NativeProps = ViewProps & {
  source?: ArrayBuffer | null
  sourceGeneration?: number
  streamId?: string
  fontSize?: number
  fontFamily?: string
  fontWeight?: string
  color?: ColorValue
  lineHeight?: number
  letterSpacing?: number
  textAlign?: string
  selectable?: boolean
  selectionColor?: ColorValue
  recordDisplayLog?: boolean
  onStatusChange?: (event: StatusEvent) => void
}

// Hand-written Fabric component (cpp/StreamTextViewShadowNode.hpp, ios/fabric/).
const NativeStreamText = NativeComponentRegistry.get<NativeProps>('StreamTextView', () => ({
  uiViewClassName: 'StreamTextView',
  validAttributes: {
    // Two ArrayBuffers deep-compare as equal (no enumerable keys): compare by identity.
    source: { diff: (a: unknown, b: unknown) => a !== b },
    sourceGeneration: true,
    streamId: true,
    fontSize: true,
    fontFamily: true,
    fontWeight: true,
    color: { process: processColor },
    lineHeight: true,
    letterSpacing: true,
    textAlign: true,
    selectable: true,
    selectionColor: { process: processColor },
    recordDisplayLog: true,
  },
  directEventTypes: {
    topStatusChange: { registrationName: 'onStatusChange' },
  },
}))

const Commands = codegenNativeCommands<{
  attach(view: never, bufferId: number, generation: number): void
  clear(view: never, generation: number): void
}>({ supportedCommands: ['attach', 'clear'] })

// Every source gets a new generation; status events about an older one are dropped.
let nextGeneration = 1

export interface StreamTextProps extends ViewProps {
  /**
   * A stream from `streamFetch()`, or a TSB1 buffer (e.g. `res.nativeBuffer` from the patched
   * nitro-fetch). The view reads new bytes on every frame and grows with the text, without JS. Or
   * hand it over with `ref.attach()`.
   */
  source?: ArrayBuffer | TextStream | null
  /**
   * Shows what `streamFetch(url, { streamId })` streams under this id: the request and the view
   * find each other natively, in any order, with no state or object passed between them. A later
   * request under the same id replaces what is shown.
   */
  streamId?: string
  /**
   * Makes the request itself and shows its body as it streams: started on mount and whenever the
   * request changes (compared by value), aborted on unmount. With a \`streamId\`, the request
   * outlives the view instead (a list's bubble scrolled away and back shows the same stream) and is
   * sent again when the request changes, after `releaseStream(id)`, or (GET/HEAD only) once evicted.
   */
  request?: StreamTextRequest
  fontSize?: number
  /** A font family or PostScript name, e.g. "Menlo"; defaults to the system font. */
  fontFamily?: string
  fontWeight?: StreamTextFontWeight
  color?: ColorValue
  /** Line height in points; defaults to the font's own. */
  lineHeight?: number
  letterSpacing?: number
  textAlign?: StreamTextAlign
  /** Defaults to true. */
  selectable?: boolean
  selectionColor?: ColorValue
  /** Fires when a stream starts, and with its final status once all of its text is on screen. */
  onStatusChange?: (status: StreamTextStatus, bytes: number) => void
  /**
   * Tests and benchmarks only: records when bytes reach the screen, under `nativeID`. No effect
   * unless the native library is built with STREAMTEXT_TESTING (the example app is).
   */
  recordDisplayLog?: boolean
}

/** `streamFetch(url, init)` as one value, for `<StreamText request={…} />`. */
export interface StreamTextRequest extends StreamFetchInit {
  url: string
}

let nextOwnStreamId = 1

/** What `<StreamText ref={…} />` gives you. */
export interface StreamTextHandle {
  /** Starts rendering `buffer` (or a stream's) without a React render; `undefined` clears the view. */
  attach(buffer: ArrayBuffer | TextStream | undefined): void
  /** Same as `attach(undefined)`. */
  clear(): void
  /** The stream's state, read synchronously. */
  getStatus(): StreamTextState
  /** The text received so far. */
  getText(): Promise<string>
}

type HostInstance = object

const IDLE: StreamTextState = { status: 'idle', bytes: 0 }

/**
 * A native text view that renders a TSB1 buffer as it fills and grows with its text, like
 * `<Text>`: put it in a chat bubble, a list cell or a ScrollView. No JS runs per chunk, and
 * it keeps growing while the JS thread is busy.
 */
export const StreamText = forwardRef<StreamTextHandle, StreamTextProps>(function StreamText(
  { source: sourceProp, request, streamId: streamIdProp, onStatusChange, ...props },
  ref
) {
  // A view making its own request without a streamId streams under one of its own.
  const sharedStreamId = streamIdProp || undefined
  const ownStreamId = useRef<string | null>(null)
  if (request && !sharedStreamId && ownStreamId.current == null) ownStreamId.current = `stream-text-${nextOwnStreamId++}`
  const streamId = sharedStreamId ?? (request ? ownStreamId.current! : undefined)
  useStreamRequest(request, streamId, sharedStreamId == null)
  // The native view takes the buffer itself (its props parser reads a raw ArrayBuffer).
  const source = toBuffer(sourceProp)
  const native = useRef<HostInstance | null>(null)
  // Whichever was set last, the `source` prop or `attach()`; read by getStatus/getText.
  const current = useRef<ArrayBuffer | null>(source ?? null)
  const followsStream = useRef(source == null && !!streamId)
  const lastStatus = useRef<StreamTextStatus | null>(null)
  // A buffer attached before the native view mounted.
  const pending = useRef<{
    buffer: ArrayBuffer | null
    generation: number
  } | null>(null)
  // These inputs define the identity; the generated number does not otherwise read them.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  const sourceGeneration = useMemo(() => nextGeneration++, [source, streamId])
  // The generation of `current`: status events about any other source are late, and dropped.
  const generation = useRef(sourceGeneration)

  // Only a real change of the prop resets: a buffer attached before mount must survive the first run.
  // A layout effect, so a parent's layout effect after the same commit already reads the new source.
  const previousSource = useRef(source)
  const previousStreamId = useRef(streamId)
  useLayoutEffect(() => {
    if (previousSource.current === source && previousStreamId.current === streamId) return
    previousSource.current = source
    previousStreamId.current = streamId
    current.current = source ?? null
    followsStream.current = source == null && !!streamId
    generation.current = sourceGeneration
    lastStatus.current = null
  }, [source, streamId, sourceGeneration])

  // Only one notification per publication/replacement, never per chunk. Retain the displayed
  // buffer so refs still work after releaseStream(), exactly like the native view does.
  const acceptStream = useCallback(
    (buffer: ArrayBuffer) => {
      if (bufferSerial(current.current) !== bufferSerial(buffer)) lastStatus.current = null
      current.current = buffer
      followsStream.current = true
      generation.current = sourceGeneration
    },
    [sourceGeneration]
  )
  useLayoutEffect(() => {
    if (source != null || !streamId || !Buffers) return
    let active = true
    const watch = Buffers.watchStream(streamId, (buffer) => {
      if (active) acceptStream(buffer)
    })
    if (watch.buffer && generation.current === sourceGeneration) acceptStream(watch.buffer)
    return () => {
      active = false
      Buffers?.unwatchStream(watch.token)
    }
  }, [source, streamId, sourceGeneration, acceptStream])

  const readCurrent = useCallback(() => {
    if (followsStream.current && streamId) {
      const latest = Buffers?.streamBuffer(streamId)
      if (latest) acceptStream(latest)
    }
    return current.current
  }, [streamId, acceptStream])

  const send = useCallback((instance: HostInstance, buffer: ArrayBuffer | null, gen: number) => {
    if (!buffer) return Commands.clear(instance as never, gen)
    const bufferId = Buffers?.put(buffer) ?? 0
    if (bufferId === 0) {
      // The view can't read it: show nothing rather than keep the previous buffer.
      console.warn('StreamText: attach() needs a natively-backed ArrayBuffer, e.g. res.nativeBuffer')
      current.current = null
      return Commands.clear(instance as never, gen)
    }
    Commands.attach(instance as never, bufferId, gen)
  }, [])

  const setNative = useCallback(
    (instance: HostInstance | null) => {
      native.current = instance
      if (instance && pending.current) {
        send(instance, pending.current.buffer, pending.current.generation)
        pending.current = null
      }
    },
    [send]
  )

  const handleStatus = useCallback(
    (event: StatusEvent) => {
      const buffer = readCurrent()
      if (event.nativeEvent.generation !== generation.current) return
      // A user streamId may name many requests. Serial identifies the actual buffer, including
      // events already queued for JS when a replacement was published.
      if ((event.nativeEvent.serial ?? 0) !== bufferSerial(buffer)) return
      lastStatus.current = event.nativeEvent.status
      onStatusChange?.(event.nativeEvent.status, event.nativeEvent.bytes)
    },
    [onStatusChange, readCurrent]
  )

  useImperativeHandle(ref, () => {
    const attach = (input: ArrayBuffer | TextStream | undefined) => {
      const buffer = toBuffer(input)
      const gen = nextGeneration++
      current.current = buffer ?? null
      followsStream.current = false
      generation.current = gen
      lastStatus.current = null
      if (native.current) send(native.current, buffer ?? null, gen)
      else pending.current = { buffer: buffer ?? null, generation: gen }
    }
    return {
      attach,
      clear: () => attach(undefined),
      getStatus: () => {
        const buffer = readCurrent()
        if (!buffer || buffer.byteLength < TSB_HEADER_SIZE) return IDLE
        return {
          status: lastStatus.current ?? 'streaming',
          bytes: committedBytes(buffer),
        }
      },
      getText: async () => {
        const buffer = readCurrent()
        return buffer ? bufferText(buffer) : ''
      },
    }
  }, [send, readCurrent])

  return (
    <NativeStreamText
      {...props}
      ref={setNative as never}
      source={source}
      streamId={streamId}
      sourceGeneration={sourceGeneration}
      // Always subscribed: getStatus() needs the final status even without an onStatusChange prop.
      onStatusChange={handleStatus}
    />
  )
})

export type StreamTextRef = React.RefObject<StreamTextHandle | null> & StreamTextHandle

/**
 * A ref for `<StreamText ref={streamRef} />` that you can call directly, even before the view mounts.
 *
 * ```tsx
 * const streamRef = useStreamTextRef()
 * const res = await fetch(url, { stream: true, nativeBuffer: true })
 * streamRef.attach(res.nativeBuffer) // no setState, no re-render
 * ```
 */
export function useStreamTextRef(): StreamTextRef {
  const ref = useRef<StreamTextRef | null>(null)
  if (ref.current == null) {
    let pendingBuffer: { buffer: ArrayBuffer | TextStream | undefined } | null = null
    let handle: StreamTextHandle | null = null
    const attach = (buffer: ArrayBuffer | TextStream | undefined) => {
      if (handle) handle.attach(buffer)
      else pendingBuffer = { buffer }
    }
    ref.current = {
      // React writes the view's handle here on mount and null on unmount.
      get current() {
        return handle
      },
      set current(next: StreamTextHandle | null) {
        handle = next
        if (next && pendingBuffer) {
          next.attach(pendingBuffer.buffer)
          pendingBuffer = null
        }
      },
      attach,
      clear: () => attach(undefined),
      getStatus: () => handle?.getStatus() ?? IDLE,
      getText: () => handle?.getText() ?? Promise.resolve(''),
    }
  }
  return ref.current
}

function toBuffer(source: ArrayBuffer | TextStream | null | undefined): ArrayBuffer | null | undefined {
  return isTextStream(source) ? source.buffer : source
}

/**
 * `<StreamText request>`: sends it under `streamId` when it changes. A view's own stream (`owned`)
 * ends with the view; one under the app's streamId outlives it, and a remount with the same request
 * doesn't send it again.
 */
function useStreamRequest(request: StreamTextRequest | undefined, streamId: string | undefined, owned: boolean) {
  // Compare actual outgoing header pairs, including Headers/Map/forEach implementations.
  const normalized = request ? normalizeRequest(request) : null
  const key = request && normalized ? requestKey(request.url, normalized) : null
  const signal = request?.signal
  const latest = useRef({ request, normalized })
  latest.current = { request, normalized }
  const previousSignal = useRef(signal)
  useEffect(() => {
    const changedSignal = previousSignal.current !== signal
    previousSignal.current = signal
    const { request: current, normalized: init } = latest.current
    if (!current || !init || !streamId || key == null) return
    // Only a safe method is sent again once its finished stream was evicted: a POST may cost a new generation.
    const resend = init.method === 'GET' || init.method === 'HEAD'
    if (!owned && !changedSignal && Buffers?.hasStream(streamId, key, !resend)) return
    const stream = startFetch(current.url, current, {
      ...init,
      streamId,
      requestKey: key,
    })
    if (!owned) return
    return () => {
      stream.abort()
      releaseStream(streamId)
    }
  }, [key, signal, streamId, owned])
}
