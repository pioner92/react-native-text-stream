import { applePlatform, appleSimulator } from '@react-native-harness/platform-apple';
import { androidPlatform, physicalAndroidDevice } from '@react-native-harness/platform-android';

// In-app tests (react-native-harness) for StreamText's native behaviour. They run in a Debug
// build of this app: on the dedicated simulator (ios) or the cabled Pixel 8a (android). Harness
// starts its own Metro on 8081 (and sets up adb reverse for it).
export default {
  entryPoint: './index.js',
  appRegistryComponentName: 'TextStreamExample',
  runners: [
    applePlatform({
      name: 'ios',
      device: appleSimulator('UltraBase64 17 Pro Max', '26.5'),
      bundleId: 'com.textstreamexample',
    }),
    androidPlatform({
      name: 'android',
      device: physicalAndroidDevice('Google', 'Pixel 8a'),
      bundleId: 'com.textstreamexample',
    }),
  ],
  defaultRunner: 'ios',
};
