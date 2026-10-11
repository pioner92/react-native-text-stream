const path = require('path')
const pkg = require('../packages/react-native-text-stream/package.json')

/**
 * @type {import('@react-native-community/cli-types').Config}
 */
module.exports = {
    project: {
        ios: {
            automaticPodsInstallation: true,
        },
    },
    dependencies: {
        [pkg.name]: {
            root: path.join(__dirname, '../packages/react-native-text-stream'),
        },
        // Benchmark and test helpers (FrameMonitor, StreamTextTesting): this app only, iOS only.
        'text-stream-testing': {
            root: path.join(__dirname, 'modules/text-stream-testing'),
            platforms: { android: null },
        },
    },
}
