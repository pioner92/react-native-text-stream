package com.textstream

import com.facebook.react.ReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.uimanager.ViewManager

/** StreamTextBuffers is a C++ TurboModule (cpp/StreamTextBuffers.hpp), linked by autolinking. */
class StreamTextPackage : ReactPackage {
  override fun createNativeModules(reactContext: ReactApplicationContext): List<NativeModule> = emptyList()

  override fun createViewManagers(reactContext: ReactApplicationContext): List<ViewManager<*, *>> {
    StreamTextPreparedLayout.init(reactContext)
    return listOf(StreamTextViewManager())
  }
}
