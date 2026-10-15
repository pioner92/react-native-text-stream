import { normalizeRequest, requestKey, streamFetch, type StreamFetchParser } from '../fetch'

const mockBuffers = require('react-native/Libraries/TurboModule/TurboModuleRegistry').buffers

beforeEach(() => jest.clearAllMocks())

describe('native response parser contract', () => {
  it('keeps the plain-text default free of parser configuration', () => {
    expect(normalizeRequest({})).not.toHaveProperty('parser')
  })

  it.each(['openai-chat', 'openai-responses', 'anthropic', 'ollama'] as const)(
    'normalizes the %s protocol explicitly', provider => {
      expect(normalizeRequest({ parser: { provider } }).parser).toEqual({
        format: provider === 'ollama' ? 'ndjson' : 'sse', extractor: provider, maxEventBytes: 1024 * 1024,
      })
    }
  )

  it('supports raw SSE, root JSON strings, escaped keys and array elements', () => {
    expect(normalizeRequest({ parser: { format: 'sse' } }).parser?.extractor).toBe('data')
    for (const textPath of ['', '/delta/text', '/choices/0/delta/content', '/a~1b/~0']) {
      expect(normalizeRequest({ parser: { format: 'ndjson', textPath } }).parser).toEqual({
        format: 'ndjson', extractor: 'json-pointer', textPath, maxEventBytes: 1024 * 1024,
      })
    }
  })

  it.each([
    null, [], 'sse', {}, { provider: 'guess' }, { format: 'json' }, { format: 'ndjson' },
    { provider: 'ollama', format: 'sse' }, { provider: 'anthropic', textPath: '/text' },
    { format: 'sse', textPath: 0 }, { format: 'sse', textPath: 'delta.text' },
    { format: 'sse', textPath: '/~2' }, { format: 'sse', textPath: '/~' },
    { format: 'sse', textPath: '/a\0b' }, { format: 'sse', textPath: '/' + 'x'.repeat(1024) },
  ])('rejects invalid parser configuration before starting native work: %j', parser => {
    expect(() => streamFetch('https://example.com', { parser: parser as StreamFetchParser })).toThrow(TypeError)
    expect(mockBuffers.fetch).not.toHaveBeenCalled()
  })

  it.each([0, -1, 1.5, NaN, Infinity, 16 * 1024 * 1024 + 1])('rejects event bound %s', maxEventBytes => {
    expect(() => normalizeRequest({ parser: { format: 'sse', maxEventBytes } })).toThrow(RangeError)
  })

  it('includes parser semantics in request identity but ignores option property order', () => {
    const key = (parser?: StreamFetchParser) => requestKey('https://example.com', normalizeRequest({ parser }))
    expect(key({ format: 'sse', textPath: '/delta', maxEventBytes: 4096 })).toBe(
      key({ maxEventBytes: 4096, textPath: '/delta', format: 'sse' })
    )
    const variants: (StreamFetchParser | undefined)[] = [
      undefined, { format: 'sse' }, { format: 'sse', textPath: '' },
      { format: 'sse', textPath: '/text' }, { format: 'ndjson', textPath: '/text' },
      { provider: 'openai-chat' }, { provider: 'openai-responses' },
      { provider: 'anthropic' }, { provider: 'ollama' }, { format: 'sse', maxEventBytes: 7 },
    ]
    expect(new Set(variants.map(key)).size).toBe(variants.length)
  })

  it('snapshots parser configuration before caller mutation', () => {
    const parser: StreamFetchParser = { format: 'sse', textPath: '/first' }
    const normalized = normalizeRequest({ parser })
    parser.textPath = '/second'
    expect(normalized.parser?.textPath).toBe('/first')
  })
})
