import type {
  HybridView,
  HybridViewProps,
  HybridViewMethods,
} from 'react-native-nitro-modules'

export interface TextStreamProps extends HybridViewProps {
   isRed: boolean
}

export interface TextStreamMethods extends HybridViewMethods {}

export type TextStream = HybridView<TextStreamProps, TextStreamMethods, { ios: 'swift' }>