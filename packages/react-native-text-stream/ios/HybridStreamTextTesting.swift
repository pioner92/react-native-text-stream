//
//  HybridStreamTextTesting.swift
//  Pods
//

import Foundation
import NitroModules
import UIKit

/// Tests only: writes TSB1 buffers like the nitro-fetch producer, and reads StreamTextViews.
class HybridStreamTextTesting: HybridStreamTextTestingSpec {
  private static let producerQueue = DispatchQueue(label: "StreamText.testProducer")
  /// Buffers this object created, by data address: a buffer coming back from JS is looked up here,
  /// so writes always go to memory this object owns and keeps alive.
  private var buffers: [UnsafeMutableRawPointer: ArrayBuffer] = [:]
  private var producers: [DispatchSourceTimer] = []
  private let lock = NSLock()

  func createBuffer(capacity: Double) throws -> ArrayBuffer {
    let buffer = ArrayBuffer.allocate(size: Int(TSB_HEADER_SIZE) + Int(capacity), initializeToZero: true)
    tsb_init(buffer.data, UInt64(capacity))
    lock.withLock { buffers[UnsafeMutableRawPointer(buffer.data)] = buffer }
    return buffer
  }

  private func owned(_ buffer: ArrayBuffer) throws -> UnsafeMutableRawPointer {
    let base = UnsafeMutableRawPointer(buffer.data)
    guard lock.withLock({ buffers[base] != nil }) else {
      throw RuntimeError.error(withMessage: "Not a buffer from StreamTextTesting.createBuffer()")
    }
    return base
  }

  func append(buffer: ArrayBuffer, bytes: ArrayBuffer) throws {
    _ = tsb_append(try owned(buffer), bytes.data, UInt64(bytes.size))
  }

  func setStatus(buffer: ArrayBuffer, status: Double) throws {
    tsb_set_status(try owned(buffer), UInt32(status))
  }

  func startProducer(buffer: ArrayBuffer, bytes: ArrayBuffer, chunk: Double, intervalMs: Double, finalStatus: Double) throws {
    let base = try owned(buffer)
    let data = Data(bytes: bytes.data, count: bytes.size)  // JS may reuse its buffer
    let chunk = max(1, Int(chunk))
    var offset = 0
    let timer = DispatchSource.makeTimerSource(queue: Self.producerQueue)
    timer.schedule(deadline: .now(), repeating: .milliseconds(max(1, Int(intervalMs))))
    timer.setEventHandler { [weak timer] in
      let end = min(offset + chunk, data.count)
      if end > offset {
        data.withUnsafeBytes { raw in
          _ = tsb_append(base, raw.baseAddress!.advanced(by: offset), UInt64(end - offset))
        }
        offset = end
      }
      if offset >= data.count {
        if finalStatus > 0 { tsb_set_status(base, UInt32(finalStatus)) }
        timer?.cancel()
      }
    }
    lock.withLock { producers.append(timer) }
    timer.resume()
  }

  func stopProducers() throws {
    let stopped = lock.withLock { () -> [DispatchSourceTimer] in
      defer { producers.removeAll() }
      return producers
    }
    stopped.forEach { $0.cancel() }
  }

  func snapshot(key: String) throws -> Promise<StreamTextSnapshot> {
    let promise = Promise<StreamTextSnapshot>()
    DispatchQueue.main.async {
      guard let renderer = StreamTextRenderer.debugView(key) else {
        promise.reject(withError: RuntimeError.error(withMessage: "No StreamTextView with nativeID \(key) and recordDisplayLog"))
        return
      }
      let state = renderer.debugState()
      promise.resolve(withResult: StreamTextSnapshot(
        text: state.text,
        height: Double(state.height),
        status: state.status,
        hasTextView: state.hasTextView,
        fontSize: Double(state.fontSize),
        color: state.color,
        selectable: state.selectable,
        accessibilityValue: state.accessibilityValue
      ))
    }
    return promise
  }
}
