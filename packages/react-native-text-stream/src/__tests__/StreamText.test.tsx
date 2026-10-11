import React, { useImperativeHandle } from 'react'
import { fireEvent, render, screen } from '@testing-library/react-native'
import { StreamText, useStreamTextRef, type StreamTextHandle, type StreamTextRef } from '../StreamText'
import { TSB_STATUS, tsbBuffer } from './tsb'

// The native boundary is mocked in jest.setup.js.
const mockCommands = require('react-native/Libraries/Utilities/codegenNativeCommands').default()
const mockBuffers = require('react-native/Libraries/TurboModule/TurboModuleRegistry').buffers

let nextId = 1
beforeEach(() => {
  jest.clearAllMocks()
  nextId = 1
  mockBuffers.put.mockImplementation(() => nextId++)
})

const host = () => screen.getByTestId('stream')
const statusEvent = (status: string, bytes: number) => ({ nativeEvent: { status, bytes } })

/** Renders StreamText with a ref; `useStreamTextRef` when asked, else a plain ref object. */
async function renderStream(props: Partial<React.ComponentProps<typeof StreamText>> = {}) {
  const ref = React.createRef<StreamTextHandle>()
  const result = await render(<StreamText ref={ref} testID="stream" {...props} />)
  return { ref, ...result }
}

describe('StreamText', () => {
  it('passes the source prop and style props through to the native view', async () => {
    const source = tsbBuffer('hi')
    await renderStream({ source, fontSize: 18, color: 'red', selectable: false })
    expect(host().props.source).toBe(source)
    expect(host().props.fontSize).toBe(18)
    expect(host().props.selectable).toBe(false)
  })

  it('always subscribes to status events, even without onStatusChange', async () => {
    await renderStream()
    expect(typeof host().props.onStatusChange).toBe('function')
  })

  it('forwards status events to onStatusChange', async () => {
    const onStatusChange = jest.fn()
    await renderStream({ onStatusChange })
    await fireEvent(host(), 'statusChange', statusEvent('done', 42))
    expect(onStatusChange).toHaveBeenCalledWith('done', 42)
  })

  describe('ref', () => {
    it('attach() puts the buffer in StreamTextBuffers and sends its id to the view', async () => {
      const { ref } = await renderStream()
      const buffer = tsbBuffer('hello')
      ref.current!.attach(buffer)
      expect(mockBuffers.put).toHaveBeenCalledWith(buffer)
      expect(mockCommands.attach).toHaveBeenCalledTimes(1)
      expect(mockCommands.attach.mock.calls[0][1]).toBe(1)
    })

    it('attach(undefined) and clear() clear the view', async () => {
      const { ref } = await renderStream()
      ref.current!.attach(undefined)
      ref.current!.clear()
      expect(mockCommands.clear).toHaveBeenCalledTimes(2)
      expect(mockBuffers.put).not.toHaveBeenCalled()
    })

    it('a buffer the view cannot read (made in JS) warns, clears the view and reads as idle', async () => {
      const warn = jest.spyOn(console, 'warn').mockImplementation(() => {})
      mockBuffers.put.mockReturnValue(0)
      const { ref } = await renderStream()
      ref.current!.attach(tsbBuffer('from JS'))
      expect(warn).toHaveBeenCalledTimes(1)
      expect(mockCommands.attach).not.toHaveBeenCalled()
      expect(mockCommands.clear).toHaveBeenCalledTimes(1)
      expect(ref.current!.getStatus()).toEqual({ status: 'idle', bytes: 0 })
      await expect(ref.current!.getText()).resolves.toBe('')
      warn.mockRestore()
    })

    it('getStatus() is idle without a buffer, then streaming with the committed bytes', async () => {
      const { ref } = await renderStream()
      expect(ref.current!.getStatus()).toEqual({ status: 'idle', bytes: 0 })
      ref.current!.attach(tsbBuffer('hello'))
      expect(ref.current!.getStatus()).toEqual({ status: 'streaming', bytes: 5 })
    })

    it('getStatus() takes the final status from the view, without an onStatusChange prop', async () => {
      const { ref } = await renderStream()
      ref.current!.attach(tsbBuffer('hello', { status: TSB_STATUS.done }))
      await fireEvent(host(), 'statusChange', statusEvent('done', 5))
      expect(ref.current!.getStatus()).toEqual({ status: 'done', bytes: 5 })
      // A new buffer starts over.
      ref.current!.attach(tsbBuffer('again'))
      expect(ref.current!.getStatus().status).toBe('streaming')
    })

    it('getText() decodes the committed bytes of the attached buffer', async () => {
      const { ref } = await renderStream()
      ref.current!.attach(tsbBuffer('Привет 🚀'))
      await expect(ref.current!.getText()).resolves.toBe('Привет 🚀')
    })

    it('an unrelated prop change does not undo a ref attach', async () => {
      const source = tsbBuffer('from prop')
      const { ref, rerender } = await renderStream({ source })
      ref.current!.attach(tsbBuffer('from ref'))
      await rerender(<StreamText ref={ref} testID="stream" source={source} color="blue" />)
      await expect(ref.current!.getText()).resolves.toBe('from ref')
      expect(mockCommands.clear).not.toHaveBeenCalled()
    })

    it('a new source prop replaces what the ref attached', async () => {
      const { ref, rerender } = await renderStream({ source: tsbBuffer('first') })
      ref.current!.attach(tsbBuffer('from ref'))
      await rerender(<StreamText ref={ref} testID="stream" source={tsbBuffer('second')} />)
      await expect(ref.current!.getText()).resolves.toBe('second')
      expect(ref.current!.getStatus().status).toBe('streaming')
    })

    it("a parent's layout effect already reads the new source prop", async () => {
      const seen: string[] = []
      function Parent({ text }: { text: string }) {
        const ref = React.useRef<StreamTextHandle>(null)
        const source = React.useMemo(() => tsbBuffer(text, { status: TSB_STATUS.done }), [text])
        React.useLayoutEffect(() => {
          ref.current!.getText().then((t) => seen.push(`${t}:${ref.current!.getStatus().bytes}`))
        }, [source])
        return <StreamText ref={ref} testID="stream" source={source} />
      }
      const { rerender } = await render(<Parent text="first" />)
      await rerender(<Parent text="second!" />)
      expect(seen).toEqual(['first:5', 'second!:7'])
    })

    it('getText() reads the source prop when nothing was attached', async () => {
      const { ref } = await renderStream({ source: tsbBuffer('prop text') })
      await expect(ref.current!.getText()).resolves.toBe('prop text')
    })
  })
})

describe('useStreamTextRef', () => {
  function Screen({ refOut, mounted = true }: { refOut: (ref: StreamTextRef) => void; mounted?: boolean }) {
    const streamRef = useStreamTextRef()
    refOut(streamRef)
    return mounted ? <StreamText ref={streamRef} testID="stream" /> : null
  }

  it('is the same object on every render', async () => {
    const refs: StreamTextRef[] = []
    const { rerender } = await render(<Screen refOut={(r) => refs.push(r)} />)
    await rerender(<Screen refOut={(r) => refs.push(r)} />)
    expect(refs[0]).toBe(refs[1])
  })

  it('reads idle and empty before the view mounts', async () => {
    let ref!: StreamTextRef
    await render(<Screen refOut={(r) => (ref = r)} mounted={false} />)
    expect(ref.current).toBeNull()
    expect(ref.getStatus()).toEqual({ status: 'idle', bytes: 0 })
    await expect(ref.getText()).resolves.toBe('')
  })

  it('applies an attach made before the view mounts, once, on mount', async () => {
    let ref!: StreamTextRef
    const { rerender } = await render(<Screen refOut={(r) => (ref = r)} mounted={false} />)
    const buffer = tsbBuffer('early')
    ref.attach(buffer)
    expect(mockCommands.attach).not.toHaveBeenCalled()
    await rerender(<Screen refOut={(r) => (ref = r)} />)
    expect(mockBuffers.put).toHaveBeenCalledWith(buffer)
    expect(mockCommands.attach).toHaveBeenCalledTimes(1)
    await expect(ref.getText()).resolves.toBe('early')
  })

  it('the last attach before mount wins', async () => {
    let ref!: StreamTextRef
    const { rerender } = await render(<Screen refOut={(r) => (ref = r)} mounted={false} />)
    ref.attach(tsbBuffer('first'))
    ref.attach(tsbBuffer('second'))
    await rerender(<Screen refOut={(r) => (ref = r)} />)
    expect(mockCommands.attach).toHaveBeenCalledTimes(1)
    await expect(ref.getText()).resolves.toBe('second')
  })

  it('drops the handle on unmount', async () => {
    let ref!: StreamTextRef
    const { rerender } = await render(<Screen refOut={(r) => (ref = r)} />)
    expect(ref.current).not.toBeNull()
    await rerender(<Screen refOut={(r) => (ref = r)} mounted={false} />)
    expect(ref.current).toBeNull()
  })

  it('also works as the ref of a component that forwards it', async () => {
    const Wrapper = React.forwardRef<StreamTextHandle>(function Wrapper(_, forwarded) {
      const inner = React.useRef<StreamTextHandle>(null)
      useImperativeHandle(forwarded, () => inner.current!, [])
      return <StreamText ref={inner} testID="stream" />
    })
    let ref!: StreamTextRef
    function Host() {
      ref = useStreamTextRef()
      return <Wrapper ref={ref} />
    }
    await render(<Host />)
    ref.attach(tsbBuffer('x'))
    expect(mockCommands.attach).toHaveBeenCalledTimes(1)
  })
})
