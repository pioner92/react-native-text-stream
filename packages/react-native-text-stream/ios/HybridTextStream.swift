//
//  HybridTextStream.swift
//  Pods
//
//  Created by Alex Shumihin on 10/10/2026.
//

import Foundation
import NitroModules
import UIKit

/// Renders a TSB1 buffer (see TextStreamBuffer.h) as it fills. On each frame the main
/// thread appends the newly committed bytes to the text storage; no JS is involved.
///
/// Cost model:
/// - a frame without new bytes is two atomic loads; after `idleAfter` without data the
///   display link pauses and a coarse timer watches the header instead;
/// - a frame with new bytes appends at most `bytesPerTick`, adapted so append + layout
///   stays within `frameBudget`; a large burst is spread over frames instead of stalling one;
/// - bytes are decoded straight from the buffer; only a split UTF-8 sequence (≤ 3 bytes)
///   is copied aside.
class HybridTextStream: HybridTextStreamSpec {
  private static let sliceBytes = 16 * 1024
  /// Main-thread time per frame we allow for append + layout (a 60 Hz frame is ~16.7 ms).
  private static let frameBudget: CFTimeInterval = 0.006
  private static let minBytesPerTick = 4 * 1024
  private static let maxBytesPerTick = 1024 * 1024
  private static let idleAfter: CFTimeInterval = 0.25
  /// Short pauses between tokens poll often (≤ 33 ms extra latency); long ones (a model
  /// "thinking") back off to 10 Hz.
  private static let idlePollFast: DispatchTimeInterval = .milliseconds(33)
  private static let idlePollSlow: DispatchTimeInterval = .milliseconds(100)
  private static let idleSlowAfter: CFTimeInterval = 2

  private let container = TextStreamContainer()
  private let textView: StreamUITextView
  private let statsLabel = UILabel()

  var view: UIView { container }

  // Reader state, main thread only.
  private var buffer: ArrayBuffer?
  private var base: UnsafeMutableRawPointer?
  private var capacity: UInt64 = 0
  /// Bytes consumed from the buffer: shown, or held in `utf8Tail`.
  private var offset: UInt64 = 0
  /// Start of a UTF-8 sequence split across slices (at most 3 bytes).
  private var utf8Tail: [UInt8] = []
  private var finished = false
  private var finalStatus = UInt32(TSB_STATUS_OPEN)
  private var lastDataAt: CFTimeInterval = 0
  /// Layout cost dominates (TextKit lays out what it follows), so the budget is in bytes,
  /// re-estimated from the measured cost of each limited tick.
  private var bytesPerTick = 32 * 1024
  private var displayLink: CADisplayLink?
  private var idleTimer: DispatchSourceTimer?
  private var idlePollIsSlow = false
  private var statsKey: (UInt64, UInt32, Bool)?

  // Benchmark log: [wallClockMs, bytesShown] pairs, read from the JS thread.
  private let displayLogLock = NSLock()
  private var displayLog: [Double] = []
  private var unloggedShown: UInt64?

  private var textAttributes: [NSAttributedString.Key: Any] = [:]

  // Events: the last values sent to JS, so each change is reported once.
  private var reportedStatus: StreamTextStatus?
  private var reportedAtEnd = true
  private var reportedSize: CGSize = .zero
  private var lastSizeReportAt: CFTimeInterval = 0
  private var sizeReportScheduled = false
  private static let sizeReportInterval: CFTimeInterval = 0.1

  // getStatus() snapshot, read from the JS thread.
  private let stateLock = NSLock()
  private var stateSnapshot: (status: StreamTextStatus, bytes: UInt64, shown: UInt64) = (.idle, 0, 0)

  var source: Variant_ArrayBuffer_NullType? {
    // null arrives when React removes a previously set prop; treat it like undefined.
    didSet { attach(source?.asType(ArrayBuffer.self)) }
  }

  var fontSize: Double? { didSet { updateTextAttributes() } }
  var fontFamily: String? { didSet { updateTextAttributes() } }
  var fontWeight: String? { didSet { updateTextAttributes() } }
  var color: String? { didSet { updateTextAttributes() } }
  var lineHeight: Double? { didSet { updateTextAttributes() } }
  var letterSpacing: Double? { didSet { updateTextAttributes() } }
  var textAlign: StreamTextAlign? { didSet { updateTextAttributes() } }

  var contentInset: StreamTextInsets? {
    didSet {
      textView.textContainerInset = UIEdgeInsets(
        top: CGFloat(contentInset?.top ?? 0),
        left: CGFloat(contentInset?.left ?? 0),
        bottom: CGFloat(contentInset?.bottom ?? 0),
        right: CGFloat(contentInset?.right ?? 0)
      )
    }
  }

  var selectable: Bool? {
    didSet { textView.isSelectable = selectable ?? true }
  }

  var selectionColor: String? {
    // nil restores the inherited tint.
    didSet { textView.tintColor = UIColor(hex: selectionColor) }
  }

  var showsVerticalScrollIndicator: Bool? {
    didSet { textView.showsVerticalScrollIndicator = showsVerticalScrollIndicator ?? true }
  }

  var indicatorStyle: StreamTextIndicatorStyle? {
    didSet {
      switch indicatorStyle {
      case .black: textView.indicatorStyle = .black
      case .white: textView.indicatorStyle = .white
      default: textView.indicatorStyle = .default
      }
    }
  }

  var onStatusChange: ((_ status: StreamTextStatus, _ bytes: Double) -> Void)?
  var onAtEndChange: ((_ atEnd: Bool) -> Void)?
  var onContentSizeChange: ((_ width: Double, _ height: Double) -> Void)? {
    didSet { reportedSize = .zero }
  }

  var showStats: Bool? {
    didSet {
      statsLabel.isHidden = !(showStats ?? false)
      statsKey = nil
      updateStats(committed: offset)
    }
  }

  var recordDisplayLog: Bool?

  override init() {
    if #available(iOS 16.0, *) {
      // TextKit 2 lays out only the visible viewport, so appends stay cheap as the text grows.
      textView = StreamUITextView(usingTextLayoutManager: true)
    } else {
      textView = StreamUITextView()
    }
    super.init()

    textView.isEditable = false
    textView.isSelectable = true
    textView.alwaysBounceVertical = true
    textView.backgroundColor = .clear
    // Padding comes only from `contentInset`, like a plain <Text>.
    textView.textContainerInset = .zero
    textView.textContainer.lineFragmentPadding = 0
    textView.contentInsetAdjustmentBehavior = .never
    textView.frame = container.bounds
    textView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
    container.addSubview(textView)

    statsLabel.font = .monospacedDigitSystemFont(ofSize: 11, weight: .semibold)
    statsLabel.textAlignment = .center
    statsLabel.backgroundColor = UIColor(white: 1, alpha: 0.08)
    statsLabel.layer.cornerRadius = 11
    statsLabel.layer.borderWidth = 1
    statsLabel.layer.borderColor = UIColor(white: 1, alpha: 0.12).cgColor
    statsLabel.clipsToBounds = true
    statsLabel.isHidden = true
    statsLabel.translatesAutoresizingMaskIntoConstraints = false
    container.addSubview(statsLabel)
    NSLayoutConstraint.activate([
      statsLabel.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -10),
      statsLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -10),
      statsLabel.heightAnchor.constraint(equalToConstant: 22),
    ])

    updateTextAttributes()
    container.onWindowChange = { [weak self] inWindow in
      guard let self else { return }
      self.setTicking(inWindow && self.base != nil && !self.finished)
    }
    textView.delegate = scrollDelegate
    scrollDelegate.onScroll = { [weak self] in self?.reportAtEnd() }
    textView.onContentSizeChange = { [weak self] in
      self?.reportAtEnd()
      self?.reportContentSize()
    }
  }

  private let scrollDelegate = ScrollDelegate()

  deinit {
    displayLink?.invalidate()
    idleTimer?.cancel()
  }

  private func updateTextAttributes() {
    let font = makeFont()
    let paragraph = NSMutableParagraphStyle()
    switch textAlign {
    case .left: paragraph.alignment = .left
    case .right: paragraph.alignment = .right
    case .center: paragraph.alignment = .center
    case .justify: paragraph.alignment = .justified
    default: paragraph.alignment = .natural
    }
    var attributes: [NSAttributedString.Key: Any] = [
      .font: font,
      .foregroundColor: UIColor(hex: color) ?? .label,
      .paragraphStyle: paragraph,
    ]
    if let lineHeight, lineHeight > 0 {
      let height = CGFloat(lineHeight)
      paragraph.minimumLineHeight = height
      paragraph.maximumLineHeight = height
      // TextKit puts the extra height above the glyphs; centre them like RN <Text> does.
      attributes[.baselineOffset] = (height - font.lineHeight) / 2
    }
    if let letterSpacing {
      attributes[.kern] = CGFloat(letterSpacing)
    }
    textAttributes = attributes
    textView.typingAttributes = textAttributes
    let storage = textView.textStorage
    storage.setAttributes(textAttributes, range: NSRange(location: 0, length: storage.length))
  }

  private func makeFont() -> UIFont {
    let size = CGFloat(fontSize ?? 15)
    let weight = parseFontWeight(fontWeight)
    guard let family = fontFamily, !family.isEmpty else {
      return .systemFont(ofSize: size, weight: weight ?? .regular)
    }
    // A PostScript name ("Menlo-Bold") resolves directly; a family name goes through a descriptor.
    if weight == nil, let named = UIFont(name: family, size: size) { return named }
    var descriptor = UIFontDescriptor(fontAttributes: [.family: family])
    if let weight {
      descriptor = descriptor.addingAttributes([.traits: [UIFontDescriptor.TraitKey.weight: weight]])
    }
    return UIFont(descriptor: descriptor, size: size)
  }

  // MARK: - Source

  private func attach(_ newBuffer: ArrayBuffer?) {
    setTicking(false)
    buffer = nil
    base = nil
    capacity = 0
    offset = 0
    utf8Tail.removeAll()
    finished = false
    finalStatus = UInt32(TSB_STATUS_OPEN)
    textView.text = ""
    statsKey = nil
    updateStats(committed: 0)
    displayLogLock.withLock { displayLog.removeAll() }
    unloggedShown = nil

    guard let newBuffer, isValidSource(newBuffer) else {
      publishState(.idle, bytes: 0)
      return
    }

    buffer = newBuffer  // keeps the memory alive while we read it
    base = UnsafeMutableRawPointer(newBuffer.data)
    capacity = base!.assumingMemoryBound(to: TSBHeader.self).pointee.capacity
    lastDataAt = CACurrentMediaTime()
    publishState(.streaming, bytes: 0)
    setTicking(container.window != nil)
    tick()
  }

  private func isValidSource(_ buffer: ArrayBuffer) -> Bool {
    // A non-owning buffer was created by JS and may only be touched on the JS thread.
    guard buffer.isOwner else {
      NSLog("[TextStream] source is not a native-owned ArrayBuffer; ignoring it")
      return false
    }
    let size = buffer.size
    guard size >= Int(TSB_HEADER_SIZE) else {
      NSLog("[TextStream] source is too small for a TSB1 header (%d bytes)", size)
      return false
    }
    let header = UnsafeRawPointer(buffer.data).assumingMemoryBound(to: TSBHeader.self).pointee
    guard header.magic == TSB_MAGIC, Int(header.version) == Int(TSB_VERSION),
          header.capacity <= UInt64(size - Int(TSB_HEADER_SIZE)) else {
      NSLog("[TextStream] source is not a valid TSB1 buffer")
      return false
    }
    return true
  }

  // MARK: - State and events

  /// Updates the getStatus() snapshot and reports a status change once.
  private func publishState(_ status: StreamTextStatus, bytes: UInt64) {
    stateLock.withLock { stateSnapshot = (status, bytes, offset) }
    guard status != reportedStatus else { return }
    reportedStatus = status
    onStatusChange?(status, Double(bytes))
  }

  private func terminalStatus(_ status: UInt32) -> StreamTextStatus {
    switch status {
    case UInt32(TSB_STATUS_DONE): return .done
    case UInt32(TSB_STATUS_CANCELLED): return .cancelled
    case UInt32(TSB_STATUS_OVERFLOW): return .truncated
    default: return .error
    }
  }

  private func reportAtEnd() {
    let atEnd = distanceToEnd() <= Self.atEndThreshold
    guard atEnd != reportedAtEnd else { return }
    reportedAtEnd = atEnd
    onAtEndChange?(atEnd)
  }

  /// Reports the content size at most every `sizeReportInterval`, always ending on the latest size.
  private func reportContentSize() {
    guard onContentSizeChange != nil, !sizeReportScheduled else { return }
    let wait = lastSizeReportAt + Self.sizeReportInterval - CACurrentMediaTime()
    guard wait > 0 else { return sendContentSize() }
    sizeReportScheduled = true
    DispatchQueue.main.asyncAfter(deadline: .now() + wait) { [weak self] in
      self?.sizeReportScheduled = false
      self?.sendContentSize()
    }
  }

  private func sendContentSize() {
    let size = textView.contentSize
    guard size != reportedSize, let onContentSizeChange else { return }
    reportedSize = size
    lastSizeReportAt = CACurrentMediaTime()
    onContentSizeChange(Double(size.width), Double(size.height))
  }

  // MARK: - Frame loop

  private func setTicking(_ on: Bool) {
    stopIdlePolling()
    if on {
      if let link = displayLink {
        link.isPaused = false
        return
      }
      let link = CADisplayLink(target: DisplayLinkProxy(self), selector: #selector(DisplayLinkProxy.tick))
      // Text gains nothing from 120 Hz; 60 is plenty and halves the wakeups on ProMotion.
      link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
      link.add(to: .main, forMode: .common)
      displayLink = link
    } else {
      displayLink?.invalidate()
      displayLink = nil
    }
  }

  /// No data for a while: pause the display link and watch the header with a coarse timer.
  private func startIdlePolling() {
    guard idleTimer == nil, let link = displayLink else { return }
    link.isPaused = true
    let timer = DispatchSource.makeTimerSource(queue: .main)
    timer.setEventHandler { [weak self] in self?.idlePoll() }
    idleTimer = timer
    scheduleIdlePoll(slow: false)
    timer.resume()
  }

  private func scheduleIdlePoll(slow: Bool) {
    idlePollIsSlow = slow
    let interval = slow ? Self.idlePollSlow : Self.idlePollFast
    idleTimer?.schedule(deadline: .now() + interval, repeating: interval,
                        leeway: slow ? .milliseconds(20) : .milliseconds(5))
  }

  private func stopIdlePolling() {
    idleTimer?.cancel()
    idleTimer = nil
  }

  private func idlePoll() {
    guard let base else { return }
    if tsb_committed(base) > offset || tsb_status(base) != UInt32(TSB_STATUS_OPEN) {
      stopIdlePolling()
      displayLink?.isPaused = false
      tick()  // render now rather than a frame later
    } else if !idlePollIsSlow && CACurrentMediaTime() - lastDataAt > Self.idleSlowAfter {
      scheduleIdlePoll(slow: true)
    }
  }

  // Called on the JS thread; the view state lives on main.
  func attachSource(source: ArrayBuffer?) throws {
    if let source, !source.isOwner {
      // A JS-created buffer must not leave the JS thread.
      NSLog("[TextStream] attachSource: not a native-owned ArrayBuffer; ignoring it")
      return
    }
    DispatchQueue.main.async { [weak self] in self?.attach(source) }
  }

  func scrollToEnd(animated: Bool) throws {
    DispatchQueue.main.async { [weak self] in self?.scrollToTail(animated: animated) }
  }

  func getStatus() throws -> StreamTextState {
    let s = stateLock.withLock { stateSnapshot }
    return StreamTextState(status: s.status, bytes: Double(s.bytes), shownBytes: Double(s.shown))
  }

  func getText() throws -> Promise<String> {
    let promise = Promise<String>()
    DispatchQueue.main.async { [weak self] in
      promise.resolve(withResult: self?.textView.textStorage.string ?? "")
    }
    return promise
  }

  func getDisplayLog() throws -> [Double] {
    displayLogLock.withLock { displayLog }
  }

  fileprivate func tick() {
    guard let base else {
      setTicking(false)
      return
    }
    // Text appended last tick has been laid out and committed by now: log it as shown.
    if let bytes = unloggedShown {
      let now = Date().timeIntervalSince1970 * 1000
      displayLogLock.withLock { displayLog.append(contentsOf: [now, Double(bytes)]) }
      unloggedShown = nil
    }
    if finished {
      setTicking(false)
      return
    }

    // Status first: once it is terminal, the committed value read after it is final.
    let status = tsb_status(base)
    let committed = min(tsb_committed(base), capacity)
    let consumedBefore = offset
    let now = CACurrentMediaTime()

    if committed > offset {
      lastDataAt = now
      let follow = isAtTail()
      let storage = textView.textStorage
      let budget = UInt64(bytesPerTick)
      let end = min(committed, offset + budget)
      let started = CACurrentMediaTime()
      let appended = end - offset
      storage.beginEditing()
      repeat {
        let count = Int(min(end - offset, UInt64(Self.sliceBytes)))
        let start = base.advanced(by: Int(TSB_HEADER_SIZE) + Int(offset)).assumingMemoryBound(to: UInt8.self)
        appendUTF8(start, count)
        offset += UInt64(count)
      } while offset < end
      storage.endEditing()
      if follow { scrollToTail() }
      // Re-estimate only from ticks that hit the budget: small appends say little about cost per byte.
      if appended >= budget {
        let perByte = max(CACurrentMediaTime() - started, 1e-6) / Double(appended)
        bytesPerTick = min(max(Int(Self.frameBudget / perByte), Self.minBytesPerTick), Self.maxBytesPerTick)
      }
    }

    if status != UInt32(TSB_STATUS_OPEN) && offset >= committed {
      if !utf8Tail.isEmpty {
        // A truncated sequence at the very end: show it as U+FFFD rather than dropping it.
        appendString(String(decoding: utf8Tail, as: UTF8.self))
        utf8Tail.removeAll()
      }
      finished = true
      finalStatus = status
    }

    // Logs consumed bytes, a split character included, like the JS decoders' byte counts.
    if recordDisplayLog == true && offset > consumedBefore { unloggedShown = offset }
    updateStats(committed: committed)
    // Terminal statuses are reported only once all the text is on screen.
    publishState(finished ? terminalStatus(finalStatus) : .streaming, bytes: committed)

    // One more tick when finished, so the last append can still be logged.
    if !finished && offset >= committed && now - lastDataAt > Self.idleAfter {
      startIdlePolling()
    }
  }

  /// Decodes `count` bytes from the buffer and appends them, carrying a split sequence over.
  private func appendUTF8(_ start: UnsafePointer<UInt8>, _ count: Int) {
    var p = start
    var remaining = count
    if !utf8Tail.isEmpty {
      let needed = utf8SequenceLength(utf8Tail[0])
      let take = min(needed - utf8Tail.count, remaining)
      utf8Tail.append(contentsOf: UnsafeBufferPointer(start: p, count: take))
      p += take
      remaining -= take
      guard utf8Tail.count >= needed else { return }
      appendString(String(decoding: utf8Tail, as: UTF8.self))
      utf8Tail.removeAll()
    }
    guard remaining > 0 else { return }
    let complete = utf8CompletePrefixLength(p, remaining)
    if complete > 0 {
      // One copy into an NSString; invalid input falls back to U+FFFD replacement.
      let text = NSString(bytes: p, length: complete, encoding: String.Encoding.utf8.rawValue) as String?
        ?? String(decoding: UnsafeBufferPointer(start: p, count: complete), as: UTF8.self)
      appendString(text)
    }
    if complete < remaining {
      utf8Tail.append(contentsOf: UnsafeBufferPointer(start: p + complete, count: remaining - complete))
    }
  }

  private func appendString(_ text: String) {
    textView.textStorage.append(NSAttributedString(string: text, attributes: textAttributes))
  }

  private static let atEndThreshold: CGFloat = 40

  private func distanceToEnd() -> CGFloat {
    textView.contentSize.height - textView.bounds.height - textView.contentOffset.y
  }

  /// Follow the tail unless the user is scrolling or has scrolled away from the bottom.
  private func isAtTail() -> Bool {
    if textView.isTracking || textView.isDecelerating { return false }
    return distanceToEnd() <= Self.atEndThreshold
  }

  private func scrollToTail(animated: Bool = false) {
    textView.layoutIfNeeded()
    let maxY = textView.contentSize.height - textView.bounds.height + textView.adjustedContentInset.bottom
    textView.setContentOffset(CGPoint(x: 0, y: max(-textView.adjustedContentInset.top, maxY)), animated: animated)
  }

  private func updateStats(committed: UInt64) {
    guard !statsLabel.isHidden else { return }
    let status = finished ? finalStatus : UInt32(TSB_STATUS_OPEN)
    let key = (committed, status, base != nil)
    if let statsKey, statsKey == key { return }
    statsKey = key

    let (dot, label): (UIColor, String)
    switch status {
    case UInt32(TSB_STATUS_DONE): (dot, label) = (.systemGreen, "DONE")
    case UInt32(TSB_STATUS_ERROR): (dot, label) = (.systemRed, "ERROR")
    case UInt32(TSB_STATUS_CANCELLED): (dot, label) = (.systemGray, "CANCELLED")
    case UInt32(TSB_STATUS_OVERFLOW): (dot, label) = (.systemOrange, "OVERFLOW")
    default: (dot, label) = base == nil ? (.systemGray, "IDLE") : (.systemPink, "LIVE")
    }
    let text = NSMutableAttributedString(string: "   ● ", attributes: [.foregroundColor: dot])
    text.append(NSAttributedString(
      string: "\(label)  \(formatBytes(committed))   ",
      attributes: [.foregroundColor: UIColor(white: 1, alpha: 0.85)]
    ))
    statsLabel.attributedText = text
  }
}

/// Exposes only the end of the text to accessibility: an accessibility snapshot of a
/// UITextView holding megabytes of text otherwise stalls the main thread for seconds.
private final class StreamUITextView: UITextView {
  private static let accessibleTailCharacters = 2000

  var onContentSizeChange: (() -> Void)?

  override var contentSize: CGSize {
    didSet {
      if contentSize != oldValue { onContentSizeChange?() }
    }
  }

  override var isAccessibilityElement: Bool {
    get { true }
    set {}
  }

  override var accessibilityTraits: UIAccessibilityTraits {
    get { .staticText }
    set {}
  }

  override var accessibilityValue: String? {
    get {
      let text = textStorage.string as NSString
      let length = text.length
      guard length > Self.accessibleTailCharacters else { return text as String }
      let start = text.rangeOfComposedCharacterSequence(at: length - Self.accessibleTailCharacters).location
      return "…" + text.substring(from: start)
    }
    set {}
  }

  override var accessibilityElements: [Any]? {
    get { nil }
    set {}
  }
}

private func formatBytes(_ bytes: UInt64) -> String {
  bytes < 1024 ? "\(bytes) B" : String(format: "%.1f KB", Double(bytes) / 1024)
}

/// Total length of the UTF-8 sequence that starts with `lead` (1 for invalid leads).
private func utf8SequenceLength(_ lead: UInt8) -> Int {
  lead < 0xC0 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2
}

/// Length of the longest prefix that does not end inside a UTF-8 sequence.
private func utf8CompletePrefixLength(_ p: UnsafePointer<UInt8>, _ n: Int) -> Int {
  var i = n - 1
  while i >= 0 && n - i <= 4 {
    let b = p[i]
    if b & 0xC0 != 0x80 {
      return n - i >= utf8SequenceLength(b) ? n : i
    }
    i -= 1
  }
  return n  // invalid trailing bytes: let the decoder replace them
}

private extension UIColor {
  /// Parses "#RRGGBB" or "#RRGGBBAA".
  convenience init?(hex: String?) {
    guard var s = hex?.trimmingCharacters(in: .whitespaces), s.hasPrefix("#") else { return nil }
    s.removeFirst()
    guard s.count == 6 || s.count == 8, let v = UInt64(s, radix: 16) else { return nil }
    let rgba = s.count == 6 ? (v << 8) | 0xFF : v
    self.init(
      red: CGFloat((rgba >> 24) & 0xFF) / 255,
      green: CGFloat((rgba >> 16) & 0xFF) / 255,
      blue: CGFloat((rgba >> 8) & 0xFF) / 255,
      alpha: CGFloat(rgba & 0xFF) / 255
    )
  }
}

/// "normal", "bold" or "100"…"900", as in React Native.
private func parseFontWeight(_ value: String?) -> UIFont.Weight? {
  switch value {
  case "100": return .ultraLight
  case "200": return .thin
  case "300": return .light
  case "normal", "400": return .regular
  case "500": return .medium
  case "600": return .semibold
  case "bold", "700": return .bold
  case "800": return .heavy
  case "900": return .black
  default: return nil
  }
}

private final class ScrollDelegate: NSObject, UITextViewDelegate {
  var onScroll: (() -> Void)?
  func scrollViewDidScroll(_ scrollView: UIScrollView) { onScroll?() }
}

/// Breaks the CADisplayLink -> target retain cycle.
private final class DisplayLinkProxy: NSObject {
  private weak var target: HybridTextStream?
  init(_ target: HybridTextStream) { self.target = target }
  @objc func tick() { target?.tick() }
}

final class TextStreamContainer: UIView {
  var onWindowChange: ((Bool) -> Void)?
  override func didMoveToWindow() {
    super.didMoveToWindow()
    onWindowChange?(window != nil)
  }
}
