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
        // Benchmark and test helpers (FrameMonitor, StreamTextTesting): this app only.
        // STREAMTEXT_TESTING=0 builds without them, as an app using the library would.
        'text-stream-testing': {
            root: path.join(__dirname, 'modules/text-stream-testing'),
            platforms: process.env.STREAMTEXT_TESTING === '0' ? { ios: null, android: null } : {},
        },
    },
}
