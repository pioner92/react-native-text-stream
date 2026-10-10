import { processColor, type ColorValue, type ViewProps } from 'react-native';
import * as NativeComponentRegistry from 'react-native/Libraries/NativeComponent/NativeComponentRegistry';

// Prototype: the hand-written Fabric "StreamTextView", sized by its text (no own scrolling).
export type StreamTextViewProps = ViewProps & {
  source?: ArrayBuffer;
  fontSize?: number;
  color?: ColorValue;
};

export const StreamTextView = NativeComponentRegistry.get<StreamTextViewProps>('StreamTextView', () => ({
  uiViewClassName: 'StreamTextView',
  validAttributes: {
    // Two ArrayBuffers deep-compare as equal (no enumerable keys): compare by identity.
    source: { diff: (a: unknown, b: unknown) => a !== b },
    fontSize: true,
    color: { process: processColor },
  },
}));
