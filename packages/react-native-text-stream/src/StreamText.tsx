import React, { forwardRef, useImperativeHandle, useMemo, useRef } from 'react'
import type { ViewProps } from 'react-native'
import {
  callback,
  getHostComponent,
  type HybridRef,
} from 'react-native-nitro-modules'
import TextStreamConfig from '../nitrogen/generated/shared/json/TextStreamConfig.json'
import type {
  TextStreamMethods,
  TextStreamProps,
} from './specs/text-stream.nitro'

const NativeStreamText = getHostComponent<TextStreamProps, TextStreamMethods>(
  'TextStream',
  () => TextStreamConfig
)

type NativeRef = HybridRef<TextStreamProps, TextStreamMethods>

export type StreamTextProps = Omit<TextStreamProps, 'source'> &
  Pick<ViewProps, 'style' | 'testID' | 'pointerEvents'> & {
    /** A TSB1 buffer, e.g. `res.nativeBuffer`. Or hand it over with `ref.attach()`. */
    source?: ArrayBuffer | null
  }

/** What `<StreamText ref={…} />` gives you. */
export interface StreamTextHandle {
  /** Starts rendering `buffer` without a React render; `undefined` clears the view. */
  attach(buffer: ArrayBuffer | undefined): void
  /** Same as `attach(undefined)`. */
  clear(): void
  /** Benchmarks only, with `recordDisplayLog`: `[wallClockMs, bytesShown, ...]` pairs. */
  getDisplayLog(): number[]
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
  function StreamText(props, ref) {
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
          getDisplayLog: () => native.current?.getDisplayLog() ?? [],
        }
      },
      []
    )

    return <NativeStreamText {...props} hybridRef={hybridRef} />
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
      getDisplayLog: () => handle?.getDisplayLog() ?? [],
    }
  }
  return ref.current
}
