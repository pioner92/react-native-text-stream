import { isTextStream, streamFetch } from '../fetch'
import { TSB_STATUS, tsbBuffer } from './tsb'

// The native module is mocked in jest.setup.js.
const mockBuffers = require('react-native/Libraries/TurboModule/TurboModuleRegistry').buffers

type OnEvent = (type: string, ...args: unknown[]) => void

/** The buffer the native module returns, and its event callback once `fetch` was called. */
function mockNative(content = '', status: number = TSB_STATUS.open) {
  const buffer = tsbBuffer(content, { status })
  let onEvent: OnEvent = () => {}
  mockBuffers.fetch.mockImplementation((_url: string, _init: unknown, callback: OnEvent) => {
    onEvent = callback
    return { id: 7, buffer }
  })
  return {
    buffer,
    emit: (type: string, ...args: unknown[]) => onEvent(type, ...args),
    setStatus: (s: number) => new DataView(buffer).setUint32(24, s, true),
  }
}

beforeEach(() => jest.clearAllMocks())

describe('streamFetch', () => {
  it('passes the request to the native module, headers flattened', () => {
    mockNative()
    streamFetch('https://x/y', {
      body: '{"a":1}',
      headers: { Authorization: 'Bearer t', 'Content-Type': 'application/json' },
      capacity: 1024,
      timeout: 5000,
    })
    expect(mockBuffers.fetch).toHaveBeenCalledWith(
      'https://x/y',
      {
        method: 'POST',
        headers: ['Authorization', 'Bearer t', 'Content-Type', 'application/json'],
        body: '{"a":1}',
        capacity: 1024,
        timeout: 5000,
      },
      expect.any(Function)
    )
  })

  it('defaults to GET, 16 MB and 5 minutes; takes header pairs', () => {
    mockNative()
    streamFetch('https://x', { headers: [['X-A', '1']] })
    expect(mockBuffers.fetch.mock.calls[0][1]).toEqual({
      method: 'GET',
      headers: ['X-A', '1'],
      body: undefined,
      capacity: 16 * 1024 * 1024,
      timeout: 300_000,
    })
  })

  it('rejects a capacity that is not a positive whole number', () => {
    mockNative()
    expect(() => streamFetch('https://x', { capacity: 0 })).toThrow(RangeError)
    expect(() => streamFetch('https://x', { capacity: 1.5 })).toThrow(RangeError)
  })

  it('resolves the response, then done with the status from the buffer', async () => {
    const native = mockNative('hello')
    const stream = streamFetch('https://x')
    native.emit('response', 201, { 'content-type': 'text/plain' }, 'https://x/final')
    await expect(stream.response).resolves.toEqual({
      status: 201,
      ok: true,
      headers: { 'content-type': 'text/plain' },
      url: 'https://x/final',
    })
    native.setStatus(TSB_STATUS.done)
    native.emit('complete', null)
    await expect(stream.done).resolves.toEqual({ status: 'done' })
    expect(stream.text()).toBe('hello')
    expect(stream.buffer).toBe(native.buffer)
  })

  it('reports an overflow as truncated', async () => {
    const native = mockNative('abc')
    const stream = streamFetch('https://x')
    native.emit('response', 200, {}, 'https://x')
    native.setStatus(TSB_STATUS.overflow)
    native.emit('complete', null)
    await expect(stream.done).resolves.toEqual({ status: 'truncated' })
  })

  it('a failure before the response rejects it and resolves done with the error', async () => {
    const native = mockNative()
    const stream = streamFetch('https://x')
    native.setStatus(TSB_STATUS.error)
    native.emit('complete', 'connection refused')
    await expect(stream.response).rejects.toThrow('connection refused')
    await expect(stream.done).resolves.toEqual({ status: 'error', error: 'connection refused' })
  })

  it('aborts through abort() and an AbortSignal, once, and not after the end', async () => {
    const native = mockNative()
    const controller = new AbortController()
    const stream = streamFetch('https://x', { signal: controller.signal })
    controller.abort()
    expect(mockBuffers.cancel).toHaveBeenCalledWith(7)
    native.setStatus(TSB_STATUS.cancelled)
    native.emit('complete', null)
    await expect(stream.done).resolves.toEqual({ status: 'cancelled' })
    stream.abort()
    expect(mockBuffers.cancel).toHaveBeenCalledTimes(1)
  })

  it('an already aborted signal cancels at once', () => {
    mockNative()
    const controller = new AbortController()
    controller.abort()
    streamFetch('https://x', { signal: controller.signal })
    expect(mockBuffers.cancel).toHaveBeenCalledWith(7)
  })

  it('tells a stream from a buffer', () => {
    const native = mockNative()
    expect(isTextStream(streamFetch('https://x'))).toBe(true)
    expect(isTextStream(native.buffer)).toBe(false)
    expect(isTextStream(null)).toBe(false)
  })
})
