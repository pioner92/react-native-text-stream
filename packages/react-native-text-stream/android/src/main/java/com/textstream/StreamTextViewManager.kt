package com.textstream

import com.facebook.react.bridge.ReadableArray
import com.facebook.react.module.annotations.ReactModule
import com.facebook.react.uimanager.ReactStylesDiffMap
import com.facebook.react.uimanager.StateWrapper
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.annotations.ReactProp
import com.facebook.react.views.view.ReactViewGroup
import com.facebook.react.views.view.ReactViewManager

/**
 * "StreamTextView" (cpp/StreamTextViewShadowNode.hpp). A ReactViewManager, so the view style
 * (background, border, radius) works as on a View. `source` and `sourceGeneration` come through
 * the state: props reach Java as folly::dynamic, which loses the buffer.
 */
@ReactModule(name = StreamTextViewManager.NAME)
class StreamTextViewManager : ReactViewManager() {
  override fun getName(): String = NAME

  override fun createViewInstance(context: ThemedReactContext): ReactViewGroup = StreamTextView(context)

  override fun updateState(view: ReactViewGroup, props: ReactStylesDiffMap, stateWrapper: StateWrapper): Any? {
    (view as StreamTextView).setState(stateWrapper)
    return null
  }

  override fun setPadding(view: ReactViewGroup, left: Int, top: Int, right: Int, bottom: Int) {
    view.setPadding(left, top, right, bottom)
  }

  override fun onAfterUpdateTransaction(view: ReactViewGroup) {
    super.onAfterUpdateTransaction(view)
    (view as StreamTextView).applyStyle()
  }

  override fun onDropViewInstance(view: ReactViewGroup) {
    (view as StreamTextView).dispose()
    super.onDropViewInstance(view)
  }

  override fun receiveCommand(root: ReactViewGroup, commandId: String, args: ReadableArray?) {
    val view = root as StreamTextView
    when (commandId) {
      "attach" -> view.attachCommand(args?.getInt(0) ?: 0, if (args != null && args.size() > 1) args.getInt(1) else 0)
      "clear" -> view.clearCommand(if (args != null && args.size() > 0) args.getInt(0) else 0)
      else -> super.receiveCommand(root, commandId, args)
    }
  }

  @ReactProp(name = "fontSize", defaultFloat = 15f)
  fun setFontSize(view: ReactViewGroup, value: Float) {
    (view as StreamTextView).fontSize = value
  }

  @ReactProp(name = "fontFamily")
  fun setFontFamily(view: ReactViewGroup, value: String?) {
    (view as StreamTextView).fontFamily = value?.ifEmpty { null }
  }

  @ReactProp(name = "fontWeight")
  fun setFontWeight(view: ReactViewGroup, value: String?) {
    (view as StreamTextView).fontWeight = value?.ifEmpty { null }
  }

  @ReactProp(name = "color", customType = "Color")
  fun setColor(view: ReactViewGroup, value: Int?) {
    (view as StreamTextView).textColor = value
  }

  @ReactProp(name = "lineHeight", defaultFloat = 0f)
  fun setLineHeight(view: ReactViewGroup, value: Float) {
    (view as StreamTextView).lineHeight = value
  }

  @ReactProp(name = "letterSpacing", defaultFloat = 0f)
  fun setLetterSpacing(view: ReactViewGroup, value: Float) {
    (view as StreamTextView).letterSpacing = value
  }

  @ReactProp(name = "textAlign")
  fun setTextAlign(view: ReactViewGroup, value: String?) {
    (view as StreamTextView).textAlign = value
  }

  companion object {
    const val NAME = "StreamTextView"
  }
}
