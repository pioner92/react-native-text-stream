/** Where a stream is, as seen by the view: terminal states arrive once all of the text is shown. */
export type StreamTextStatus =
  | 'idle'
  | 'streaming'
  | 'done'
  | 'error'
  | 'cancelled'
  /** The buffer filled up; the rest of the response was dropped. */
  | 'truncated'

export type StreamTextAlign = 'auto' | 'left' | 'right' | 'center' | 'justify'

export type StreamTextFontWeight =
  | 'normal'
  | 'bold'
  | '100'
  | '200'
  | '300'
  | '400'
  | '500'
  | '600'
  | '700'
  | '800'
  | '900'

export interface StreamTextState {
  status: StreamTextStatus
  /** Bytes the producer has committed to the buffer so far. */
  bytes: number
}
