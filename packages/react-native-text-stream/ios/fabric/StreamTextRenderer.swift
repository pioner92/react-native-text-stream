import UIKit

/// Renders a TSB1 buffer (see TextStreamBuffer.h) as it fills and sizes itself to the text:
/// after each append it reports the laid-out height (only when it changes, i.e. per line), which
/// the Fabric component view feeds into its shadow node's state. No JS is involved per chunk.
///
/// Cost model:
/// - a background watcher reads the header (two atomic loads) every few milliseconds and wakes the
///   main thread as soon as bytes or a final status land, so new text shows on the next frame
///   rather than after the next display-link tick; without new bytes the main thread sleeps;
/// - a wake appends at most `bytesPerTick`, adapted so the work stays within `frameBudget`; a
///   burst is spread over frames by the display link, which runs only while there is a backlog;
/// - TextKit 1 with contiguous layout: appending lays out only the tail, and the used rect gives
///   the height without laying out the whole text again.
@objc(StreamTextRenderer)
public final class StreamTextRenderer: UIView {
  private static let sliceBytes = 16 * 1024
  /// Main-thread time per frame we allow for append + layout (a 60 Hz frame is ~16.7 ms).
  private static let frameBudget: CFTimeInterval = 0.006
  private static let minBytesPerTick = 4 * 1024
  private static let maxBytesPerTick = 256 * 1024

  /// Created on the first text: an empty view (e.g. mounted before its first token) is just a
  /// plain UIView, which is much cheaper to create and keep than a text view.
  private var textView: StreamTextView?
  /// A finished text laid out ahead (StreamTextPreparedLayout), drawn without a text view until
  /// something needs one: a long press to select, a new width or a new style.
  private var canvas: StreamTextCanvas?
  /// A style prop that changes lines arrived for the canvas; the next `updatePrepared` settles it.
  private var canvasStyleStale = false

  // Reader state, main thread only.
  private var base: UnsafeMutableRawPointer?
  private var capacity: UInt64 = 0
  /// Bytes consumed from the buffer: shown, or the last `heldUTF8` of them, an unfinished sequence.
  private var offset: UInt64 = 0
  private var heldUTF8 = 0
  private var finished = false
  /// The attached buffer was finished and small: show it in one go rather than frame by frame.
  private var instantFill = false
  private var bytesPerTick = 32 * 1024
  private var displayLink: CADisplayLink?
  private lazy var watcher = BufferWatcher { [weak self] in self?.watcherWoke() }

  private var textAttributes: [NSAttributedString.Key: Any] = [:]
  private var layoutWidth: CGFloat = -1
  /// Starts at the shadow node state's initial height.
  private var reportedHeight: CGFloat = 0
  private var reportedStatus: String?
  private var unloggedShown: UInt64?
  /// Benchmarks only (with `displayLogKey`): main-thread cost per phase.
  private var stats = StreamTextRenderStats.Sample()

  /// Called on the main thread with the text's height at the current width.
  @objc public var onHeightChange: ((CGFloat) -> Void)?
  /// Called on the main thread: "streaming" when a stream starts, and its final status
  /// ("done", "error", "cancelled", "truncated") once all of its text is on screen.
  @objc public var onStatusChange: ((String, Double) -> Void)?
  /// Benchmarks only: when set, records when bytes reach the screen under this key.
  @objc public var displayLogKey: String? {
    didSet {
      if let oldValue, Self.debugViews.object(forKey: oldValue as NSString) === self {
        Self.debugViews.removeObject(forKey: oldValue as NSString)
      }
      if let displayLogKey { Self.debugViews.setObject(self, forKey: displayLogKey as NSString) }
    }
  }

  /// Benchmarks and tests: renderers that have a displayLogKey, by key. Main thread.
  private static let debugViews = NSMapTable<NSString, StreamTextRenderer>.strongToWeakObjects()

  @_spi(Testing) public static func debugView(_ key: String) -> StreamTextRenderer? {
    debugViews.object(forKey: key as NSString)
  }

  /// What the view shows, for tests. Main thread.
  @_spi(Testing) public func debugState() -> (text: String, height: CGFloat, status: String, hasTextView: Bool, fontSize: CGFloat,
                        color: String, selectable: Bool, accessibilityValue: String) {
    let storage = textView?.textStorage
      ?? canvas.map { NSTextStorage(attributedString: NSAttributedString(string: $0.layout.text.string, attributes: textAttributes)) }
    let attributes = storage.flatMap { $0.length > 0 ? $0.attributes(at: 0, effectiveRange: nil) : nil }
      ?? textView?.typingAttributes ?? textAttributes
    let font = attributes[.font] as? UIFont
    var (r, g, b, a): (CGFloat, CGFloat, CGFloat, CGFloat) = (0, 0, 0, 0)
    (attributes[.foregroundColor] as? UIColor)?.resolvedColor(with: traitCollection).getRed(&r, green: &g, blue: &b, alpha: &a)
    let color = "#" + [r, g, b, a].map { String(format: "%02X", Int(($0 * 255).rounded())) }.joined()
    return (storage?.string ?? "", reportedHeight, reportedStatus ?? "", textView != nil, font?.pointSize ?? 0,
            color, textView?.isSelectable ?? selectable,
            textView?.accessibilityValue ?? canvas?.accessibilityValue ?? "")
  }

  /// The component view sets it, then calls `applyTextStyle()` once.
  @objc public var textStyle = StreamTextStyle()

  @objc public var selectable: Bool = true {
    didSet {
      textView?.isSelectable = selectable
      canvas?.isSelectable = selectable
    }
  }

  @objc public var selectionColor: UIColor? {
    // nil restores the inherited tint.
    didSet {
      textView?.tintColor = selectionColor
      canvas?.tintColor = selectionColor
    }
  }

  public override init(frame: CGRect) {
    let started = CACurrentMediaTime()
    defer {
      let time = CACurrentMediaTime() - started
      StreamTextRenderStats.shared.addTotals { $0.inits += 1; $0.initTime += time }
    }
    super.init(frame: frame)
    clipsToBounds = true
    applyTextStyle()
  }

  /// The text view, created on first use.
  private func ensureTextView() -> StreamTextView {
    if let textView { return textView }
    let created = CACurrentMediaTime()
    let pooled = Self.textViewPool.popLast()
    let textView = pooled ?? StreamTextView()
    defer {
      StreamTextRenderStats.shared.addPhase(pooled == nil ? "textView.create" : "textView.reuse",
                                            time: CACurrentMediaTime() - created)
    }
    textView.isEditable = false
    textView.isSelectable = selectable
    textView.isScrollEnabled = false
    // No indicators: they'd be extra subviews per bubble, and VoiceOver would list each as a scroll bar.
    textView.showsVerticalScrollIndicator = false
    textView.showsHorizontalScrollIndicator = false
    textView.backgroundColor = .clear
    textView.tintColor = selectionColor
    textView.textContainerInset = .zero
    textView.textContainer.lineFragmentPadding = 0
    // Contiguous layout: appending to the end lays out only the new tail.
    textView.layoutManager.allowsNonContiguousLayout = false
    textView.typingAttributes = textAttributes
    textView.frame = CGRect(x: 0, y: 0, width: bounds.width, height: StreamTextLayout.textViewHeight)
    textView.visibleBounds = { [weak self] in self?.bounds ?? .zero }
    addSubview(textView)
    self.textView = textView
    if let canvas {
      // Laid out again on the main thread, the next time it is measured: same engine, same lines.
      self.canvas = nil
      canvas.removeFromSuperview()
      textView.textStorage.setAttributedString(NSAttributedString(string: canvas.layout.text.string, attributes: textAttributes))
    }
    return textView
  }

  /// Shows a prepared layout of the finished text the buffer holds, instead of filling a text view.
  private func showPrepared(_ prepared: StreamTextPreparedLayout, status: UInt32) {
    if let textView {
      Self.recycle(textView)
      self.textView = nil
    }
    let canvas = StreamTextCanvas(layout: prepared, color: textColor)
    canvas.visibleBounds = { [weak self] in self?.bounds ?? .zero }
    canvas.isSelectable = selectable
    canvas.tintColor = selectionColor
    addSubview(canvas)
    self.canvas = canvas
    offset = prepared.bytes
    finished = true
    base = nil
    // The shadow node has this height already: no commit.
    reportedHeight = prepared.height
    if let displayLogKey { StreamTextDisplayLogs.shared.append(displayLogKey, bytes: offset) }
    reportStatus(terminalStatus(status), bytes: prepared.bytes)
  }

  /// The layout the shadow node prepared for the current props, after a transaction. Replaces the
  /// canvas's layout when the width or style changed; without one, the text view takes over.
  @objc public func updatePrepared(_ prepared: StreamTextPreparedLayout?) {
    defer { canvasStyleStale = false }
    guard let canvas else { return }
    if let prepared, prepared !== canvas.layout, prepared.bytes == canvas.layout.bytes {
      canvas.replace(prepared)
      canvas.color = textColor
      // The shadow node has this height already.
      reportedHeight = prepared.height
      return
    }
    if canvasStyleStale || abs(canvas.layout.width - bounds.width) > 0.5 {
      _ = ensureTextView()
      setTextViewHeight(StreamTextLayout.textViewHeight)
      reportHeight()
    }
  }

  /// An ancestor scroll view moved: tiles near the screen are drawn, far ones dropped.
  @objc public func visibleAreaChanged() {
    canvas?.updateTiles()
  }

  /// Tests: selects the word at `point` as the selection gestures would, copies and returns it.
  @_spi(Testing) public func debugSelectWord(at point: CGPoint) -> String {
    let input: UITextInput & UIResponder
    if let canvas { input = canvas } else if let textView { input = textView } else { return "" }
    guard let position = input.closestPosition(to: point),
          let word = input.tokenizer.rangeEnclosingPosition(position, with: .word, inDirection: .storage(.forward))
    else { return "" }
    input.selectedTextRange = word
    _ = input.becomeFirstResponder()
    input.copy(nil)
    return input.text(in: word) ?? ""
  }

  /// Tests: the selection geometry UIKit's selection UI reads for a range (rects with their direction
  /// and ends, the carets at both ends), from the canvas or the text view, as JSON.
  @_spi(Testing) public func debugSelectionGeometry(location: Int, length: Int) -> String {
    let input: UITextInput
    if let canvas { input = canvas } else if let textView { input = textView } else { return "{}" }
    guard let start = input.position(from: input.beginningOfDocument, offset: location),
          let end = input.position(from: start, offset: length),
          let range = input.textRange(from: start, to: end) else { return "{}" }
    func caret(_ position: UITextPosition) -> [String: CGFloat] {
      let rect = input.caretRect(for: position)
      return ["x": rect.minX, "y": rect.minY, "h": rect.height]
    }
    func back(_ position: UITextPosition) -> Int {
      let rect = input.caretRect(for: position)
      guard let found = input.closestPosition(to: CGPoint(x: rect.minX, y: rect.midY)) else { return -1 }
      return input.offset(from: input.beginningOfDocument, to: found)
    }
    let rects = input.selectionRects(for: range).filter { !$0.rect.isEmpty }.map { rect -> [String: Any] in
      ["x": rect.rect.minX, "y": rect.rect.minY, "w": rect.rect.width, "h": rect.rect.height,
       "rtl": rect.writingDirection == .rightToLeft, "start": rect.containsStart, "end": rect.containsEnd]
    }
    let geometry: [String: Any] = [
      "rects": rects, "startCaret": caret(start), "endCaret": caret(end),
      "rtl": input.baseWritingDirection(for: start, in: .forward) == .rightToLeft,
      // The selection UI places a caret, then asks for the position at it: it must come back.
      "startBack": back(start), "endBack": back(end),
      "selected": input.selectedTextRange.map { input.offset(from: $0.start, to: $0.end) } ?? -1,
    ]
    guard let data = try? JSONSerialization.data(withJSONObject: geometry) else { return "{}" }
    return String(decoding: data, as: UTF8.self)
  }

  /// Tests: "canvas", "textView" or "" (nothing shown yet), and the canvas's engine.
  @_spi(Testing) public var debugMode: (mode: String, engine: String, tiles: Int) {
    if let canvas { return ("canvas", canvas.layout.isCoreText ? "coreText" : "textKit", canvas.tileCount) }
    return (textView != nil ? "textView" : "", "", 0)
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

  deinit {
    displayLink?.invalidate()
    watcher.stop()
    if let textView { Self.recycle(textView) }
  }

  /// Cleared text views, reused by new renderers: creating one is the main cost of mounting a
  /// renderer that gets text. Bounded, so leaving a long chat doesn't pin hundreds of them.
  private static var textViewPool: [StreamTextView] = []
  private static let textViewPoolSize = 24

  /// Under memory pressure the pool is the first thing to go.
  private static let poolDrain: NSObjectProtocol = NotificationCenter.default.addObserver(
    forName: UIApplication.didReceiveMemoryWarningNotification, object: nil, queue: .main
  ) { _ in textViewPool.removeAll() }

  /// Benchmarks: a cold start for the next mounts. Main thread.
  @_spi(Testing) public static func drainTextViewPool() {
    textViewPool.removeAll()
  }

  private static func recycle(_ textView: StreamTextView) {
    let started = CACurrentMediaTime()
    defer { StreamTextRenderStats.shared.addPhase("textView.recycle", time: CACurrentMediaTime() - started) }
    _ = poolDrain
    textView.removeFromSuperview()
    guard textViewPool.count < textViewPoolSize else { return }
    textView.text = ""
    textView.visibleBounds = nil
    textViewPool.append(textView)
  }

  /// Stops reading the buffer and the frame loop, without callbacks. The component view calls it
  /// before it releases the buffer, as the watcher reads it off the main thread.
  @objc public func invalidate() {
    setTicking(false)
    watcher.stop()
    base = nil
    finished = true
  }

  // MARK: - Source

  /// Starts rendering the TSB1 buffer at `base` (`size` bytes); nil detaches. The caller keeps
  /// the memory alive until the next call.
  /// `prepared`: the layout the shadow node prepared for this buffer, if it is finished and small.
  @objc public func attach(base newBase: UnsafeMutableRawPointer?, size: Int, prepared: StreamTextPreparedLayout?) {
    let attachStarted = CACurrentMediaTime()
    defer {
      let time = CACurrentMediaTime() - attachStarted
      StreamTextRenderStats.shared.addTotals { $0.attaches += 1; $0.attachTime += time }
    }
    setTicking(false)
    watcher.stop()
    instantFill = false
    // A new source starts its own status sequence (a finished one reports only its final status).
    reportedStatus = nil
    base = nil
    capacity = 0
    offset = 0
    heldUTF8 = 0
    finished = false
    unloggedShown = nil
    if let displayLogKey {
      StreamTextDisplayLogs.shared.clear(displayLogKey)
      StreamTextRenderStats.shared.clear(displayLogKey)
    }
    stats = StreamTextRenderStats.Sample()
    if let textView, textView.textStorage.length > 0 { textView.text = "" }
    canvas?.removeFromSuperview()
    canvas = nil
    setTextViewHeight(StreamTextLayout.textViewHeight)
    // The height is reported once at the end: clearing and refilling in one go is one commit.
    defer { reportHeight() }

    guard let newBase, size >= Int(TSB_HEADER_SIZE) else {
      reportStatus("idle", bytes: 0)
      return
    }
    let header = newBase.assumingMemoryBound(to: TSBHeader.self).pointee
    guard header.magic == TSB_MAGIC, Int(header.version) == Int(TSB_VERSION),
          header.capacity <= UInt64(size - Int(TSB_HEADER_SIZE)) else {
      NSLog("[StreamText] source is not a valid TSB1 buffer")
      reportStatus("idle", bytes: 0)
      return
    }
    base = newBase
    capacity = header.capacity
    // A finished stream (e.g. a message remounted by a list) that is small enough is shown in one
    // go, at its final height, instead of being typed out again over several frames.
    instantFill = tsb_status(newBase) != UInt32(TSB_STATUS_OPEN) && tsb_committed(newBase) <= StreamTextLayout.instantFillBytes
    if instantFill, let prepared, prepared.bytes == min(tsb_committed(newBase), capacity),
       abs(prepared.width - bounds.width) <= 0.5 {
      showPrepared(prepared, status: tsb_status(newBase))
      return
    }
    if !instantFill {
      reportStatus("streaming", bytes: 0)
      setTicking(window != nil)
      if window != nil { watcher.start(base: newBase, from: 0) }
    }
    // A freshly mounted view has no width yet: the text is laid out once it gets one.
    tick(fillAll: instantFill)
  }

  public override func didMoveToWindow() {
    super.didMoveToWindow()
    let active = window != nil && base != nil && !finished
    setTicking(active)
    if active, let base { watcher.start(base: base, from: offset) } else { watcher.stop() }
  }

  public override func layoutSubviews() {
    super.layoutSubviews()
    // Only a new width re-wraps the text; a new height is our own report coming back.
    guard bounds.width != layoutWidth else { return }
    let started = CACurrentMediaTime()
    layoutWidth = bounds.width
    // A prepared layout only fits the width it was made for.
    if let canvas, abs(canvas.layout.width - bounds.width) > 0.5 { _ = ensureTextView() }
    if let textView { setTextViewHeight(textView.frame.height) }
    reportHeight()
    let time = CACurrentMediaTime() - started
    StreamTextRenderStats.shared.addTotals { $0.widthLayouts += 1; $0.widthLayoutTime += time }
  }

  // MARK: - Style

  private var textColor: UIColor { textAttributes[.foregroundColor] as? UIColor ?? .label }

  /// Re-applies the style to the whole text: O(text), so only call it when a style prop changed.
  @objc public func applyTextStyle() {
    applyTextStyle(layoutChanged: true)
  }

  /// `layoutChanged`: a prop that moves the lines changed (anything but the color). A canvas waits
  /// for the layout the shadow node prepares for the new style (`updatePrepared`).
  @objc public func applyTextStyle(layoutChanged: Bool) {
    let attributes = textStyle.attributes()
    textAttributes = attributes
    if let canvas {
      canvas.color = textColor
      if layoutChanged || !canvas.layout.colorIndependent { canvasStyleStale = true }
      return
    }
    guard let textView else { return }
    textView.typingAttributes = attributes
    let storage = textView.textStorage
    storage.setAttributes(attributes, range: NSRange(location: 0, length: storage.length))
    reportHeight()
  }

  // MARK: - Measuring

  private func setTextViewHeight(_ height: CGFloat) {
    textView?.frame = CGRect(x: 0, y: 0, width: bounds.width, height: height)
  }

  /// Returns the time spent committing the new height, if there was one.
  @discardableResult
  private func reportHeight() -> CFTimeInterval {
    guard bounds.width > 0 else { return 0 }
    let started = CACurrentMediaTime()
    if let canvas {
      guard canvas.layout.height != reportedHeight else { return 0 }
      reportedHeight = canvas.layout.height
      onHeightChange?(reportedHeight)
      return CACurrentMediaTime() - started
    }
    guard let textView else {
      // No text view yet, so no text: an empty view has no height.
      guard reportedHeight != 0 else { return 0 }
      reportedHeight = 0
      onHeightChange?(0)
      return CACurrentMediaTime() - started
    }
    let layoutManager = textView.layoutManager
    layoutManager.ensureLayout(for: textView.textContainer)
    // Empty text still has a line for the caret; like <Text>, an empty view has no height (and
    // mounting one doesn't need a commit).
    var height = textView.textStorage.length == 0 ? 0 : ceil(layoutManager.usedRect(for: textView.textContainer).height)
    // Text is only laid out inside the text view's frame: grow it before the text reaches the end.
    if height > textView.frame.height * 0.75 {
      var newHeight = textView.frame.height
      while height > newHeight * 0.75 { newHeight *= 2 }
      setTextViewHeight(newHeight)
      layoutManager.ensureLayout(for: textView.textContainer)
      height = ceil(layoutManager.usedRect(for: textView.textContainer).height)
      stats.grows += 1
    }
    let laidOut = CACurrentMediaTime()
    StreamTextRenderStats.shared.addPhase("measure.layout", time: laidOut - started)
    stats.layout += laidOut - started
    guard height != reportedHeight else {
      StreamTextRenderStats.shared.addTotals { $0.layout += laidOut - started }
      return 0
    }
    reportedHeight = height
    onHeightChange?(height)
    let committed = CACurrentMediaTime()
    stats.commit += committed - laidOut
    stats.heightReports += 1
    StreamTextRenderStats.shared.addTotals {
      $0.layout += laidOut - started
      $0.commit += committed - laidOut
    }
    return committed - laidOut
  }

  private func reportStatus(_ status: String, bytes: UInt64) {
    guard status != reportedStatus else { return }
    reportedStatus = status
    onStatusChange?(status, Double(bytes))
  }

  // MARK: - Frame loop

  private func setTicking(_ on: Bool) {
    if on {
      if let link = displayLink {
        link.isPaused = false
        return
      }
      let link = CADisplayLink(target: RendererLinkProxy(self), selector: #selector(RendererLinkProxy.tick))
      // Text gains nothing from 120 Hz; 60 is plenty and halves the wakeups on ProMotion.
      link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
      link.add(to: .main, forMode: .common)
      displayLink = link
    } else {
      displayLink?.invalidate()
      displayLink = nil
    }
  }

  /// New bytes or a final status landed: show them now, not at the next frame.
  private func watcherWoke() {
    watcher.didWake()
    guard base != nil, !finished else { return }
    tick()
  }

  fileprivate func tick(fillAll: Bool = false) {
    // Text appended last tick has been laid out and committed by now: log it as shown.
    if let bytes = unloggedShown, let displayLogKey {
      StreamTextDisplayLogs.shared.append(displayLogKey, bytes: bytes)
      unloggedShown = nil
    }
    guard let base, !finished else {
      setTicking(false)
      return
    }
    // Status first: once it is terminal, the committed value read after it is final.
    let status = tsb_status(base)
    let committed = min(tsb_committed(base), capacity)
    let consumedBefore = offset

    if committed > offset {
      let storage = ensureTextView().textStorage
      let budget = fillAll ? committed - offset : UInt64(bytesPerTick)
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
      stats.append += CACurrentMediaTime() - started
      StreamTextRenderStats.shared.addPhase("fill.append", time: CACurrentMediaTime() - started)
      // Before the first layout there is no width: the text is laid out once the view gets one.
      let measured = bounds.width > 0
      let commitTime = fillAll ? 0 : reportHeight()
      let tickTime = CACurrentMediaTime() - started
      stats.ticks += 1
      stats.maxTick = max(stats.maxTick, tickTime)
      stats.maxTickBytes = max(stats.maxTickBytes, Double(appended))
      if tickTime > 0.008 { stats.slowTicks += 1 }
      StreamTextRenderStats.shared.addTotals { $0.ticks += 1; $0.tickTime += tickTime }
      // Re-estimate from ticks that hit the budget and were laid out (small appends say little about
      // the cost per byte), without the commit, whose cost doesn't grow with the bytes. The budget
      // at most doubles per tick, so one cheap tick can't schedule a huge append.
      if !fillAll && measured && appended >= budget {
        let perByte = max(tickTime - commitTime, 1e-6) / Double(appended)
        let estimate = min(Int(Self.frameBudget / perByte), bytesPerTick * 2)
        bytesPerTick = min(max(estimate, Self.minBytesPerTick), Self.maxBytesPerTick)
      }
    }
    if displayLogKey != nil && offset > consumedBefore { unloggedShown = offset }

    if status != UInt32(TSB_STATUS_OPEN) && offset >= committed {
      if heldUTF8 > 0 {
        // A truncated sequence at the very end: show it as U+FFFD rather than dropping it.
        let held = base.advanced(by: Int(TSB_HEADER_SIZE) + Int(offset) - heldUTF8).assumingMemoryBound(to: UInt8.self)
        appendString(String(decoding: UnsafeBufferPointer(start: held, count: heldUTF8), as: UTF8.self))
        heldUTF8 = 0
      }
      if !fillAll { reportHeight() }
      finished = true
      // Everything is in the text storage now: let go of the buffer (the watcher first, as it reads it).
      watcher.stop()
      self.base = nil
      if let displayLogKey { StreamTextRenderStats.shared.set(displayLogKey, stats) }
      reportStatus(terminalStatus(status), bytes: committed)
      // One more tick when there is an append left to log.
      if unloggedShown == nil { setTicking(false) }
      return
    }
    // Frames only while there is a backlog, or an append left to log; otherwise the watcher wakes us.
    if let displayLink { displayLink.isPaused = offset >= committed && unloggedShown == nil }
  }

  private func terminalStatus(_ status: UInt32) -> String {
    switch status {
    case UInt32(TSB_STATUS_DONE): return "done"
    case UInt32(TSB_STATUS_CANCELLED): return "cancelled"
    case UInt32(TSB_STATUS_OVERFLOW): return "truncated"
    default: return "error"
    }
  }

  /// Decodes `count` bytes from the buffer and appends them. An unfinished sequence at the end is
  /// held and decoded again with the bytes that follow, so chunk boundaries never change the text.
  private func appendUTF8(_ start: UnsafePointer<UInt8>, _ count: Int) {
    // The held bytes are the ones just before `start` in the buffer.
    let held = heldUTF8
    let p = start - held
    let n = held + count
    let complete = rendererUTF8CompletePrefixLength(p, n)
    heldUTF8 = n - complete
    if complete > 0 {
      appendString(streamTextDecodeUTF8(p, complete, atStreamStart: offset == UInt64(held)))
    }
  }

  private func appendString(_ text: String) {
    ensureTextView().textStorage.append(NSAttributedString(string: text, attributes: textAttributes))
  }
}

/// Exposes only the end of the text, in the renderer's visible frame: an accessibility snapshot of
/// a text view holding megabytes of text otherwise stalls the main thread for seconds, and the text
/// view itself is much taller than what is on screen.
private final class StreamTextView: UITextView {
  private static let accessibleTailCharacters = 2000

  var visibleBounds: (() -> CGRect)?

  init() {
    // An explicit TextKit 1 stack (see StreamTextRenderer); the container follows the view's size.
    let storage = NSTextStorage()
    let layoutManager = NSLayoutManager()
    storage.addLayoutManager(layoutManager)
    let container = NSTextContainer(size: .zero)
    container.widthTracksTextView = true
    container.heightTracksTextView = true
    layoutManager.addTextContainer(container)
    super.init(frame: .zero, textContainer: container)
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

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

  override var accessibilityFrame: CGRect {
    get {
      guard let superview, let visible = visibleBounds?() else { return super.accessibilityFrame }
      return UIAccessibility.convertToScreenCoordinates(visible, in: superview)
    }
    set {}
  }

  override var accessibilityElements: [Any]? {
    get { nil }
    set {}
  }
}

/// UTF-8 to String, with U+FFFD for invalid bytes. Only a BOM opening the stream is dropped, as
/// TextDecoder does: NSString would drop one at the start of every chunk.
func streamTextDecodeUTF8(_ bytes: UnsafePointer<UInt8>, _ count: Int, atStreamStart: Bool) -> String {
  var p = bytes
  var n = count
  var prefix = ""
  while n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF {
    if !(atStreamStart && p == bytes) { prefix += "\u{FEFF}" }
    p += 3
    n -= 3
  }
  // One copy into an NSString; invalid input falls back to U+FFFD replacement.
  let text = NSString(bytes: p, length: n, encoding: String.Encoding.utf8.rawValue) as String?
    ?? String(decoding: UnsafeBufferPointer(start: p, count: n), as: UTF8.self)
  return prefix.isEmpty ? text : prefix + text
}

/// Total length of the UTF-8 sequence that starts with `lead` (1 for invalid leads).
private func rendererUTF8SequenceLength(_ lead: UInt8) -> Int {
  lead < 0xC0 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2
}

/// Length of the longest prefix that does not end inside a UTF-8 sequence.
private func rendererUTF8CompletePrefixLength(_ p: UnsafePointer<UInt8>, _ n: Int) -> Int {
  var i = n - 1
  while i >= 0 && n - i <= 4 {
    let b = p[i]
    if b & 0xC0 != 0x80 {
      return n - i >= rendererUTF8SequenceLength(b) ? n : i
    }
    i -= 1
  }
  return n  // invalid trailing bytes: let the decoder replace them
}

/// Benchmark display logs, `[wallClockMs, bytesShown, ...]` per key; written on main, read from JS.
@objc(StreamTextDisplayLogs)
public final class StreamTextDisplayLogs: NSObject {
  @objc public static let shared = StreamTextDisplayLogs()
  private let lock = NSLock()
  private var logs: [String: [Double]] = [:]

  func append(_ key: String, bytes: UInt64) {
    let now = Date().timeIntervalSince1970 * 1000
    lock.withLock { logs[key, default: []].append(contentsOf: [now, Double(bytes)]) }
  }

  func clear(_ key: String) {
    lock.withLock { logs[key] = nil }
  }

  @objc public func log(for key: String) -> [Double] {
    lock.withLock { logs[key] ?? [] }
  }
}

/// Benchmark render stats per key, written when a stream finishes; read from JS.
@objc(StreamTextRenderStats)
public final class StreamTextRenderStats: NSObject {
  struct Sample {
    var ticks = 0.0, append = 0.0, layout = 0.0, commit = 0.0, maxTick = 0.0
    var heightReports = 0.0, maxTickBytes = 0.0, slowTicks = 0.0, grows = 0.0
  }

  /// Main-thread time summed over every renderer, keyed or not (seconds).
  struct Totals {
    var inits = 0.0, initTime = 0.0, ticks = 0.0, tickTime = 0.0, layout = 0.0, commit = 0.0
    var attaches = 0.0, attachTime = 0.0, widthLayouts = 0.0, widthLayoutTime = 0.0
  }

  @objc public static let shared = StreamTextRenderStats()
  private let lock = NSLock()
  private var samples: [String: Sample] = [:]
  private var totals = Totals()

  /// Off until a benchmark asks for totals: views then pay nothing but a flag check.
  @objc public static var enabled = false

  private var phases: [String: (count: Double, time: Double)] = [:]

  /// Benchmarks: adds one run of a named phase (seconds). Any thread.
  @objc public func addPhase(_ name: String, time: CFTimeInterval) {
    guard Self.enabled else { return }
    lock.withLock {
      let current = phases[name] ?? (0, 0)
      phases[name] = (current.count + 1, current.time + time)
    }
  }

  /// `{"<phase>.count": n, "<phase>.ms": total}` since the last reset.
  @objc public func phases(reset: Bool) -> [String: Double] {
    lock.withLock {
      var out: [String: Double] = [:]
      for (name, value) in phases {
        out[name + ".count"] = value.count
        out[name + ".ms"] = value.time * 1000
      }
      if reset { phases.removeAll() }
      return out
    }
  }

  func addTotals(_ update: (inout Totals) -> Void) {
    guard Self.enabled else { return }
    lock.withLock { update(&totals) }
  }

  /// `[inits, initMs, ticks, tickMs, layoutMs, commitMs, attaches, attachMs, widthLayouts, widthLayoutMs]`
  /// since the last reset.
  @objc public func totals(reset: Bool) -> [Double] {
    lock.withLock {
      let t = totals
      if reset { totals = Totals() }
      return [t.inits, t.initTime * 1000, t.ticks, t.tickTime * 1000, t.layout * 1000, t.commit * 1000,
              t.attaches, t.attachTime * 1000, t.widthLayouts, t.widthLayoutTime * 1000]
    }
  }

  func set(_ key: String, _ sample: Sample) {
    lock.withLock { samples[key] = sample }
  }

  func clear(_ key: String) {
    lock.withLock { samples[key] = nil }
  }

  /// `[ticks, appendMs, layoutMs, commitMs, maxTickMs, heightReports, maxTickBytes, slowTicks, grows]`,
  /// or empty until the stream finished.
  @objc public func stats(for key: String) -> [Double] {
    guard let s = lock.withLock({ samples[key] }) else { return [] }
    return [s.ticks, s.append * 1000, s.layout * 1000, s.commit * 1000, s.maxTick * 1000,
            s.heightReports, s.maxTickBytes, s.slowTicks, s.grows]
  }
}

/// Watches a buffer's header off the main thread and wakes the renderer when bytes or a final
/// status land. It polls fast while data flows and backs off while the producer is quiet (a model
/// "thinking"), and stops itself once the status is final.
private final class BufferWatcher {
  private static let queue = DispatchQueue(label: "StreamText.watcher", qos: .userInteractive)

  private let lock = NSLock()
  private let wake: () -> Void
  // Guarded by `lock`.
  private var base: UnsafeMutableRawPointer?
  private var seenBytes: UInt64 = 0
  private var seenFinal = false
  private var wakePending = false
  private var lastChange: CFTimeInterval = 0
  private var intervalMs = 0
  private var timer: DispatchSourceTimer?

  init(wake: @escaping () -> Void) {
    self.wake = wake
  }

  /// Watches `base` for bytes beyond `from`. Main thread.
  func start(base: UnsafeMutableRawPointer, from bytes: UInt64) {
    stop()
    let timer = DispatchSource.makeTimerSource(queue: Self.queue)
    lock.withLock {
      self.base = base
      seenBytes = bytes
      seenFinal = false
      wakePending = false
      lastChange = CACurrentMediaTime()
      self.timer = timer
      schedule(timer, ms: 8)
    }
    timer.setEventHandler { [weak self] in self?.poll() }
    timer.resume()
  }

  /// After this returns, the watcher no longer reads the buffer. Main thread.
  func stop() {
    let timer = lock.withLock { () -> DispatchSourceTimer? in
      base = nil
      defer { self.timer = nil }
      return self.timer
    }
    timer?.cancel()
  }

  /// The renderer handled the wake; the next change wakes it again. Main thread.
  func didWake() {
    lock.withLock { wakePending = false }
  }

  private func poll() {
    let shouldWake: Bool = lock.withLock {
      guard let base else { return false }
      // Status first: once it is terminal, the committed value read after it is final.
      let final = tsb_status(base) != UInt32(TSB_STATUS_OPEN)
      let committed = tsb_committed(base)
      let now = CACurrentMediaTime()
      var changed = false
      if committed > seenBytes {
        seenBytes = committed
        changed = true
      }
      if final && !seenFinal {
        seenFinal = true
        changed = true
      }
      if changed { lastChange = now }
      if seenFinal {
        timer?.cancel()
        timer = nil
      } else if let timer {
        // 8 ms while data flows, then 16 ms, then 50 ms once the producer has been quiet for 5 s.
        let quiet = now - lastChange
        let ms = quiet < 1 ? 8 : quiet < 5 ? 16 : 50
        if ms != intervalMs { schedule(timer, ms: ms) }
      }
      guard changed, !wakePending else { return false }
      wakePending = true
      return true
    }
    if shouldWake { DispatchQueue.main.async(execute: wake) }
  }

  /// Under `lock`.
  private func schedule(_ timer: DispatchSourceTimer, ms: Int) {
    intervalMs = ms
    timer.schedule(deadline: .now() + .milliseconds(ms), repeating: .milliseconds(ms), leeway: .milliseconds(ms / 4))
  }
}

/// Breaks the CADisplayLink -> target retain cycle.
private final class RendererLinkProxy: NSObject {
  private weak var target: StreamTextRenderer?
  init(_ target: StreamTextRenderer) { self.target = target }
  @objc func tick() { target?.tick() }
}
