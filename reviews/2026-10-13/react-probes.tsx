import React from 'react'
import { render } from '@testing-library/react-native'
import { StreamText, type StreamTextHandle } from '../StreamText'
import { tsbBuffer } from './tsb'

const buffers = require('react-native/Libraries/TurboModule/TurboModuleRegistry').buffers
let publish: ((buffer: ArrayBuffer) => void) | undefined
beforeEach(() => {
  jest.resetAllMocks()
  publish = undefined
  buffers.watchStream.mockImplementation((_id: string, callback: typeof publish) => {
    publish = callback
    return { token: 1, buffer: null }
  })
  buffers.put.mockReturnValue(1)
  buffers.fetch.mockImplementation(() => ({ id: 1, buffer: tsbBuffer('network') }))
  buffers.cancelledBuffer.mockImplementation(() => tsbBuffer('', { status: 3 }))
})

it('review: late stream publication must not overwrite imperative attach', async () => {
  const ref = React.createRef<StreamTextHandle>()
  await render(<StreamText ref={ref} streamId="old" />)
  ref.current!.attach(tsbBuffer('manual'))
  await expect(ref.current!.getText()).resolves.toBe('manual')
  publish!(tsbBuffer('late publication'))
  await expect(ref.current!.getText()).resolves.toBe('manual')
})

it('review: late stream publication must not undo clear', async () => {
  const ref = React.createRef<StreamTextHandle>()
  await render(<StreamText ref={ref} streamId="old" />)
  ref.current!.clear()
  publish!(tsbBuffer('late publication'))
  expect(ref.current!.getStatus().status).toBe('idle')
  await expect(ref.current!.getText()).resolves.toBe('')
})

it('review: an owned request with pre-aborted signal exposes cancelled', async () => {
  const ref = React.createRef<StreamTextHandle>()
  const controller = new AbortController()
  controller.abort()
  await render(<StreamText ref={ref} request={{ url: 'https://example.test', signal: controller.signal }} />)
  expect(buffers.fetch).not.toHaveBeenCalled()
  expect(ref.current!.getStatus().status).toBe('cancelled')
})

it('review: owned POST should not start twice in StrictMode', async () => {
  await render(<React.StrictMode><StreamText request={{ url: 'https://example.test', method: 'POST', body: 'charge' }} /></React.StrictMode>)
  expect(buffers.fetch).toHaveBeenCalledTimes(1)
})
