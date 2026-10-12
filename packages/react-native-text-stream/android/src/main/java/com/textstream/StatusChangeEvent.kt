package com.textstream

import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.WritableMap
import com.facebook.react.uimanager.events.Event

/** `onStatusChange`: `generation` tells the JS wrapper which source the event is about. */
internal class StatusChangeEvent(
    surfaceId: Int,
    viewId: Int,
    private val status: String,
    private val bytes: Double,
    private val generation: Int,
) : Event<StatusChangeEvent>(surfaceId, viewId) {
  override fun getEventName(): String = "topStatusChange"

  // Every status counts: "streaming" must not be merged into the final one.
  override fun canCoalesce(): Boolean = false

  override fun getEventData(): WritableMap =
      Arguments.createMap().apply {
        putString("status", status)
        putDouble("bytes", bytes)
        putInt("generation", generation)
      }
}
