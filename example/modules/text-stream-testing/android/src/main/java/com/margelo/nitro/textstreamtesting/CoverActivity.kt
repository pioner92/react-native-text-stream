package com.margelo.nitro.textstreamtesting

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.view.View

/** Tests only: a blank screen over the app (StreamTextTesting.cover), as a pushed screen would be. */
class CoverActivity : Activity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(View(this).apply { setBackgroundColor(Color.WHITE) })
    current = this
  }

  override fun onDestroy() {
    if (current === this) current = null
    super.onDestroy()
  }

  companion object {
    var current: CoverActivity? = null
  }
}
