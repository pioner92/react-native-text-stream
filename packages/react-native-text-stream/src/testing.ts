// Tests only, not part of the public API: import from 'react-native-text-stream/src/testing'.
import { NitroModules } from 'react-native-nitro-modules'
import type { StreamTextTesting } from './specs/testing.nitro'

export function createStreamTextTesting(): StreamTextTesting {
  return NitroModules.createHybridObject<StreamTextTesting>('StreamTextTesting')
}

export type { StreamTextSnapshot, StreamTextTesting } from './specs/testing.nitro'
export { bufferText, committedBytes, headerStatus } from './buffer'
