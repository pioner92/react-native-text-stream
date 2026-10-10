import UIKit

/// Renders a TSB1 buffer (see TextStreamBuffer.h) as it fills and sizes itself to the text:
/// after each append it reports the laid-out height, which the Fabric component view feeds
/// into its shadow node's state. No JS is involved per chunk.
@objc(StreamTextRenderer)
public final class StreamTextRenderer: UIView {
  private static let sliceBytes = 16 * 1024
  /// Main-thread time per frame we allow for append + layout (a 60 Hz frame is ~16.7 ms).
  private static let frameBudget: CFTimeInterval = 0.006
  private static let minBytesPerTick = 4 * 1024
  private static let maxBytesPerTick = 1024 * 1024
  private static let idleAfter: CFTimeInterval = 0.25
  private static let idlePollFast: DispatchTimeInterval = .milliseconds(33)
  private static let idlePollSlow: DispatchTimeInterval = .milliseconds(100)
  private static let idleSlowAfter: CFTimeInterval = 2

  private let textView: UITextView
  /// The text view keeps this fixed height and the renderer clips it: a text view whose frame
  /// grows re-lays out all of its text on every resize.
  private static let textViewHeight: CGFloat = 1_000_000

  // Reader state, main thread only.
  private var base: UnsafeMutableRawPointer?
  private var capacity: UInt64 = 0
  /// Bytes consumed from the buffer: shown, or held in `utf8Tail`.
  private var offset: UInt64 = 0
  private var utf8Tail: [UInt8] = []
  private var finished = false
  private var lastDataAt: CFTimeInterval = 0
  private var bytesPerTick = 32 * 1024
  private var displayLink: CADisplayLink?
  private var idleTimer: DispatchSourceTimer?
  private var idlePollIsSlow = false

  private var textAttributes: [NSAttributedString.Key: Any] = [:]
  private var reportedHeight: CGFloat = -1
  private var unloggedShown: UInt64?

  /// Benchmarks only: when set, records when bytes reach the screen under this key.
  @objc public var displayLogKey: String?

  /// Called on the main thread with the text's height at the current width.
  @objc public var onHeightChange: ((CGFloat) -> Void)?

  @objc public var fontSize: CGFloat = 15 {
    didSet { updateTextAttributes() }
  }

  @objc public var textColor: UIColor? {
    didSet { updateTextAttributes() }
  }

  public override init(frame: CGRect) {
    // TextKit 1: appending to the end re-lays out only the tail, and the used rect gives the height
    // without laying out the whole text again (TextKit 2 does that once the view isn't scrolling).
    if #available(iOS 16.0, *) {
      textView = UITextView(usingTextLayoutManager: false)
    } else {
      textView = UITextView()
    }
    super.init(frame: frame)
    // Contiguous layout: appending to the end lays out only the new tail.
    textView.layoutManager.allowsNonContiguousLayout = false
    clipsToBounds = true
    textView.isEditable = false
    textView.isSelectable = true
    // Sized by its text: the surrounding list scrolls, not this view.
    textView.isScrollEnabled = false
    textView.backgroundColor = .clear
    textView.textContainerInset = .zero
    textView.textContainer.lineFragmentPadding = 0
    textView.frame = CGRect(x: 0, y: 0, width: bounds.width, height: Self.textViewHeight)
    addSubview(textView)
    updateTextAttributes()
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

  deinit {
    displayLink?.invalidate()
    idleTimer?.cancel()
  }

  // MARK: - Source

  /// Starts rendering the TSB1 buffer at `base` (`size` bytes); nil detaches. The caller keeps
  /// the memory alive until the next call.
  @objc public func attach(base newBase: UnsafeMutableRawPointer?, size: Int) {
    setTicking(false)
    base = nil
    capacity = 0
    offset = 0
    utf8Tail.removeAll()
    finished = false
    unloggedShown = nil
    if let displayLogKey { StreamTextDisplayLogs.shared.clear(displayLogKey) }
    textView.text = ""
    reportHeight()

    guard let newBase, size >= Int(TSB_HEADER_SIZE) else { return }
    let header = newBase.assumingMemoryBound(to: TSBHeader.self).pointee
    guard header.magic == TSB_MAGIC, Int(header.version) == Int(TSB_VERSION),
          header.capacity <= UInt64(size - Int(TSB_HEADER_SIZE)) else {
      NSLog("[StreamText] source is not a valid TSB1 buffer")
      return
    }
    base = newBase
    capacity = header.capacity
    lastDataAt = CACurrentMediaTime()
    setTicking(window != nil)
    tick()
  }

  public override func didMoveToWindow() {
    super.didMoveToWindow()
    setTicking(window != nil && base != nil && !finished)
  }

  private var layoutWidth: CGFloat = -1

  public override func layoutSubviews() {
    super.layoutSubviews()
    // Only a new width re-wraps the text; a new height is our own report coming back.
    guard bounds.width != layoutWidth else { return }
    layoutWidth = bounds.width
    textView.frame = CGRect(x: 0, y: 0, width: bounds.width, height: Self.textViewHeight)
    reportHeight()
  }

  private func updateTextAttributes() {
    let font = UIFont.systemFont(ofSize: fontSize)
    textAttributes = [.font: font, .foregroundColor: textColor ?? .label]
    textView.typingAttributes = textAttributes
    let storage = textView.textStorage
    storage.setAttributes(textAttributes, range: NSRange(location: 0, length: storage.length))
    reportHeight()
  }

  private func reportHeight() {
    let width = bounds.width
    guard width > 0 else { return }
    let layoutManager = textView.layoutManager
    layoutManager.ensureLayout(for: textView.textContainer)
    let insets = textView.textContainerInset
    let height = ceil(layoutManager.usedRect(for: textView.textContainer).height + insets.top + insets.bottom)
    guard height != reportedHeight else { return }
    reportedHeight = height
    onHeightChange?(height)
  }

  // MARK: - Frame loop

  private func setTicking(_ on: Bool) {
    stopIdlePolling()
    if on {
      if let link = displayLink {
        link.isPaused = false
        return
      }
      let link = CADisplayLink(target: RendererLinkProxy(self), selector: #selector(RendererLinkProxy.tick))
      link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
      link.add(to: .main, forMode: .common)
      displayLink = link
    } else {
      displayLink?.invalidate()
      displayLink = nil
    }
  }

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
      tick()
    } else if !idlePollIsSlow && CACurrentMediaTime() - lastDataAt > Self.idleSlowAfter {
      scheduleIdlePoll(slow: true)
    }
  }

  fileprivate func tick() {
    // Text appended last tick has been laid out and committed by now: log it as shown.
    if let bytes = unloggedShown, let displayLogKey {
      StreamTextDisplayLogs.shared.append(displayLogKey, bytes: bytes)
      unloggedShown = nil
    }
    guard let base, !finished else {
      setTicking(false)
      return
    }
    let consumedBefore = offset
    // Status first: once it is terminal, the committed value read after it is final.
    let status = tsb_status(base)
    let committed = min(tsb_committed(base), capacity)
    let now = CACurrentMediaTime()

    if committed > offset {
      lastDataAt = now
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
      reportHeight()
      if appended >= budget {
        let perByte = max(CACurrentMediaTime() - started, 1e-6) / Double(appended)
        bytesPerTick = min(max(Int(Self.frameBudget / perByte), Self.minBytesPerTick), Self.maxBytesPerTick)
      }
    }

    if status != UInt32(TSB_STATUS_OPEN) && offset >= committed {
      if !utf8Tail.isEmpty {
        appendString(String(decoding: utf8Tail, as: UTF8.self))
        utf8Tail.removeAll()
        reportHeight()
      }
      finished = true
      // One more tick, so the last append can still be logged.
      if displayLogKey != nil && offset > consumedBefore { unloggedShown = offset; return }
      setTicking(false)
      return
    }
    if displayLogKey != nil && offset > consumedBefore { unloggedShown = offset }
    if offset >= committed && now - lastDataAt > Self.idleAfter {
      startIdlePolling()
    }
  }

  private func appendUTF8(_ start: UnsafePointer<UInt8>, _ count: Int) {
    var p = start
    var remaining = count
    if !utf8Tail.isEmpty {
      let needed = rendererUTF8SequenceLength(utf8Tail[0])
      let take = min(needed - utf8Tail.count, remaining)
      utf8Tail.append(contentsOf: UnsafeBufferPointer(start: p, count: take))
      p += take
      remaining -= take
      guard utf8Tail.count >= needed else { return }
      appendString(String(decoding: utf8Tail, as: UTF8.self))
      utf8Tail.removeAll()
    }
    guard remaining > 0 else { return }
    let complete = rendererUTF8CompletePrefixLength(p, remaining)
    if complete > 0 {
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
}

private func rendererUTF8SequenceLength(_ lead: UInt8) -> Int {
  lead < 0xC0 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2
}

private func rendererUTF8CompletePrefixLength(_ p: UnsafePointer<UInt8>, _ n: Int) -> Int {
  var i = n - 1
  while i >= 0 && n - i <= 4 {
    let b = p[i]
    if b & 0xC0 != 0x80 {
      return n - i >= rendererUTF8SequenceLength(b) ? n : i
    }
    i -= 1
  }
  return n
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

/// Breaks the CADisplayLink -> target retain cycle.
private final class RendererLinkProxy: NSObject {
  private weak var target: StreamTextRenderer?
  init(_ target: StreamTextRenderer) { self.target = target }
  @objc func tick() { target?.tick() }
}
