package com.textstream

import android.annotation.SuppressLint
import android.os.Build
import android.os.SystemClock
import android.text.Layout
import android.text.Selection
import android.text.Spannable
import android.text.SpannableString
import android.text.TextPaint
import android.util.TypedValue
import android.view.ActionMode
import android.view.Gravity
import android.view.Menu
import android.view.MenuItem
import android.view.MotionEvent
import android.view.View
import android.widget.TextView

/**
 * Selection on Android: a long press lays a selectable TextView with the same text and line
 * metrics over the view, which brings the system's selection (handles, magnifier, the Copy and
 * Select all toolbar) without the view keeping a TextView around. Laid out once per long press, on
 * the main thread (a press already lasts half a second). It goes when nothing is selected any more;
 * until then the view draws no text itself (the same text drawn twice looks bolder).
 */
internal class StreamTextSelection(private val host: StreamTextView) {
  var overlay: TextView? = null
    private set

  /** Starts selecting the word at (`x`, `y`): the long press `down` began there. */
  fun start(down: MotionEvent, text: CharSequence, style: StreamTextStyle, paint: TextPaint, highlight: Int?) {
    dismiss()
    val view = makeOverlay(text, style, paint, highlight)
    overlay = view
    host.addView(view)
    layoutOverlay()
    host.invalidate()
    // The press began on the host: replay its down on the overlay, then the long press, which
    // selects the word there and shows the handles and the toolbar; the rest of the gesture follows.
    val replay = MotionEvent.obtain(down)
    view.dispatchTouchEvent(replay)
    replay.recycle()
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) view.performLongClick(down.x, down.y) else view.performLongClick()
  }

  /** Touches after the long press (dragging a handle out of it) go to the overlay. */
  fun forward(event: MotionEvent): Boolean {
    val view = overlay ?: return false
    return view.dispatchTouchEvent(event)
  }

  fun layoutOverlay() {
    val view = overlay ?: return
    val width = host.width
    val height = host.height
    view.measure(
        View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
    view.layout(0, 0, width, height)
  }

  /** More text streamed in while selecting. */
  fun append(chars: CharSequence) {
    overlay?.append(chars)
  }

  fun dismiss() {
    val view = overlay ?: return
    overlay = null
    view.clearFocus()
    host.removeView(view)
    host.invalidate()
  }

  /** Tests: selects the word at (`x`, `y`) as a long press would, and returns it. */
  fun selectWordAt(x: Float, y: Float, text: CharSequence, style: StreamTextStyle, paint: TextPaint, highlight: Int?): String {
    val now = SystemClock.uptimeMillis()
    val down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, x, y, 0)
    try {
      start(down, text, style, paint, highlight)
    } finally {
      down.recycle()
    }
    val view = overlay ?: return ""
    val start = view.selectionStart
    val end = view.selectionEnd
    return if (start in 0 until end) view.text.subSequence(start, end).toString() else ""
  }

  @SuppressLint("WrongConstant")
  private fun makeOverlay(text: CharSequence, style: StreamTextStyle, paint: TextPaint, highlight: Int?): TextView {
    val view = TextView(host.context)
    view.setPadding(host.paddingLeft, host.paddingTop, host.paddingRight, host.paddingBottom)
    view.background = null
    view.includeFontPadding = true
    view.setTextSize(TypedValue.COMPLEX_UNIT_PX, paint.textSize)
    view.typeface = paint.typeface
    view.letterSpacing = paint.letterSpacing
    view.setTextColor(paint.color)
    highlight?.let { view.highlightColor = it }
    view.breakStrategy = Layout.BREAK_STRATEGY_SIMPLE
    view.hyphenationFrequency = Layout.HYPHENATION_FREQUENCY_NONE
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) view.isFallbackLineSpacing = true
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && style.textAlign == "justify") {
      view.justificationMode = Layout.JUSTIFICATION_MODE_INTER_WORD
    }
    view.textDirection = View.TEXT_DIRECTION_FIRST_STRONG_LTR
    view.gravity = Gravity.TOP or when (style.textAlign) {
      "right" -> Gravity.END
      "center" -> Gravity.CENTER_HORIZONTAL
      else -> Gravity.START
    }
    view.setHorizontallyScrolling(false)
    val spannable: Spannable = SpannableString(text)
    style.applySegmentedLineHeight(spannable)
    view.setText(spannable, TextView.BufferType.SPANNABLE)
    view.setTextIsSelectable(true)
    // Gone once nothing is selected (a tap elsewhere in it, Copy, the back button).
    view.customSelectionActionModeCallback = object : ActionMode.Callback {
      override fun onCreateActionMode(mode: ActionMode, menu: Menu) = true

      override fun onPrepareActionMode(mode: ActionMode, menu: Menu) = false

      override fun onActionItemClicked(mode: ActionMode, item: MenuItem) = false

      override fun onDestroyActionMode(mode: ActionMode) {
        view.post {
          if (overlay === view && !view.hasSelection()) dismiss()
        }
      }
    }
    view.onFocusChangeListener = View.OnFocusChangeListener { _, focused ->
      if (!focused && overlay === view) {
        Selection.removeSelection(view.text as Spannable)
        dismiss()
      }
    }
    return view
  }
}
