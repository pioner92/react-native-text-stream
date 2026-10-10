import { getHostComponent, type HybridRef } from 'react-native-nitro-modules'
import TextStreamConfig from '../nitrogen/generated/shared/json/TextStreamConfig.json'
import type {
  TextStreamProps,
  TextStreamMethods,
} from './specs/text-stream.nitro'


export const TextStream = getHostComponent<TextStreamProps, TextStreamMethods>(
  'TextStream',
  () => TextStreamConfig
)

export type TextStreamRef = HybridRef<TextStreamProps, TextStreamMethods>
