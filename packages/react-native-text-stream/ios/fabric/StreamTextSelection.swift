import UIKit

/// Text selection on a canvas, the system's own: a read-only `UITextInput` driven by
/// `UITextInteraction(for: .nonEditable)`, which brings the loupe, handles, double tap, pointer
/// selection and the edit menu. Geometry comes from the prepared layout, so selecting never needs
/// a text view or a new layout.
final class StreamTextPosition: UITextPosition {
  let index: Int
  init(_ index: Int) { self.index = index }
}

final class StreamTextRange: UITextRange {
  let range: NSRange
  init(_ range: NSRange) { self.range = range }
  override var start: UITextPosition { StreamTextPosition(range.location) }
  override var end: UITextPosition { StreamTextPosition(NSMaxRange(range)) }
  override var isEmpty: Bool { range.length == 0 }
}

private final class StreamTextSelectionRect: UITextSelectionRect {
  private let frame: CGRect
  private let isStart: Bool
  private let isEnd: Bool
  init(_ frame: CGRect, start: Bool, end: Bool) {
    self.frame = frame
    isStart = start
    isEnd = end
  }
  override var rect: CGRect { frame }
  override var writingDirection: NSWritingDirection { .leftToRight }
  override var containsStart: Bool { isStart }
  override var containsEnd: Bool { isEnd }
  override var isVertical: Bool { false }
}

extension StreamTextCanvas: UITextInput {
  private var string: NSString { layout.text.string as NSString }

  private func clamp(_ index: Int) -> Int { min(max(index, 0), layout.length) }

  private func index(_ position: UITextPosition) -> Int { (position as? StreamTextPosition)?.index ?? 0 }

  /// Selects `range` and tells the interaction, as when the user selects.
  func select(_ range: NSRange) {
    inputDelegate?.selectionWillChange(self)
    selection = range
    inputDelegate?.selectionDidChange(self)
  }

  // MARK: Responder

  override var canBecomeFirstResponder: Bool { true }

  /// UITextInput is a key input: without this, selecting would bring up the keyboard.
  override var inputView: UIView? { UIView(frame: .zero) }
  override var inputAccessoryView: UIView? { nil }

  override func resignFirstResponder() -> Bool {
    let resigned = super.resignFirstResponder()
    if resigned { select(NSRange(location: 0, length: 0)) }
    return resigned
  }

  override func canPerformAction(_ action: Selector, withSender sender: Any?) -> Bool {
    switch action {
    case #selector(copy(_:)): return selection.length > 0
    case #selector(selectAll(_:)): return selection.length < layout.length
    default: return super.canPerformAction(action, withSender: sender)
    }
  }

  override func copy(_ sender: Any?) {
    guard selection.length > 0 else { return }
    UIPasteboard.general.string = string.substring(with: selection)
  }

  override func selectAll(_ sender: Any?) {
    select(NSRange(location: 0, length: layout.length))
  }

  // MARK: UITextInput (read only)

  var selectedTextRange: UITextRange? {
    get { StreamTextRange(selection) }
    set { selection = (newValue as? StreamTextRange)?.range ?? NSRange(location: 0, length: 0) }
  }

  var markedTextRange: UITextRange? { nil }
  var markedTextStyle: [NSAttributedString.Key: Any]? {
    get { nil }
    set {}
  }
  var beginningOfDocument: UITextPosition { StreamTextPosition(0) }
  var endOfDocument: UITextPosition { StreamTextPosition(layout.length) }
  var hasText: Bool { layout.length > 0 }
  var tokenizer: UITextInputTokenizer { textTokenizer }

  func text(in range: UITextRange) -> String? {
    guard let range = range as? StreamTextRange else { return nil }
    return string.substring(with: NSIntersectionRange(range.range, NSRange(location: 0, length: layout.length)))
  }

  func textRange(from fromPosition: UITextPosition, to toPosition: UITextPosition) -> UITextRange? {
    let a = clamp(index(fromPosition))
    let b = clamp(index(toPosition))
    return StreamTextRange(NSRange(location: min(a, b), length: abs(b - a)))
  }

  func position(from position: UITextPosition, offset: Int) -> UITextPosition? {
    let target = index(position) + offset
    return target < 0 || target > layout.length ? nil : StreamTextPosition(target)
  }

  func position(from position: UITextPosition, in direction: UITextLayoutDirection, offset: Int) -> UITextPosition? {
    switch direction {
    case .left: return self.position(from: position, offset: -offset)
    case .right: return self.position(from: position, offset: offset)
    case .up, .down:
      let caret = caretRect(for: position)
      let dy = CGFloat(direction == .up ? -offset : offset) * caret.height
      return closestPosition(to: CGPoint(x: caret.midX, y: caret.midY + dy))
    @unknown default: return nil
    }
  }

  func compare(_ position: UITextPosition, to other: UITextPosition) -> ComparisonResult {
    let a = index(position)
    let b = index(other)
    return a < b ? .orderedAscending : a > b ? .orderedDescending : .orderedSame
  }

  func offset(from: UITextPosition, to toPosition: UITextPosition) -> Int { index(toPosition) - index(from) }

  func position(within range: UITextRange, farthestIn direction: UITextLayoutDirection) -> UITextPosition? {
    direction == .left || direction == .up ? range.start : range.end
  }

  func characterRange(byExtending position: UITextPosition, in direction: UITextLayoutDirection) -> UITextRange? {
    let at = index(position)
    let to = direction == .left || direction == .up ? clamp(at - 1) : clamp(at + 1)
    return StreamTextRange(NSRange(location: min(at, to), length: abs(to - at)))
  }

  func baseWritingDirection(for position: UITextPosition, in direction: UITextStorageDirection) -> NSWritingDirection {
    .leftToRight
  }

  func setBaseWritingDirection(_ writingDirection: NSWritingDirection, for range: UITextRange) {}

  func firstRect(for range: UITextRange) -> CGRect {
    guard let range = range as? StreamTextRange else { return .null }
    return layout.selectionRects(for: range.range).first ?? caretRect(for: range.start)
  }

  func caretRect(for position: UITextPosition) -> CGRect { layout.caretRect(at: clamp(index(position))) }

  func selectionRects(for range: UITextRange) -> [UITextSelectionRect] {
    guard let range = range as? StreamTextRange else { return [] }
    let rects = layout.selectionRects(for: range.range)
    return rects.enumerated().map { offset, rect in
      StreamTextSelectionRect(rect, start: offset == 0, end: offset == rects.count - 1)
    }
  }

  func closestPosition(to point: CGPoint) -> UITextPosition? { StreamTextPosition(layout.characterIndex(at: point)) }

  func closestPosition(to point: CGPoint, within range: UITextRange) -> UITextPosition? {
    guard let range = range as? StreamTextRange else { return nil }
    let at = layout.characterIndex(at: point)
    return StreamTextPosition(min(max(at, range.range.location), NSMaxRange(range.range)))
  }

  func characterRange(at point: CGPoint) -> UITextRange? {
    let at = layout.characterIndex(at: point)
    guard at < layout.length else { return nil }
    return StreamTextRange(string.rangeOfComposedCharacterSequence(at: at))
  }

  // Not editable.
  func replace(_ range: UITextRange, withText text: String) {}
  func setMarkedText(_ markedText: String?, selectedRange: NSRange) {}
  func unmarkText() {}
  func insertText(_ text: String) {}
  func deleteBackward() {}
}
