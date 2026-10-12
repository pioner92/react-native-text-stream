import React from 'react'
import { fireEvent, render, screen } from '@testing-library/react-native'
import { StreamText, type StreamTextHandle } from '../StreamText'
import { releaseStream, streamFetch } from '../fetch'
import { tsbBuffer } from './tsb'

// The native module is mocked in jest.setup.js.
const mockBuffers = require('react-native/Libraries/TurboModule/TurboModuleRegistry').buffers

const streams = new Map<string, { buffer: ArrayBuffer; key: string | undefined }>()
const serials = new WeakMap<ArrayBuffer, number>()
const watchers = new Map<number, { id: string; callback: (buffer: ArrayBuffer) => void }>()
// Native eviction keeps the request key; an explicit release forgets it.
const evicted = new Map<string, string | undefined>()
const evict = (id: string) => {
  evicted.set(id, streams.get(id)?.key)
  streams.delete(id)
}
let serial = 0
beforeEach(() => {
  jest.clearAllMocks()
  streams.clear()
  evicted.clear()
  watchers.clear()
  let id = 1
  mockBuffers.readBuffer.mockImplementation((buffer: ArrayBuffer) => {
    const header = new DataView(buffer)
    return {
      serial: serials.get(buffer) ?? 0,
      status: header.getUint32(24, true),
      bytes: header.getUint32(8, true),
    }
  })
  mockBuffers.fetch.mockImplementation((_url: string, init: { streamId?: string; requestKey?: string }) => {
    const buffer = tsbBuffer('abc')
    serials.set(buffer, ++serial)
    if (init.streamId) {
      streams.set(init.streamId, { buffer, key: init.requestKey })
      for (const watcher of watchers.values()) if (watcher.id === init.streamId) watcher.callback(buffer)
    }
    return { id: id++, buffer }
  })
  mockBuffers.hasStream.mockImplementation((id: string, key: string, includeEvicted: boolean) =>
    streams.has(id) ? streams.get(id)!.key === key : includeEvicted && evicted.get(id) === key
  )
  mockBuffers.streamBuffer.mockImplementation((id: string) => streams.get(id)?.buffer ?? null)
  mockBuffers.releaseStream.mockImplementation((id: string) => {
    evicted.delete(id)
    streams.delete(id)
  })
  let token = 0
  mockBuffers.watchStream.mockImplementation((id: string, callback: (buffer: ArrayBuffer) => void) => {
    watchers.set(++token, { id, callback })
    return { token, buffer: streams.get(id)?.buffer ?? null }
  })
  mockBuffers.unwatchStream.mockImplementation((token: number) => watchers.delete(token))
})

const host = () => screen.getByTestId('stream')
const fetchedStreamIds = () =>
  mockBuffers.fetch.mock.calls.map((call: unknown[]) => (call[1] as { streamId: string }).streamId)

describe('<StreamText request>', () => {
  it('treats an empty streamId as an owned request, consistently with streamFetch', async () => {
    const view = await render(<StreamText streamId="" request={{ url: 'https://x/empty-id' }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(1)
    expect(fetchedStreamIds()[0]).toMatch(/^stream-text-/)
    await view.unmount()
    expect(mockBuffers.cancel).toHaveBeenCalledWith(1)
  })
  it('sends the request under a streamId of its own, which the native view shows', async () => {
    await render(<StreamText testID="stream" request={{ url: 'https://x/a', method: 'POST', body: '{}' }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(1)
    const [url, init] = mockBuffers.fetch.mock.calls[0]
    expect(url).toBe('https://x/a')
    expect(init).toMatchObject({ method: 'POST', body: '{}' })
    expect(init.streamId).toMatch(/^stream-text-/)
    expect(host().props.streamId).toBe(init.streamId)
  })

  it('compares the request by value: an inline object sends it once', async () => {
    const view = await render(<StreamText testID="stream" request={{ url: 'https://x/a' }} />)
    await view.rerender(<StreamText testID="stream" request={{ url: 'https://x/a' }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(1)
    await view.rerender(<StreamText testID="stream" request={{ url: 'https://x/b' }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(2)
  })

  it('a view of its own aborts and forgets its stream on unmount', async () => {
    const view = await render(<StreamText testID="stream" request={{ url: 'https://x/a' }} />)
    const [streamId] = fetchedStreamIds()
    await view.unmount()
    expect(mockBuffers.cancel).toHaveBeenCalledWith(1)
    expect(mockBuffers.releaseStream).toHaveBeenCalledWith(streamId)
  })

  it("under the app's streamId the request outlives the view and is not sent again on a remount", async () => {
    const first = await render(<StreamText testID="stream" streamId="msg-1" request={{ url: 'https://x/a' }} />)
    expect(fetchedStreamIds()).toEqual(['msg-1'])
    await first.unmount()
    expect(mockBuffers.cancel).not.toHaveBeenCalled()
    await render(<StreamText testID="stream" streamId="msg-1" request={{ url: 'https://x/a' }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(1)
    // A new request under it (regenerate): sent.
    await render(<StreamText testID="stream" streamId="msg-1" request={{ url: 'https://x/a', body: 'again' }} />)
    expect(fetchedStreamIds()).toEqual(['msg-1', 'msg-1'])
  })
  it('remounts after explicit release or native eviction by starting a new request', async () => {
    for (const release of [() => releaseStream('released'), () => evict('released')]) {
      const first = await render(<StreamText streamId="released" request={{ url: 'https://x/released' }} />)
      await first.unmount()
      release()
      const count = mockBuffers.fetch.mock.calls.length
      const second = await render(<StreamText streamId="released" request={{ url: 'https://x/released' }} />)
      expect(mockBuffers.fetch).toHaveBeenCalledTimes(count + 1)
      await second.unmount()
    }
  })
  it('does not resend an evicted POST on a remount, only after an explicit release', async () => {
    const request = { url: 'https://x/chat', method: 'POST', body: '{"prompt":"hi"}' }
    const first = await render(<StreamText streamId="post" request={request} />)
    await first.unmount()
    evict('post')
    const second = await render(<StreamText streamId="post" request={{ ...request }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(1)
    expect(mockBuffers.hasStream).toHaveBeenLastCalledWith('post', expect.any(String), true)
    await second.unmount()
    releaseStream('post')
    await render(<StreamText streamId="post" request={{ ...request }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(2)
  })

  it('a synchronous start failure does not poison retries', async () => {
    mockBuffers.fetch.mockImplementationOnce(() => {
      throw new Error('allocation failed')
    })
    await expect(render(<StreamText streamId="retry" request={{ url: 'https://x/retry' }} />)).rejects.toThrow(
      'allocation failed'
    )
    await render(<StreamText streamId="retry" request={{ url: 'https://x/retry' }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(2)
  })

  it('detects direct streamFetch replacement instead of trusting old JS metadata', async () => {
    const first = await render(<StreamText streamId="replace" request={{ url: 'https://x/old' }} />)
    await first.unmount()
    streamFetch('https://x/new', { streamId: 'replace' })
    await render(<StreamText streamId="replace" request={{ url: 'https://x/old' }} />)
    expect(mockBuffers.fetch.mock.calls.map((call: unknown[]) => call[0])).toEqual([
      'https://x/old',
      'https://x/new',
      'https://x/old',
    ])
  })

  it('reacts to actual HeadersLike contents and a changed signal', async () => {
    const a = new AbortController(),
      b = new AbortController()
    const view = await render(
      <StreamText
        request={{
          url: 'https://x/change',
          headers: new Map([['Authorization', 'old']]),
          signal: a.signal,
        }}
      />
    )
    await view.rerender(
      <StreamText
        request={{
          url: 'https://x/change',
          headers: new Map([['Authorization', 'new']]),
          signal: a.signal,
        }}
      />
    )
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(2)
    await view.rerender(
      <StreamText
        request={{
          url: 'https://x/change',
          headers: new Map([['Authorization', 'new']]),
          signal: b.signal,
        }}
      />
    )
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(3)
    const before = mockBuffers.cancel.mock.calls.length
    b.abort()
    expect(mockBuffers.cancel).toHaveBeenCalledTimes(before + 1)
  })

  it('reads named streams through refs, retains released text, and ignores old serial events', async () => {
    const ref = React.createRef<StreamTextHandle>()
    const onStatusChange = jest.fn()
    const view = await render(
      <StreamText
        ref={ref}
        testID="stream"
        streamId="ref"
        request={{ url: 'https://x/a' }}
        onStatusChange={onStatusChange}
      />
    )
    const oldSerial = serials.get(streams.get('ref')!.buffer)
    await view.rerender(
      <StreamText
        ref={ref}
        testID="stream"
        streamId="ref"
        request={{ url: 'https://x/b' }}
        onStatusChange={onStatusChange}
      />
    )
    await fireEvent(host(), 'statusChange', {
      nativeEvent: {
        status: 'done',
        bytes: 3,
        generation: host().props.sourceGeneration,
        serial: oldSerial,
      },
    })
    expect(onStatusChange).not.toHaveBeenCalled()
    const currentSerial = serials.get(streams.get('ref')!.buffer)
    await fireEvent(host(), 'statusChange', {
      nativeEvent: {
        status: 'done',
        bytes: 3,
        generation: host().props.sourceGeneration,
        serial: currentSerial,
      },
    })
    expect(ref.current!.getStatus()).toEqual({ status: 'done', bytes: 3 })
    releaseStream('ref')
    await expect(ref.current!.getText()).resolves.toBe('abc')
    expect(ref.current!.getStatus()).toEqual({ status: 'done', bytes: 3 })
  })

  it('two declarative owners of one id share the request and unmount independently', async () => {
    const a = await render(<StreamText streamId="shared" request={{ url: 'https://x/shared' }} />)
    const b = await render(<StreamText streamId="shared" request={{ url: 'https://x/shared' }} />)
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(1)
    await a.unmount()
    expect(mockBuffers.cancel).not.toHaveBeenCalled()
    expect(streams.has('shared')).toBe(true)
    await b.unmount()
    expect(mockBuffers.cancel).not.toHaveBeenCalled()
    expect(watchers.size).toBe(0)
  })

  it('does not duplicate a named request during StrictMode effect replay', async () => {
    const view = await render(
      <React.StrictMode>
        <StreamText streamId="strict" request={{ url: 'https://x/strict' }} />
      </React.StrictMode>
    )
    expect(mockBuffers.fetch).toHaveBeenCalledTimes(1)
    await view.unmount()
    expect(watchers.size).toBe(0)
  })
})
