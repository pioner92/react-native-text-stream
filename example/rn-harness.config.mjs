import { applePlatform, appleSimulator } from '@react-native-harness/platform-apple';

// In-app tests (react-native-harness) for StreamText's native behaviour. They run in a Debug
// build of this app on the dedicated simulator; Harness starts its own Metro on 8081.
export default {
  entryPoint: './index.js',
  appRegistryComponentName: 'TextStreamExample',
  runners: [
    applePlatform({
      name: 'ios',
      device: appleSimulator('UltraBase64 17 Pro Max', '26.5'),
      bundleId: 'com.textstreamexample',
    }),
  ],
  defaultRunner: 'ios',
};
