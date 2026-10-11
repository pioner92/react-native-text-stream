import React, { forwardRef, useCallback, useImperativeHandle, useLayoutEffect, useMemo, useRef } from 'react'
import {
  codegenNativeCommands,
  processColor,
  TurboModuleRegistry,
  type ColorValue,
  type NativeSyntheticEvent,
  type TurboModule,
  type ViewProps,
} from 'react-native'
import * as NativeComponentRegistry from 'react-native/Libraries/NativeComponent/NativeComponentRegistry'
import type {
  StreamTextAlign,
  StreamTextFontWeight,
  StreamTextState,
  StreamTextStatus,
} from './types'
import { bufferText, committedBytes, TSB_HEADER_SIZE } from './buffer'

// `generation`: which source the event is about (see `sourceGeneration`).
type StatusEvent = NativeSyntheticEvent<{ status: StreamTextStatus; bytes: number; generation: number }>

type NativeProps = ViewProps & {
  source?: ArrayBuffer | null
  sourceGeneration?: number
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

// Fabric view commands carry only plain values, so an ArrayBuffer goes to the hand-written
// StreamTextBuffers TurboModule (cpp/StreamTextBuffers.hpp) first; `attach` sends its id.
interface StreamTextBuffers extends TurboModule {
  /** Keeps a natively-backed buffer for one `attach` command; 0 for a buffer created in JS. */
  put(buffer: ArrayBuffer): number
}
const Buffers = TurboModuleRegistry.get<StreamTextBuffers>('StreamTextBuffers')

const Commands = codegenNativeCommands<{
  attach(view: never, bufferId: number, generation: number): void
  clear(view: never, generation: number): void
}>({ supportedCommands: ['attach', 'clear'] })

// Every source gets a new generation; status events about an older one are dropped.
let nextGeneration = 1

export interface StreamTextProps extends ViewProps {
  /**
   * A TSB1 buffer, e.g. `res.nativeBuffer` from the patched nitro-fetch. The view reads new bytes
   * from it on every frame and grows with the text, without JS. Or hand it over with `ref.attach()`.
   */
  source?: ArrayBuffer | null
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
  /** Benchmarks only: records when bytes reach the screen, under `nativeID`. */
  recordDisplayLog?: boolean
}

/** What `<StreamText ref={…} />` gives you. */
export interface StreamTextHandle {
  /** Starts rendering `buffer` without a React render; `undefined` clears the view. */
  attach(buffer: ArrayBuffer | undefined): void
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
  { source, onStatusChange, ...props },
  ref
) {
  const native = useRef<HostInstance | null>(null)
  // Whichever was set last, the `source` prop or `attach()`; read by getStatus/getText.
  const current = useRef<ArrayBuffer | null>(source ?? null)
  const lastStatus = useRef<StreamTextStatus | null>(null)
  // A buffer attached before the native view mounted.
  const pending = useRef<{ buffer: ArrayBuffer | null; generation: number } | null>(null)
  const sourceGeneration = useMemo(() => nextGeneration++, [source])
  // The generation of `current`: status events about any other source are late, and dropped.
  const generation = useRef(sourceGeneration)

  // Only a real change of the prop resets: a buffer attached before mount must survive the first run.
  // A layout effect, so a parent's layout effect after the same commit already reads the new source.
  const previousSource = useRef(source)
  useLayoutEffect(() => {
    if (previousSource.current === source) return
    previousSource.current = source
    current.current = source ?? null
    generation.current = sourceGeneration
    lastStatus.current = null
  }, [source, sourceGeneration])

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
      if (event.nativeEvent.generation !== generation.current) return
      lastStatus.current = event.nativeEvent.status
      onStatusChange?.(event.nativeEvent.status, event.nativeEvent.bytes)
    },
    [onStatusChange]
  )

  useImperativeHandle(
    ref,
    () => {
      const attach = (buffer: ArrayBuffer | undefined) => {
        const gen = nextGeneration++
        current.current = buffer ?? null
        generation.current = gen
        lastStatus.current = null
        if (native.current) send(native.current, buffer ?? null, gen)
        else pending.current = { buffer: buffer ?? null, generation: gen }
      }
      return {
        attach,
        clear: () => attach(undefined),
        getStatus: () => {
          const buffer = current.current
          if (!buffer || buffer.byteLength < TSB_HEADER_SIZE) return IDLE
          return { status: lastStatus.current ?? 'streaming', bytes: committedBytes(buffer) }
        },
        getText: async () => (current.current ? bufferText(current.current) : ''),
      }
    },
    [send]
  )

  return (
    <NativeStreamText
      {...props}
      ref={setNative as never}
      source={source}
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
    let pendingBuffer: { buffer: ArrayBuffer | undefined } | null = null
    let handle: StreamTextHandle | null = null
    const attach = (buffer: ArrayBuffer | undefined) => {
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
