import React, { forwardRef, useImperativeHandle, useMemo, useRef } from 'react'
import type { ViewProps } from 'react-native'
import {
  callback,
  getHostComponent,
  type HybridRef,
} from 'react-native-nitro-modules'
import TextStreamConfig from '../nitrogen/generated/shared/json/TextStreamConfig.json'
import type {
  StreamTextState,
  StreamTextStatus,
  TextStreamMethods,
  TextStreamProps,
} from './specs/text-stream.nitro'

const NativeStreamText = getHostComponent<TextStreamProps, TextStreamMethods>(
  'TextStream',
  () => TextStreamConfig
)

type NativeRef = HybridRef<TextStreamProps, TextStreamMethods>

export type StreamTextFontWeight =
  | 'normal'
  | 'bold'
  | '100'
  | '200'
  | '300'
  | '400'
  | '500'
  | '600'
  | '700'
  | '800'
  | '900'

export type StreamTextProps = Omit<
  TextStreamProps,
  'source' | 'fontWeight' | 'onStatusChange' | 'onAtEndChange' | 'onContentSizeChange'
> &
  Pick<ViewProps, 'style' | 'testID' | 'pointerEvents'> & {
    /** A TSB1 buffer, e.g. `res.nativeBuffer`. Or hand it over with `ref.attach()`. */
    source?: ArrayBuffer | null
    fontWeight?: StreamTextFontWeight
    /** Fires when the stream starts, and when it ends once all of its text is on screen. */
    onStatusChange?: (status: StreamTextStatus, bytes: number) => void
    /** Fires when the user scrolls away from the end, or back to it. */
    onAtEndChange?: (atEnd: boolean) => void
    /** Fires when the text's laid-out size changes, at most every 100 ms. */
    onContentSizeChange?: (width: number, height: number) => void
  }

/** What `<StreamText ref={…} />` gives you. */
export interface StreamTextHandle {
  /** Starts rendering `buffer` without a React render; `undefined` clears the view. */
  attach(buffer: ArrayBuffer | undefined): void
  /** Same as `attach(undefined)`. */
  clear(): void
  scrollToEnd(options?: { animated?: boolean }): void
  /** A snapshot of the stream's state, read synchronously. */
  getStatus(): StreamTextState
  /** The text shown so far. */
  getText(): Promise<string>
  /** Benchmarks only, with `recordDisplayLog`: `[wallClockMs, bytesShown, ...]` pairs. */
  getDisplayLog(): number[]
}

const IDLE_STATE: StreamTextState = { status: 'idle', bytes: 0, shownBytes: 0 }

type EventName = 'onStatusChange' | 'onAtEndChange' | 'onContentSizeChange'

/**
 * Native views take functions wrapped in `callback()`. A wrapper that stays the same across
 * renders and calls the latest handler keeps a new arrow function from re-sending the prop.
 */
// `_name` only ties the handler type to its prop.
function useEventProp<K extends EventName>(_name: K, handler: StreamTextProps[K]): any {
  const latest = useRef(handler)
  latest.current = handler
  const stable = useMemo(
    () => callback((...args: unknown[]) => (latest.current as any)?.(...args)),
    []
  )
  return handler ? stable : undefined
}

/**
 * A native text view that renders a TSB1 buffer as it fills, without JS per chunk.
 *
 * ```tsx
 * const streamRef = useStreamTextRef()
 * streamRef.attach(res.nativeBuffer)
 * <StreamText ref={streamRef} />
 * ```
 */
export const StreamText = forwardRef<StreamTextHandle, StreamTextProps>(
  function StreamText(
    { onStatusChange, onAtEndChange, onContentSizeChange, ...props },
    ref
  ) {
    const native = useRef<NativeRef | null>(null)
    // A buffer attached before the native view was ready; applied once it is.
    const pending = useRef<{ buffer: ArrayBuffer | undefined } | null>(null)

    const hybridRef = useMemo(
      () =>
        callback((view: NativeRef) => {
          native.current = view
          if (pending.current) {
            view.attachSource(pending.current.buffer)
            pending.current = null
          }
        }),
      []
    )

    useImperativeHandle(
      ref,
      () => {
        const attach = (buffer: ArrayBuffer | undefined) => {
          if (native.current) native.current.attachSource(buffer)
          else pending.current = { buffer }
        }
        return {
          attach,
          clear: () => attach(undefined),
          scrollToEnd: (options) =>
            native.current?.scrollToEnd(options?.animated ?? true),
          getStatus: () => native.current?.getStatus() ?? IDLE_STATE,
          getText: () => native.current?.getText() ?? Promise.resolve(''),
          getDisplayLog: () => native.current?.getDisplayLog() ?? [],
        }
      },
      []
    )

    return (
      <NativeStreamText
        {...props}
        onStatusChange={useEventProp('onStatusChange', onStatusChange)}
        onAtEndChange={useEventProp('onAtEndChange', onAtEndChange)}
        onContentSizeChange={useEventProp('onContentSizeChange', onContentSizeChange)}
        hybridRef={hybridRef}
      />
    )
  }
)

export type StreamTextRef = React.RefObject<StreamTextHandle | null> &
  StreamTextHandle

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
    let pending: { buffer: ArrayBuffer | undefined } | null = null
    let handle: StreamTextHandle | null = null
    const attach = (buffer: ArrayBuffer | undefined) => {
      if (handle) handle.attach(buffer)
      else pending = { buffer }
    }
    ref.current = {
      // React writes the view's handle here on mount and null on unmount.
      get current() {
        return handle
      },
      set current(next: StreamTextHandle | null) {
        handle = next
        if (next && pending) {
          next.attach(pending.buffer)
          pending = null
        }
      },
      attach,
      clear: () => attach(undefined),
      scrollToEnd: (options) => handle?.scrollToEnd(options),
      getStatus: () => handle?.getStatus() ?? IDLE_STATE,
      getText: () => handle?.getText() ?? Promise.resolve(''),
      getDisplayLog: () => handle?.getDisplayLog() ?? [],
    }
  }
  return ref.current
}
