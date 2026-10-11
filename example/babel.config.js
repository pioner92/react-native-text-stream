const path = require('path');
const pak = require('../packages/react-native-text-stream/package.json');

module.exports = api => {
  api.cache(true);
  return {
    presets: ['module:@react-native/babel-preset'],
    plugins: [
      [
        'module-resolver',
        {
          extensions: ['.js', '.ts', '.json', '.jsx', '.tsx'],
          alias: {
            // Benchmark and test helpers of this app (FrameMonitor, StreamTextTesting).
            '^text-stream-testing$': path.join(__dirname, 'modules/text-stream-testing/src'),
            [pak.name]: path.join(__dirname, '../packages/react-native-text-stream', pak.source),
          },
        },
      ],
      // Must stay last. Bundle Mode + remend forwarding are required by react-native-streamdown.
      [
        'react-native-worklets/plugin',
        { bundleMode: true, importForwarding: { moduleNames: ['remend'] } },
      ],
    ],
  };
};