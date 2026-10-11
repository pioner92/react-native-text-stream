import React from 'react';
import { View } from 'react-native';
import { describe, expect, it, render, waitFor } from 'react-native-harness';
import { StreamText } from 'react-native-text-stream';

describe('harness', () => {
  it('runs in the app', () => {
    expect(1 + 1).toBe(2);
  });

  it('renders StreamText and gets its layout', async () => {
    let height = -1;
    await render(
      <View style={{ width: 200 }}>
        <StreamText onLayout={(e) => (height = e.nativeEvent.layout.height)} />
      </View>,
    );
    await waitFor(() => expect(height).toBe(0));
  });
});
