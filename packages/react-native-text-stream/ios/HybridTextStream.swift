//
//  HybridTextStream.swift
//  Pods
//
//  Created by Alex Shumihin on 10/10/2026.
//

import Foundation
import NitroModules
import UIKit

/// Renders a TSB1 buffer (see TextStreamBuffer.h) as it fills: once per frame the
/// main thread appends the newly committed bytes to the text storage. No JS involved.
class HybridTextStream: HybridTextStreamSpec {
  private let container = TextStreamContainer()
  private let textView: UITextView
  private let statsLabel = UILabel()

  var view: UIView { container }

  // Reader state, main thread only.
  private var buffer: ArrayBuffer?
  private var base: UnsafeMutableRawPointer?
  private var capacity: UInt64 = 0
  private var offset: UInt64 = 0
  private var pending: [UInt8] = []
  private var lastStatus: UInt32 = UInt32(TSB_STATUS_OPEN)
  private var displayLink: CADisplayLink?

  private var textAttributes: [NSAttributedString.Key: Any] = [:]

  var source: ArrayBuffer? {
    didSet { attach(source) }
  }

  var fontSize: Double? {
    didSet { updateTextAttributes() }
  }

  var color: String? {
    didSet { updateTextAttributes() }
  }

  var showStats: Bool? {
    didSet {
      statsLabel.isHidden = !(showStats ?? false)
      updateStats(committed: offset)
    }
  }

  override init() {
    if #available(iOS 16.0, *) {
      // TextKit 2 lays out only the visible viewport, so appends stay cheap as the text grows.
      textView = UITextView(usingTextLayoutManager: true)
    } else {
      textView = UITextView()
    }
    super.init()

    textView.isEditable = false
    textView.isSelectable = true
    textView.alwaysBounceVertical = true
    textView.backgroundColor = .clear
    textView.indicatorStyle = .white
    textView.textContainerInset = UIEdgeInsets(top: 14, left: 12, bottom: 40, right: 12)
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
      self?.setTicking(inWindow && self?.base != nil)
    }
  }

  deinit {
    displayLink?.invalidate()
  }

  private func updateTextAttributes() {
    let paragraph = NSMutableParagraphStyle()
    paragraph.lineHeightMultiple = 1.25
    paragraph.paragraphSpacing = 4
    textAttributes = [
      .font: UIFont.systemFont(ofSize: CGFloat(fontSize ?? 15)),
      .foregroundColor: UIColor(hex: color) ?? .label,
      .paragraphStyle: paragraph,
    ]
    textView.typingAttributes = textAttributes
    let storage = textView.textStorage
    storage.setAttributes(textAttributes, range: NSRange(location: 0, length: storage.length))
  }

  // MARK: - Source

  private func attach(_ newBuffer: ArrayBuffer?) {
    setTicking(false)
    buffer = nil
    base = nil
    capacity = 0
    offset = 0
    pending.removeAll()
    lastStatus = UInt32(TSB_STATUS_OPEN)
    textView.text = ""
    updateStats(committed: 0)

    guard let newBuffer else { return }
    // A non-owning buffer was created by JS and may only be touched on the JS thread.
    guard newBuffer.isOwner else {
      NSLog("[TextStream] source is not a native-owned ArrayBuffer; ignoring it")
      return
    }
    let size = newBuffer.size
    guard size >= Int(TSB_HEADER_SIZE) else {
      NSLog("[TextStream] source is too small for a TSB1 header (%d bytes)", size)
      return
    }
    let raw = UnsafeMutableRawPointer(newBuffer.data)
    let header = raw.assumingMemoryBound(to: TSBHeader.self).pointee
    guard header.magic == TSB_MAGIC, Int(header.version) == Int(TSB_VERSION),
          header.capacity <= UInt64(size - Int(TSB_HEADER_SIZE)) else {
      NSLog("[TextStream] source is not a valid TSB1 buffer")
      return
    }

    buffer = newBuffer  // keeps the memory alive while we read it
    base = raw
    capacity = header.capacity
    setTicking(container.window != nil)
    tick()
  }

  // MARK: - Frame loop

  private func setTicking(_ on: Bool) {
    if on {
      guard displayLink == nil else { return }
      let link = CADisplayLink(target: DisplayLinkProxy(self), selector: #selector(DisplayLinkProxy.tick))
      link.add(to: .main, forMode: .common)
      displayLink = link
    } else {
      displayLink?.invalidate()
      displayLink = nil
    }
  }

  fileprivate func tick() {
    guard let base else {
      setTicking(false)
      return
    }
    // Status first: once it is terminal, the committed value read after it is final.
    let status = tsb_status(base)
    let committed = min(tsb_committed(base), capacity)

    if committed > offset {
      let start = base.advanced(by: Int(TSB_HEADER_SIZE) + Int(offset)).assumingMemoryBound(to: UInt8.self)
      pending.append(contentsOf: UnsafeBufferPointer(start: start, count: Int(committed - offset)))
      offset = committed
      let complete = utf8CompletePrefixLength(pending)
      if complete > 0 {
        appendText(String(decoding: pending[0..<complete], as: UTF8.self))
        pending.removeFirst(complete)
      }
    }

    if status != UInt32(TSB_STATUS_OPEN) {
      if !pending.isEmpty {
        appendText(String(decoding: pending, as: UTF8.self))
        pending.removeAll()
      }
      lastStatus = status
      setTicking(false)
    }
    updateStats(committed: committed)
  }

  private func appendText(_ text: String) {
    // Follow the tail unless the user is scrolling or has scrolled away from the bottom.
    let userScrolling = textView.isTracking || textView.isDecelerating
    let distanceToBottom = textView.contentSize.height - textView.bounds.height - textView.contentOffset.y
    let follow = !userScrolling && distanceToBottom <= 40
    textView.textStorage.append(NSAttributedString(string: text, attributes: textAttributes))
    if follow {
      textView.layoutIfNeeded()
      let maxY = textView.contentSize.height - textView.bounds.height + textView.adjustedContentInset.bottom
      textView.setContentOffset(CGPoint(x: 0, y: max(-textView.adjustedContentInset.top, maxY)), animated: false)
    }
  }

  private func updateStats(committed: UInt64) {
    guard !statsLabel.isHidden else { return }
    let (dot, label): (UIColor, String)
    switch lastStatus {
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

private func formatBytes(_ bytes: UInt64) -> String {
  bytes < 1024 ? "\(bytes) B" : String(format: "%.1f KB", Double(bytes) / 1024)
}

/// Length of the longest prefix of `bytes` that does not end inside a UTF-8 sequence.
private func utf8CompletePrefixLength(_ bytes: [UInt8]) -> Int {
  let n = bytes.count
  var i = n - 1
  while i >= 0 && n - i <= 4 {
    let b = bytes[i]
    if b & 0xC0 != 0x80 {
      let needed = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1
      return n - i >= needed ? n : i
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
