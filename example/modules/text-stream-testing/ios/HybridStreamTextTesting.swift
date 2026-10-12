//
//  HybridStreamTextTesting.swift
//  Pods
//

import Foundation
import NitroModules
@_spi(Testing) import TextStream
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

  func setPreparedLayout(enabled: Bool) throws {
    StreamTextPreparedLayout.enabled = enabled
  }

  func setPreparedCoreText(enabled: Bool) throws {
    StreamTextPreparedLayout.coreText = enabled
  }

  func resizeSurface(keys: [String], width: Double, ms: Double) throws -> Promise<String> {
    let promise = Promise<String>()
    DispatchQueue.main.async {
      let renderers = keys.compactMap { StreamTextRenderer.debugView($0) }
      // The root view a rotation or Split View resizes: it lays the surface out on the main thread.
      guard !keys.isEmpty, renderers.count == keys.count, let hostClass = NSClassFromString("RCTSurfaceHostingView"),
            let host = sequence(first: renderers[0] as UIView, next: { $0.superview }).first(where: { $0.isKind(of: hostClass) })
      else {
        promise.reject(withError: RuntimeError.error(withMessage: "No views \(keys) with recordDisplayLog in a surface"))
        return
      }
      var views = [String: [[String: Any]]]()
      func sample() {
        for (key, renderer) in zip(keys, renderers) {
          let entry: [String: Any] = ["w": renderer.bounds.width, "h": renderer.bounds.height, "mode": renderer.debugMode.mode]
          if let last = views[key]?.last, NSDictionary(dictionary: last).isEqual(to: entry) { continue }
          views[key, default: []].append(entry)
        }
      }
      StreamTextPreparedLayout.mainLayouts = 0
      StreamTextPreparedLayout.mainLongest = 0
      let started = CACurrentMediaTime()
      host.frame.size.width = width > 0 ? width : (host.superview?.bounds.width ?? host.frame.width)
      host.setNeedsLayout()
      host.layoutIfNeeded()
      let mainMs = (CACurrentMediaTime() - started) * 1000
      let counters: [String: Any] = ["mainMs": mainMs, "layouts": StreamTextPreparedLayout.mainLayouts,
                                     "longestMs": StreamTextPreparedLayout.mainLongest * 1000]
      // Then what each frame shows: right after Core Animation commits it (order 2000000).
      let until = started + ms / 1000
      var observer: CFRunLoopObserver?
      observer = CFRunLoopObserverCreateWithHandler(nil, CFRunLoopActivity.beforeWaiting.rawValue, true, 2_000_001) { _, _ in
        sample()
        guard CACurrentMediaTime() >= until else { return }
        CFRunLoopRemoveObserver(CFRunLoopGetMain(), observer, .commonModes)
        var result = counters
        result["views"] = views
        let data = (try? JSONSerialization.data(withJSONObject: result)) ?? Data()
        promise.resolve(withResult: String(decoding: data, as: UTF8.self))
      }
      CFRunLoopAddObserver(CFRunLoopGetMain(), observer, .commonModes)
      // Wakes the run loop each frame until the end.
      let timer = Timer(timeInterval: 1.0 / 60, repeats: true) { timer in
        if CACurrentMediaTime() >= until + 0.1 { timer.invalidate() }
      }
      RunLoop.main.add(timer, forMode: .common)
    }
    return promise
  }

  func tileMemory() throws -> Promise<String> {
    let promise = Promise<String>()
    DispatchQueue.main.async {
      let tiles = StreamTextRenderer.debugTiles
      var info = task_vm_info_data_t()
      var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
      _ = withUnsafeMutablePointer(to: &info) {
        $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count) }
      }
      let result: [String: Any] = ["bytes": tiles.bytes, "tiles": tiles.tiles, "canvases": tiles.canvases,
                                   "offWindow": tiles.offWindow, "footprintMB": Double(info.phys_footprint) / 1_048_576]
      let data = (try? JSONSerialization.data(withJSONObject: result)) ?? Data()
      promise.resolve(withResult: String(decoding: data, as: UTF8.self))
    }
    return promise
  }

  private static var coverController: UIViewController?

  func cover(shown: Bool) throws -> Promise<Double> {
    let promise = Promise<Double>()
    DispatchQueue.main.async {
      let root = UIApplication.shared.connectedScenes.compactMap { ($0 as? UIWindowScene)?.keyWindow }.first?.rootViewController
      let started = CACurrentMediaTime()
      // Main-thread time of the switch, with the layout it causes (canvases coming back draw then).
      let done = {
        root?.view.window?.layoutIfNeeded()
        promise.resolve(withResult: (CACurrentMediaTime() - started) * 1000)
      }
      if shown {
        let cover = UIViewController()
        cover.view.backgroundColor = .systemBackground
        cover.modalPresentationStyle = .fullScreen
        Self.coverController = cover
        root?.present(cover, animated: false, completion: done)
      } else {
        Self.coverController = nil
        root?.dismiss(animated: false, completion: done)
      }
    }
    return promise
  }

  func clearPreparedLayouts() throws {
    StreamTextPreparedLayout.generation += 1
    StreamTextPreparedLayout.cacheClear?()
  }

  func preparedCacheCount() throws -> Double {
    Double(StreamTextPreparedLayout.cacheCount?() ?? 0)
  }

  func preparedCacheDump() throws -> String {
    StreamTextPreparedLayout.cacheDump?() ?? ""
  }

  func simulateMemoryWarning() throws {
    DispatchQueue.main.async {
      NotificationCenter.default.post(name: UIApplication.didReceiveMemoryWarningNotification, object: nil)
    }
  }

  func compareEngines(text: String, style: StreamTextTestStyle, width: Double) throws -> Dictionary<String, Double> {
    let textStyle = StreamTextStyle()
    textStyle.fontSize = CGFloat(style.fontSize)
    textStyle.lineHeight = CGFloat(style.lineHeight)
    textStyle.fontFamily = style.fontFamily
    textStyle.fontWeight = style.fontWeight
    textStyle.letterSpacing = CGFloat(style.letterSpacing ?? 0)
    textStyle.textAlign = style.textAlign
    return StreamTextPreparedLayout.compareEngines(text: text, style: textStyle, width: CGFloat(width))
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

  func drainTextViewPool() throws {
    DispatchQueue.main.async { StreamTextRenderer.drainTextViewPool() }
  }

  func measureMount(text: String, width: Double) throws -> Promise<Dictionary<String, Double>> {
    let promise = Promise<Dictionary<String, Double>>()
    DispatchQueue.main.async {
      promise.resolve(withResult: StreamTextMountProbe.run(text: text, width: CGFloat(width)))
    }
    return promise
  }

  func selectWordAt(key: String, x: Double, y: Double) throws -> Promise<String> {
    let promise = Promise<String>()
    DispatchQueue.main.async {
      guard let renderer = StreamTextRenderer.debugView(key) else {
        promise.reject(withError: RuntimeError.error(withMessage: "No StreamTextView with nativeID \(key) and recordDisplayLog"))
        return
      }
      promise.resolve(withResult: renderer.debugSelectWord(at: CGPoint(x: x, y: y)))
    }
    return promise
  }

  func selectionGeometry(key: String, location: Double, length: Double) throws -> Promise<String> {
    let promise = Promise<String>()
    DispatchQueue.main.async {
      guard let renderer = StreamTextRenderer.debugView(key) else {
        promise.reject(withError: RuntimeError.error(withMessage: "No StreamTextView with nativeID \(key) and recordDisplayLog"))
        return
      }
      promise.resolve(withResult: renderer.debugSelectionGeometry(location: Int(location), length: Int(length)))
    }
    return promise
  }

  func accessibility(key: String, lines: [Double]) throws -> Promise<String> {
    let promise = Promise<String>()
    DispatchQueue.main.async {
      guard let renderer = StreamTextRenderer.debugView(key) else {
        promise.reject(withError: RuntimeError.error(withMessage: "No StreamTextView with nativeID \(key) and recordDisplayLog"))
        return
      }
      promise.resolve(withResult: renderer.debugAccessibility(lines: lines.map { Int($0) }))
    }
    return promise
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
        accessibilityValue: state.accessibilityValue,
        mode: renderer.debugMode.mode,
        engine: renderer.debugMode.engine,
        tiles: Double(renderer.debugMode.tiles)
      ))
    }
    return promise
  }
}
