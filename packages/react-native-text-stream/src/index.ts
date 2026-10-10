import { getHostComponent, type HybridRef } from 'react-native-nitro-modules'
import TextStreamConfig from '../nitrogen/generated/shared/json/TextStreamConfig.json'
import type {
  TextStreamProps,
  TextStreamMethods,
} from './specs/text-stream.nitro'

export const StreamText = getHostComponent<TextStreamProps, TextStreamMethods>(
  'TextStream',
  () => TextStreamConfig
)

export type StreamTextProps = TextStreamProps
export type StreamTextRef = HybridRef<TextStreamProps, TextStreamMethods>
