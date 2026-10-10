import React from 'react';
import { View, StyleSheet } from 'react-native';
import { TextStream } from 'react-native-text-stream';

function App(): React.JSX.Element {
  return (
    <View style={styles.container}>
        <TextStream isRed={true} style={styles.view} testID="text-stream" />
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
  },
  view: {
    width: 200,
    height: 200
  }});

export default App;