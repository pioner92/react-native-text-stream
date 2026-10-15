import { TurboModuleRegistry, type TurboModule } from 'react-native'

export interface NativeBufferSnapshot {
  bytes: number
  status: number
  serial: number
}

export interface NativeFetchInit {
  method: string
  headers: string[]
  body?: string
  capacity: number
  timeout: number
  streamId?: string
  requestKey?: string
  parser?: NativeStreamParser
}

export interface NativeStreamParser {
  format: 'sse' | 'ndjson'
  extractor: 'data' | 'json-pointer' | 'openai-chat' | 'openai-responses' | 'anthropic' | 'ollama'
  textPath?: string
  maxEventBytes: number
}

interface StreamTextBuffersModule extends TurboModule {
  put(buffer: ArrayBuffer): number
  fetch(
    url: string,
    init: NativeFetchInit,
    onEvent: (type: string, ...args: unknown[]) => void
  ): { id: number; buffer: ArrayBuffer }
  cancel(id: number): void
  cancelledBuffer(capacity: number): ArrayBuffer
  readBuffer(buffer: ArrayBuffer): NativeBufferSnapshot | null
  streamBuffer(streamId: string): ArrayBuffer | null
  /** `includeEvicted`: also true when that request's stream was evicted (not released). */
  hasStream(streamId: string, requestKey: string, includeEvicted: boolean): boolean
  watchStream(streamId: string, onBuffer: (buffer: ArrayBuffer) => void): { token: number; buffer: ArrayBuffer | null }
  unwatchStream(token: number): void
  abortStream(streamId: string): void
  releaseStream(streamId: string): void
}

export const Native = TurboModuleRegistry.get<StreamTextBuffersModule>('StreamTextBuffers')
