import React from 'react'
import { render, screen } from '@testing-library/react-native'
import { StreamText } from '../StreamText'
import { tsbBuffer } from './tsb'

// The native module is mocked in jest.setup.js.
const mockBuffers = require('react-native/Libraries/TurboModule/TurboModuleRegistry').buffers

beforeEach(() => {
  jest.clearAllMocks()
  let id = 1
  mockBuffers.fetch.mockImplementation(() => ({ id: id++, buffer: tsbBuffer('') }))
})

const host = () => screen.getByTestId('stream')
const fetchedStreamIds = () => mockBuffers.fetch.mock.calls.map((call: unknown[]) => (call[1] as { streamId: string }).streamId)

describe('<StreamText request>', () => {
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

  it('under the app\'s streamId the request outlives the view and is not sent again on a remount', async () => {
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
})
