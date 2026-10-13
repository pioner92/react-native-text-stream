const { getDefaultConfig, mergeConfig } = require('@react-native/metro-config');
const { getBundleModeMetroConfig } = require('react-native-worklets/bundleMode');
const path = require('path');
// Workspace packages are symlinked from the root node_modules.
const root = path.resolve(__dirname, '..');

/**
 * Metro configuration
 * https://facebook.github.io/metro/docs/configuration
 *
 * @type {import('metro-config').MetroConfig}
 */
const config = {
  // The root covers node_modules/react-native-worklets/.worklets, where Bundle Mode writes worklets.
  watchFolders: [root],
};

// Worklets Bundle Mode (needed by react-native-streamdown).
module.exports = getBundleModeMetroConfig(mergeConfig(getDefaultConfig(__dirname), config));
