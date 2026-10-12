import { isTextStream, streamFetch, MAX_CAPACITY, MAX_TIMEOUT_MS, normalizeRequest, requestKey } from '../fetch'
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

beforeEach(() => {
  jest.clearAllMocks()
  mockBuffers.readBuffer.mockReturnValue(null)
  mockBuffers.cancelledBuffer.mockImplementation(() => tsbBuffer('', { status: TSB_STATUS.cancelled }))
})

describe('streamFetch', () => {
  it.each(['https://x/a\u0000ignored', 'https://x/a\r\nInjected', 'https://x/\u007f'])('rejects URL controls before native startup: %j', (url) => {
    expect(() => streamFetch(url)).toThrow('URL cannot contain control characters')
    expect(mockBuffers.fetch).not.toHaveBeenCalled()
  })

  it('passes the request to the native module, headers flattened', () => {
    mockNative()
    streamFetch('https://x/y', {
      body: '{"a":1}',
      headers: {
        'Authorization': 'Bearer t',
        'Content-Type': 'application/json',
      },
      capacity: 1024,
      timeout: 5000,
    })
    expect(mockBuffers.fetch).toHaveBeenCalledWith(
      'https://x/y',
      {
        method: 'POST',
        headers: ['authorization', 'Bearer t', 'content-type', 'application/json'],
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
      headers: ['x-a', '1'],
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
    await expect(stream.done).resolves.toEqual({
      status: 'error',
      error: 'connection refused',
    })
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

  it('an already aborted signal never starts or replaces a native request', async () => {
    mockNative()
    const controller = new AbortController()
    controller.abort()
    const stream = streamFetch('https://x', {
      signal: controller.signal,
      streamId: 'keep-existing',
      body: 'side effect',
    })
    expect(mockBuffers.fetch).not.toHaveBeenCalled()
    expect(mockBuffers.cancel).not.toHaveBeenCalled()
    await expect(stream.done).resolves.toEqual({ status: 'cancelled' })
    await expect(stream.response).rejects.toHaveProperty('name', 'AbortError')
    expect(stream.text()).toBe('')
  })

  it('tells a stream from a buffer', () => {
    const native = mockNative()
    expect(isTextStream(streamFetch('https://x'))).toBe(true)
    expect(isTextStream(native.buffer)).toBe(false)
    expect(isTextStream(null)).toBe(false)
  })

  it.each([0, -1, 1.5, NaN, Infinity, MAX_CAPACITY + 1])(
    'rejects capacity %s before native side effects',
    (capacity) => {
      expect(() => streamFetch('https://x', { capacity })).toThrow(RangeError)
      expect(mockBuffers.fetch).not.toHaveBeenCalled()
    }
  )

  it.each([0, -1, 0.5, NaN, Infinity, MAX_TIMEOUT_MS + 1])(
    'rejects timeout %s before native side effects',
    (timeout) => {
      expect(() => streamFetch('https://x', { timeout })).toThrow(RangeError)
      expect(mockBuffers.fetch).not.toHaveBeenCalled()
    }
  )

  it('removes the original signal listener even when the caller mutates init', async () => {
    const native = mockNative()
    const controller = new AbortController()
    const remove = jest.spyOn(controller.signal, 'removeEventListener')
    const init = { signal: controller.signal }
    const stream = streamFetch('https://x', init)
    init.signal = new AbortController().signal
    native.setStatus(TSB_STATUS.done)
    native.emit('complete', null)
    await stream.done
    expect(remove).toHaveBeenCalledWith('abort', expect.any(Function))
  })

  it('cancels only once even before the native completion is delivered', () => {
    mockNative()
    const stream = streamFetch('https://x')
    stream.abort()
    stream.abort()
    expect(mockBuffers.cancel).toHaveBeenCalledTimes(1)
  })

  it('normalizes equivalent headers and keeps distinct HeadersLike contents distinct', () => {
    const key = (headers: Parameters<typeof normalizeRequest>[0]['headers']) =>
      requestKey('https://x', normalizeRequest({ headers }))
    expect(key({ B: '2', A: '1' })).toBe(
      key([
        ['a', '1'],
        ['b', '2'],
      ])
    )
    expect(key(new Map([['A', '1']]))).not.toBe(key(new Map([['A', '2']])))
  })

  it('preserves supported Accept-Encoding and rejects unsupported encodings explicitly', () => {
    expect(normalizeRequest({ headers: { 'Accept-Encoding': 'identity' } }).headers).toEqual([
      'accept-encoding',
      'identity',
    ])
    expect(normalizeRequest({ headers: { 'Accept-Encoding': 'gzip' } }).headers).toEqual(['accept-encoding', 'gzip'])
    expect(() => normalizeRequest({ headers: { 'Accept-Encoding': 'br' } })).toThrow(TypeError)
  })

  it('rejects invalid methods, headers and GET/HEAD bodies consistently before starting', () => {
    for (const init of [{ method: 'bad method' }, { method: 'HEAD', body: '' }, { headers: { 'X-A': 'a\r\nb' } }]) {
      expect(() => streamFetch('https://x', init)).toThrow(TypeError)
    }
    expect(mockBuffers.fetch).not.toHaveBeenCalled()
  })
})
