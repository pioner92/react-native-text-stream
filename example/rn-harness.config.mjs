import {
  applePlatform,
  appleSimulator,
} from '@react-native-harness/platform-apple';
import {
  androidPlatform,
  androidEmulator,
  physicalAndroidDevice,
} from '@react-native-harness/platform-android';

// In-app tests (react-native-harness) for StreamText's native behaviour. They run in a Debug
// build of this app: on the dedicated simulator (ios) or the cabled Pixel 8a (android). Harness
// starts its own Metro on 8081 (and sets up adb reverse for it).
export default {
  entryPoint: './index.js',
  appRegistryComponentName: 'TextStreamExample',
  runners: [
    applePlatform({
      name: 'ios',
      device: appleSimulator(
        process.env.HARNESS_IOS_DEVICE ?? 'UltraBase64 17 Pro Max',
        process.env.HARNESS_IOS_VERSION ?? '26.5',
      ),
      bundleId: 'com.textstreamexample',
    }),
    androidPlatform({
      name: 'android',
      device: process.env.HARNESS_ANDROID_AVD
        ? androidEmulator(process.env.HARNESS_ANDROID_AVD)
        : physicalAndroidDevice('Google', 'Pixel 8a'),
      bundleId: 'com.textstreamexample',
    }),
  ],
  defaultRunner: 'ios',
};
