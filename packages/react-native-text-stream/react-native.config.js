// Android autolinking: the shadow node and the StreamTextBuffers TurboModule are hand-written C++
// (cpp/), built by android/src/main/jni/CMakeLists.txt into the app.
module.exports = {
  dependency: {
    platforms: {
      android: {
        libraryName: 'textstream',
        componentDescriptors: ['StreamTextViewComponentDescriptor'],
        cmakeListsPath: 'src/main/jni/CMakeLists.txt',
        cxxModuleHeaderName: 'StreamTextBuffersModule',
      },
    },
  },
}
