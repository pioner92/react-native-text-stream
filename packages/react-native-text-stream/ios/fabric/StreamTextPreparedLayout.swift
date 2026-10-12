import CoreText
import UIKit

/// A finished text laid out ahead, off the main thread (in the shadow node's measure, or on a
/// background queue once a stream finishes), so mounting it on the main thread only draws what is
/// visible: no text view, no layout. A rotation or a resize lays it out again on the main thread,
/// which is still cheaper than a text view. The lines are those the renderer's text view (TextKit 1)
/// would make, so swapping one in (for selection, a new width or a new style) keeps every line.
///
/// Two engines: CoreText (several times cheaper) for a fixed line height and a single style, where
/// every line has the same height and its breaks are TextKit's; TextKit 1 for everything else.
@objc(StreamTextPreparedLayout)
public final class StreamTextPreparedLayout: NSObject {
  #if STREAMTEXT_TESTING
  private static let flagsLock = NSLock()
  private static var flags = (enabled: true, coreText: true, generation: 0)
  /// Benchmarks: off = every mount lays out on the main thread, as before. Any thread.
  @objc public static var enabled: Bool {
    get { flagsLock.withLock { flags.enabled } }
    set { flagsLock.withLock { flags.enabled = newValue } }
  }
  /// Benchmarks: false = always TextKit. Any thread.
  @objc public static var coreText: Bool {
    get { flagsLock.withLock { flags.coreText } }
    set { flagsLock.withLock { flags.coreText = newValue } }
  }
  /// Benchmarks: bumping it drops every cached layout. Any thread.
  @objc public static var generation: Int {
    get { flagsLock.withLock { flags.generation } }
    set { flagsLock.withLock { flags.generation = newValue } }
  }
  /** Tests: the cache's size (set by StreamTextPrepared.mm, which owns the cache). */
  @objc public static var cacheCount: (() -> Int)?
  @objc public static var cacheClear: (() -> Void)?
  @objc public static var cacheDump: (() -> String)?
  /// Tests: layouts made on the main thread (a rotation, a resize) and the longest (s). Main thread.
  @objc public static var mainLayouts = 0
  @objc public static var mainLongest: Double = 0
  #else
  @objc public static var enabled: Bool { true }
  @objc public static var coreText: Bool { true }
  @objc public static var generation: Int { 0 }
  #endif
  /// Larger finished texts keep the text view path: they fill over several frames anyway.
  @objc public static let maxBytes: UInt64 = StreamTextLayout.instantFillBytes

  @objc public let width: CGFloat
  @objc public let height: CGFloat
  /// Buffer bytes the text was decoded from.
  @objc public let bytes: UInt64
  /// The text's color is drawn from the context: a new color needs no new layout.
  @objc public var colorIndependent: Bool { isCoreText }
  let text: NSAttributedString
  private let engine: Engine

  private enum Engine {
    case textKit(TextKitLines)
    case coreText(CoreTextLines)
  }

  @objc public init?(utf8: UnsafePointer<UInt8>, count: Int, bytes: UInt64, style: StreamTextStyle, width: CGFloat) {
    let started = CACurrentMediaTime()
    let string = streamTextDecodeUTF8(utf8, count, atStreamStart: true)
    let attributes = style.attributes()
    text = NSAttributedString(string: string, attributes: attributes)
    self.width = width
    self.bytes = bytes
    if Self.coreText, let lines = CoreTextLines(text: text, attributes: attributes, style: style, width: width) {
      engine = .coreText(lines)
      height = lines.height
    } else if let lines = TextKitLines(text: text, width: width) {
      engine = .textKit(lines)
      height = lines.height
    } else {
      return nil
    }
    super.init()
    StreamTextRenderStats.shared.addPhase(
      isCoreText ? "prepare.coreText" : "prepare.textKit", time: CACurrentMediaTime() - started)
  }

  @objc public var isCoreText: Bool {
    if case .coreText = engine { return true }
    return false
  }

  // MARK: Geometry for selection (main thread)

  var length: Int { text.length }

  /// The character boundary nearest to `point`.
  func characterIndex(at point: CGPoint) -> Int {
    switch engine {
    case .textKit(let lines): return lines.characterIndex(at: point)
    case .coreText(let lines): return lines.characterIndex(at: point, text: text.string as NSString)
    }
  }

  /// The caret before the character at `index` (`length`: after the last one).
  func caretRect(at index: Int) -> CGRect {
    switch engine {
    case .textKit(let lines): return lines.caretRect(at: index, length: length)
    case .coreText(let lines): return lines.caretRect(at: index, length: length)
    }
  }

  /// One rect per run of one direction on each line the range covers, and whether the run is right to left.
  func selectionRects(for range: NSRange) -> [(rect: CGRect, rightToLeft: Bool)] {
    guard range.length > 0 else { return [] }
    switch engine {
    case .textKit(let lines): return lines.selectionRects(for: range)
    // CoreText only takes left-to-right text (right-to-left goes to TextKit).
    case .coreText(let lines): return lines.selectionRects(for: range).map { ($0, false) }
    }
  }

  /// Each line's characters and box, made when accessibility first reads them. Main thread.
  private(set) lazy var lineBoxes: [(range: NSRange, frame: CGRect)] = {
    switch engine {
    case .textKit(let lines): return lines.lineBoxes()
    case .coreText(let lines): return lines.lineBoxes()
    }
  }()

  /// Whether the paragraph around `index` runs right to left.
  func isRightToLeft(at index: Int) -> Bool {
    if case .textKit(let lines) = engine { return lines.isRightToLeft(at: index, length: length) }
    return false
  }

  /// Draws the part of the text inside `rect` into the current UIKit context, in `color` where the
  /// engine takes it from the context. Any thread.
  func draw(_ rect: CGRect, color: UIColor) {
    switch engine {
    case .textKit(let lines): lines.draw(rect)
    case .coreText(let lines): lines.draw(rect, color: color)
    }
  }

  #if STREAMTEXT_TESTING
  /// Tests: how far CoreText's lines are from TextKit's for `text` (line ranges, baselines, glyph
  /// x positions, heights). `coreText` is 0 when CoreText doesn't take this text.
  @_spi(Testing) public static func compareEngines(text: String, style: StreamTextStyle, width: CGFloat) -> [String: Double] {
    let attributes = style.attributes()
    let attributed = NSAttributedString(string: text, attributes: attributes)
    guard let textKit = TextKitLines(text: attributed, width: width)?.geometry() else { return ["textKit": 0] }
    var out: [String: Double] = ["heightTextKit": Double(textKit.height), "linesTextKit": Double(textKit.ranges.count)]
    guard let coreText = CoreTextLines(text: attributed, attributes: attributes, style: style, width: width)?.geometry()
    else {
      out["coreText"] = 0
      return out
    }
    out["coreText"] = 1
    out["heightCoreText"] = Double(coreText.height)
    out["linesCoreText"] = Double(coreText.ranges.count)
    out["mismatchedLines"] = Double(zip(textKit.ranges, coreText.ranges).filter { $0 != $1 }.count
      + abs(textKit.ranges.count - coreText.ranges.count))
    // TextKit reports a glyph's y before the baseline offset it applies when drawing: compare how the
    // baselines move from line to line (a fallback font shifting one line), not where they start.
    let deltas = zip(textKit.baselines, coreText.baselines).filter { !$0.0.isNaN }.map { Double($0 - $1) }
    out["baselineSpread"] = (deltas.max() ?? 0) - (deltas.min() ?? 0)
    var maxDx = 0.0
    var compared = 0
    for (index, x) in textKit.glyphX {
      guard let other = coreText.glyphX[index] else { continue }
      maxDx = max(maxDx, Double(abs(x - other)))
      compared += 1
    }
    out["maxGlyphDx"] = maxDx
    out["comparedGlyphs"] = Double(compared)
    return out
  }
  #endif
}

/// Line geometry, for comparing the engines.
private struct LineGeometry {
  var ranges: [NSRange] = []
  var baselines: [CGFloat] = []
  /// x of the first glyph of each character that has one.
  var glyphX: [Int: CGFloat] = [:]
  var height: CGFloat = 0
}

/// TextKit 1, set up as the renderer's text view.
private final class TextKitLines {
  private let storage = NSTextStorage()
  private let layoutManager = NSLayoutManager()
  private let container: NSTextContainer
  /// Tiles may draw off the main thread; NSLayoutManager is not safe for concurrent use.
  private let lock = NSLock()
  private let lineHeight: CGFloat
  let height: CGFloat

  /// nil when the text doesn't fit the container (a huge line height times many lines).
  init?(text: NSAttributedString, width: CGFloat) {
    container = NSTextContainer(size: CGSize(width: width, height: StreamTextLayout.textViewHeight))
    container.lineFragmentPadding = 0
    layoutManager.allowsNonContiguousLayout = false
    layoutManager.usesFontLeading = false
    storage.addLayoutManager(layoutManager)
    layoutManager.addTextContainer(container)
    storage.setAttributedString(text)
    layoutManager.ensureLayout(for: container)
    guard layoutManager.characterRange(forGlyphRange: layoutManager.glyphRange(for: container), actualGlyphRange: nil)
      .length >= text.length else { return nil }
    height = text.length == 0 ? 0 : ceil(layoutManager.usedRect(for: container).height)
    lineHeight = (text.length > 0 ? text.attribute(.font, at: 0, effectiveRange: nil) as? UIFont : nil)?.lineHeight ?? 20
  }

  func draw(_ rect: CGRect) {
    lock.withLock {
      // A line's ink can reach past its box (emoji, stacked marks): take the lines around the rect too.
      let glyphs = layoutManager.glyphRange(forBoundingRect: rect.insetBy(dx: 0, dy: -2 * lineHeight), in: container)
      layoutManager.drawBackground(forGlyphRange: glyphs, at: .zero)
      layoutManager.drawGlyphs(forGlyphRange: glyphs, at: .zero)
    }
  }

  func characterIndex(at point: CGPoint) -> Int {
    lock.withLock {
      let length = storage.length
      let extra = layoutManager.extraLineFragmentRect
      if length == 0 || (!extra.isEmpty && point.y >= extra.minY) { return length }
      // The nearest of the line's insertion points, the ones carets are drawn at: a caret's own
      // position maps back to its index in right-to-left runs too (the selection UI relies on it).
      let glyph = layoutManager.glyphIndex(for: point, in: container)
      let line = layoutManager.lineFragmentRect(forGlyphAt: glyph, effectiveRange: nil)
      let character = layoutManager.characterIndexForGlyph(at: glyph)
      let count = layoutManager.getLineFragmentInsertionPoints(
        forCharacterAt: character, alternatePositions: false, inDisplayOrder: false, positions: nil, characterIndexes: nil)
      if count > 0 {
        var positions = [CGFloat](repeating: 0, count: count)
        var indexes = [Int](repeating: 0, count: count)
        _ = layoutManager.getLineFragmentInsertionPoints(
          forCharacterAt: character, alternatePositions: false, inDisplayOrder: false, positions: &positions,
          characterIndexes: &indexes)
        let x = point.x - line.minX
        if let nearest = positions.indices.min(by: { abs(positions[$0] - x) < abs(positions[$1] - x) }) {
          return min(indexes[nearest], length)
        }
      }
      var fraction: CGFloat = 0
      let index = layoutManager.characterIndex(for: point, in: container, fractionOfDistanceBetweenInsertionPoints: &fraction)
      return fraction > 0.5 ? min(index + 1, length) : index
    }
  }

  func caretRect(at index: Int, length: Int) -> CGRect {
    lock.withLock {
      if index >= length, !layoutManager.extraLineFragmentRect.isEmpty {
        return CGRect(origin: layoutManager.extraLineFragmentRect.origin,
                      size: CGSize(width: 2, height: layoutManager.extraLineFragmentRect.height))
      }
      let character = min(index, max(length - 1, 0))
      let glyph = layoutManager.glyphIndexForCharacter(at: character)
      let line = layoutManager.lineFragmentRect(forGlyphAt: glyph, effectiveRange: nil)
      // TextKit's own insertion points: in right-to-left runs the caret before a character is on its right.
      if let x = insertionX(of: index, onLineOf: character) {
        return CGRect(x: line.minX + x, y: line.minY, width: 2, height: line.height)
      }
      let box = layoutManager.boundingRect(forGlyphRange: NSRange(location: glyph, length: 1), in: container)
      return CGRect(x: index >= length ? box.maxX : box.minX, y: line.minY, width: 2, height: line.height)
    }
  }

  /// Under `lock`.
  private func insertionX(of index: Int, onLineOf character: Int) -> CGFloat? {
    let count = layoutManager.getLineFragmentInsertionPoints(
      forCharacterAt: character, alternatePositions: false, inDisplayOrder: false, positions: nil, characterIndexes: nil)
    guard count > 0 else { return nil }
    var positions = [CGFloat](repeating: 0, count: count)
    var indexes = [Int](repeating: 0, count: count)
    _ = layoutManager.getLineFragmentInsertionPoints(
      forCharacterAt: character, alternatePositions: false, inDisplayOrder: false, positions: &positions,
      characterIndexes: &indexes)
    return indexes.firstIndex(of: index).map { positions[$0] }
  }

  /// One rect per run of one direction on each line, and whether the run is right to left.
  func selectionRects(for range: NSRange) -> [(rect: CGRect, rightToLeft: Bool)] {
    lock.withLock {
      let glyphs = layoutManager.glyphRange(forCharacterRange: range, actualCharacterRange: nil)
      var rects: [CGRect] = []
      layoutManager.enumerateEnclosingRects(
        forGlyphRange: glyphs, withinSelectedGlyphRange: glyphs, in: container) { rect, _ in rects.append(rect) }
      return rects.map { rect in
        let glyph = layoutManager.glyphIndex(for: CGPoint(x: rect.midX, y: rect.midY), in: container)
        return (rect, bidiLevel(of: glyph) & 1 == 1)
      }
    }
  }

  /// Each line's characters and box.
  func lineBoxes() -> [(range: NSRange, frame: CGRect)] {
    lock.withLock {
      var boxes: [(range: NSRange, frame: CGRect)] = []
      layoutManager.enumerateLineFragments(forGlyphRange: layoutManager.glyphRange(for: container)) { _, used, _, glyphs, _ in
        boxes.append((self.layoutManager.characterRange(forGlyphRange: glyphs, actualGlyphRange: nil), used))
      }
      return boxes
    }
  }

  /// Whether the paragraph around `index` runs right to left: the lowest bidi level on its line is odd.
  func isRightToLeft(at index: Int, length: Int) -> Bool {
    guard length > 0 else { return false }
    return lock.withLock {
      var lineGlyphs = NSRange()
      _ = layoutManager.lineFragmentRect(
        forGlyphAt: layoutManager.glyphIndexForCharacter(at: min(index, length - 1)), effectiveRange: &lineGlyphs)
      var levels = [UInt8](repeating: 0, count: lineGlyphs.length)
      _ = layoutManager.getGlyphs(in: lineGlyphs, glyphs: nil, properties: nil, characterIndexes: nil, bidiLevels: &levels)
      return (levels.min() ?? 0) & 1 == 1
    }
  }

  /// Under `lock`.
  private func bidiLevel(of glyph: Int) -> UInt8 {
    var level: UInt8 = 0
    _ = layoutManager.getGlyphs(in: NSRange(location: glyph, length: 1), glyphs: nil, properties: nil,
                                characterIndexes: nil, bidiLevels: &level)
    return level
  }

  func geometry() -> LineGeometry {
    var geometry = LineGeometry(height: height)
    lock.withLock {
      let all = layoutManager.glyphRange(for: container)
      layoutManager.enumerateLineFragments(forGlyphRange: all) { rect, _, _, glyphs, _ in
        geometry.ranges.append(self.layoutManager.characterRange(forGlyphRange: glyphs, actualGlyphRange: nil))
        var baseline = CGFloat.nan // an empty line has no glyph to put on it
        for glyph in glyphs.location..<NSMaxRange(glyphs) {
          let property = self.layoutManager.propertyForGlyph(at: glyph)
          if property.contains(.null) || property.contains(.controlCharacter) { continue }
          if baseline.isNaN { baseline = rect.minY + self.layoutManager.location(forGlyphAt: glyph).y }
          let character = self.layoutManager.characterIndexForGlyph(at: glyph)
          if geometry.glyphX[character] == nil {
            geometry.glyphX[character] = rect.minX + self.layoutManager.location(forGlyphAt: glyph).x
          }
        }
        geometry.baselines.append(baseline)
      }
    }
    return geometry
  }
}

/// CoreText lines in TextKit's geometry: every line `lineHeight` tall, the glyphs on the baseline
/// TextKit uses (the extra height above them, then the style's baseline offset). Immutable once
/// made, so tiles may draw them on any thread.
private final class CoreTextLines {
  /// Past this many UTF-16 units in one paragraph, CoreText stops kerning while TextKit keeps it.
  private static let maxParagraphLength = 8000
  /// Paragraphs share a typesetter up to this many UTF-16 units (well below the kerning limit).
  private static let chunkLength = 4000
  /// Shorter texts typeset on one core: spreading them costs more than it saves.
  private static let parallelLength = 2048

  private let lines: [CTLine]
  /// Each line's characters in the whole text.
  private let ranges: [NSRange]
  private let penOffsets: [CGFloat]
  private let lineHeight: CGFloat
  private let baseline: CGFloat
  let height: CGFloat

  /// nil when TextKit's geometry can't be reproduced: no fixed line height, justified text, or
  /// text that TextKit lays out by rules we don't follow.
  init?(text: NSAttributedString, attributes: [NSAttributedString.Key: Any], style: StreamTextStyle, width: CGFloat) {
    guard style.lineHeight > 0, let font = attributes[.font] as? UIFont,
          let paragraph = attributes[.paragraphStyle] as? NSParagraphStyle,
          paragraph.alignment != .justified else { return nil }
    let string = text.string as NSString
    // Right-to-left paragraphs, tab stops and ideographic spaces follow TextKit rules we don't reproduce.
    for index in 0..<string.length {
      let unit = string.character(at: index)
      if unit == 0x09 || unit == 0x3000 || (0x0590...0x08FF).contains(unit) || (0xFB1D...0xFDFF).contains(unit)
        || (0xFE70...0xFEFF).contains(unit) || unit == 0x200F || unit == 0x202B || unit == 0x202E {
        return nil
      }
    }
    lineHeight = style.lineHeight
    baseline = style.lineHeight + font.descender - ((attributes[.baselineOffset] as? CGFloat) ?? 0)
    // The color comes from the context at draw time.
    var typesetAttributes: [NSAttributedString.Key: Any] = [
      .font: font,
      NSAttributedString.Key(kCTForegroundColorFromContextAttributeName as String): true,
    ]
    if let kern = attributes[.kern] { typesetAttributes[.kern] = kern }
    let source = NSAttributedString(string: text.string, attributes: typesetAttributes)
    let flush: CGFloat
    switch paragraph.alignment {
    case .center: flush = 0.5
    case .right: flush = 1
    default: flush = 0
    }
    let length = string.length
    // Typesetters over runs of whole paragraphs: over a long text CoreText stops kerning, and one
    // per paragraph costs more than the typesetting itself.
    var paragraphs: [NSRange] = []
    var paragraphStart = 0
    while paragraphStart < length {
      let paragraphRange = string.paragraphRange(for: NSRange(location: paragraphStart, length: 0))
      guard paragraphRange.length <= Self.maxParagraphLength else { return nil }
      if let last = paragraphs.last, last.length + paragraphRange.length <= Self.chunkLength {
        paragraphs[paragraphs.count - 1].length += paragraphRange.length
      } else {
        paragraphs.append(paragraphRange)
      }
      paragraphStart = NSMaxRange(paragraphRange)
    }
    // The runs are independent: a long text typesets them on several cores.
    var typeset = [(lines: [CTLine], ranges: [NSRange], penOffsets: [CGFloat])](
      repeating: ([], [], []), count: paragraphs.count)
    let lay = { (index: Int) -> (lines: [CTLine], ranges: [NSRange], penOffsets: [CGFloat]) in
      let paragraphRange = paragraphs[index]
      let typesetter = CTTypesetterCreateWithAttributedString(source.attributedSubstring(from: paragraphRange))
      var out: (lines: [CTLine], ranges: [NSRange], penOffsets: [CGFloat]) = ([], [], [])
      var start = 0
      while start < paragraphRange.length {
        var count = max(CTTypesetterSuggestLineBreak(typesetter, start, Double(width)), 1)
        // TextKit keeps the spaces after a break on the line they follow; CoreText may start the
        // next line with them (after CJK punctuation).
        while start + count < paragraphRange.length,
              string.character(at: paragraphRange.location + start + count) == 0x20 { count += 1 }
        let line = CTTypesetterCreateLine(typesetter, CFRange(location: start, length: count))
        out.lines.append(line)
        out.ranges.append(NSRange(location: paragraphRange.location + start, length: count))
        out.penOffsets.append(flush == 0 ? 0 : CGFloat(CTLineGetPenOffsetForFlush(line, flush, Double(width))))
        start += count
      }
      return out
    }
    if length >= Self.parallelLength && paragraphs.count > 1 {
      typeset.withUnsafeMutableBufferPointer { results in
        DispatchQueue.concurrentPerform(iterations: paragraphs.count) { results[$0] = lay($0) }
      }
    } else {
      for index in paragraphs.indices { typeset[index] = lay(index) }
    }
    let lines = typeset.flatMap(\.lines)
    let ranges = typeset.flatMap(\.ranges)
    let penOffsets = typeset.flatMap(\.penOffsets)
    self.lines = lines
    self.ranges = ranges
    self.penOffsets = penOffsets
    // Like TextKit's used rect: a text ending in a line break has an empty last line.
    let endsWithBreak = length > 0 && [0x0A, 0x0D, 0x2028, 0x2029].contains(string.character(at: length - 1))
    height = length == 0 ? 0 : CGFloat(lines.count + (endsWithBreak ? 1 : 0)) * lineHeight
  }

  func draw(_ rect: CGRect, color: UIColor) {
    guard let context = UIGraphicsGetCurrentContext(), !lines.isEmpty else { return }
    // A line's ink can reach past its box (emoji, stacked marks): take the lines around the rect too.
    let first = max(Int(floor(rect.minY / lineHeight)) - 1, 0)
    let last = min(Int(ceil(rect.maxY / lineHeight)) + 1, lines.count - 1)
    guard first <= last else { return }
    context.saveGState()
    context.setFillColor(color.cgColor)
    context.textMatrix = .identity
    // UIKit's context is flipped; CoreText draws upward from the baseline.
    context.scaleBy(x: 1, y: -1)
    for index in first...last {
      context.textPosition = CGPoint(x: penOffsets[index], y: -(CGFloat(index) * lineHeight + baseline))
      CTLineDraw(lines[index], context)
    }
    context.restoreGState()
  }

  /// Each line's characters and box.
  func lineBoxes() -> [(range: NSRange, frame: CGRect)] {
    lines.indices.map { line in
      let width = CGFloat(CTLineGetTypographicBounds(lines[line], nil, nil, nil))
      return (ranges[line], CGRect(x: penOffsets[line], y: CGFloat(line) * lineHeight, width: max(width, 1),
                                   height: lineHeight))
    }
  }

  /// The line holding `index`; past the last character, the last line.
  private func lineIndex(of index: Int) -> Int {
    var low = 0
    var high = ranges.count - 1
    while low < high {
      let mid = (low + high + 1) / 2
      if ranges[mid].location <= index { low = mid } else { high = mid - 1 }
    }
    return low
  }

  /// x of the boundary before `index` (a character of line `line`, or its end).
  private func offset(_ index: Int, line: Int) -> CGFloat {
    let ctLine = lines[line]
    let relative = CTLineGetStringRange(ctLine).location + (index - ranges[line].location)
    return penOffsets[line] + CGFloat(CTLineGetOffsetForStringIndex(ctLine, relative, nil))
  }

  func characterIndex(at point: CGPoint, text: NSString) -> Int {
    guard !lines.isEmpty else { return 0 }
    let line = min(max(Int(floor(point.y / lineHeight)), 0), lines.count - 1)
    let ctLine = lines[line]
    let relative = CTLineGetStringIndexForPosition(ctLine, CGPoint(x: point.x - penOffsets[line], y: 0))
    var index = ranges[line].location + (relative - CTLineGetStringRange(ctLine).location)
    index = min(max(index, ranges[line].location), NSMaxRange(ranges[line]))
    // Never after a line's own line break: that is the start of the next line.
    if index == NSMaxRange(ranges[line]), index > ranges[line].location,
       [0x0A, 0x0D, 0x2028, 0x2029].contains(text.character(at: index - 1)) {
      index -= 1
    }
    return index
  }

  func caretRect(at index: Int, length: Int) -> CGRect {
    guard !lines.isEmpty else { return CGRect(x: 0, y: 0, width: 2, height: lineHeight) }
    // After a final line break the caret sits on the empty last line.
    if index >= length, CGFloat(lines.count) * lineHeight < height {
      return CGRect(x: 0, y: CGFloat(lines.count) * lineHeight, width: 2, height: lineHeight)
    }
    let line = lineIndex(of: min(index, length))
    return CGRect(x: offset(min(index, NSMaxRange(ranges[line])), line: line), y: CGFloat(line) * lineHeight,
                  width: 2, height: lineHeight)
  }

  func selectionRects(for range: NSRange) -> [CGRect] {
    guard !lines.isEmpty else { return [] }
    var rects: [CGRect] = []
    for line in lineIndex(of: range.location)...lineIndex(of: max(NSMaxRange(range) - 1, range.location)) {
      let start = max(range.location, ranges[line].location)
      let end = min(NSMaxRange(range), NSMaxRange(ranges[line]))
      guard end > start else { continue }
      let x0 = offset(start, line: line)
      let x1 = offset(end, line: line)
      rects.append(CGRect(x: min(x0, x1), y: CGFloat(line) * lineHeight, width: max(abs(x1 - x0), 1), height: lineHeight))
    }
    return rects
  }

  func geometry() -> LineGeometry {
    var geometry = LineGeometry(height: height)
    for (index, line) in lines.enumerated() {
      geometry.ranges.append(ranges[index])
      geometry.baselines.append(CGFloat(index) * lineHeight + baseline)
      // Run string indices are relative to the paragraph's typesetter.
      let lineStart = CTLineGetStringRange(line).location
      for run in CTLineGetGlyphRuns(line) as! [CTRun] {
        let count = CTRunGetGlyphCount(run)
        var positions = [CGPoint](repeating: .zero, count: count)
        var indices = [CFIndex](repeating: 0, count: count)
        CTRunGetPositions(run, CFRange(location: 0, length: 0), &positions)
        CTRunGetStringIndices(run, CFRange(location: 0, length: 0), &indices)
        for (position, stringIndex) in zip(positions, indices) {
          let character = ranges[index].location + (stringIndex - lineStart)
          if geometry.glyphX[character] == nil { geometry.glyphX[character] = penOffsets[index] + position.x }
        }
      }
    }
    return geometry
  }
}

/// Set on the main thread, read by background tile draws: a stale read only draws one tile in vain.
/// The canvas is read on the main thread only, so a background draw never holds it (its deinit
/// stays on the main thread).
private final class DrawToken: @unchecked Sendable {
  var cancelled = false
  weak var canvas: StreamTextCanvas?
  init(_ canvas: StreamTextCanvas?) { self.canvas = canvas }
}

/// Shows a prepared layout as image tiles: those on screen are drawn right away on the main thread
/// (so the first frame and every scrolled-to frame already have the text), those just past the
/// screen are drawn ahead in the background, and far ones are dropped. A tall canvas follows the
/// nearest scroll view.
final class StreamTextCanvas: UIView {
  private static let accessibleTailCharacters = 2000
  private static let tileHeight: CGFloat = 256
  /// Drawn ahead past each edge of the screen; tiles beyond twice this are dropped.
  private static let prefetch: CGFloat = 512
  private static let drawQueue = DispatchQueue(label: "StreamText.tiles", qos: .userInitiated)

  private(set) var layout: StreamTextPreparedLayout
  /// The text color, for engines that take it at draw time.
  var color: UIColor {
    didSet { if color != oldValue { redraw() } }
  }
  var visibleBounds: (() -> CGRect)?

  private var tiles: [Int: CALayer] = [:]
  /// The canvases alive, trimmed on a memory warning. Main thread.
  static let all = NSHashTable<StreamTextCanvas>.weakObjects()
  #if STREAMTEXT_TESTING
  /// Tests: bytes of every canvas's tiles. Main thread.
  static var tileBytes = 0
  #endif
  /// Tiles being drawn in the background; a redraw or leaving the window cancels them.
  private var pending = Set<Int>()
  private lazy var drawToken = DrawToken(self)
  private var scrollObservation: NSKeyValueObservation?

  // Selection (StreamTextSelection.swift).
  var selection = NSRange(location: 0, length: 0)
  @objc weak var inputDelegate: UITextInputDelegate?
  lazy var textTokenizer: UITextInputTokenizer = UITextInputStringTokenizer(textInput: self)
  private var textInteraction: UITextInteraction?

  /// Adds or removes the system's selection gestures.
  var isSelectable = false {
    didSet {
      guard isSelectable != oldValue else { return }
      if isSelectable {
        let interaction = UITextInteraction(for: .nonEditable)
        interaction.textInput = self
        addInteraction(interaction)
        textInteraction = interaction
      } else if let textInteraction {
        if isFirstResponder { _ = resignFirstResponder() }
        removeInteraction(textInteraction)
        self.textInteraction = nil
      }
    }
  }

  init(layout: StreamTextPreparedLayout, color: UIColor) {
    self.layout = layout
    self.color = color
    super.init(frame: CGRect(x: 0, y: 0, width: layout.width, height: layout.height))
    Self.all.add(self)
    backgroundColor = .clear
    isOpaque = false
    if #available(iOS 17.0, *) {
      registerForTraitChanges([UITraitUserInterfaceStyle.self, UITraitDisplayScale.self]) { (self: Self, _) in
        self.redraw()
      }
    }
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

  #if STREAMTEXT_TESTING
  deinit {
    MainActor.assumeIsolated { for row in Array(tiles.keys) { drop(row) } }
  }
  #endif

  /// A new layout of the same text (another width or style): redraws what is on screen at once.
  func replace(_ layout: StreamTextPreparedLayout) {
    if selection.length > 0 || isFirstResponder { _ = resignFirstResponder() }
    self.layout = layout
    frame = CGRect(x: 0, y: 0, width: layout.width, height: layout.height)
    redraw()
  }

  /// Under memory pressure, canvases keep only what is on screen.
  private static let trimOnMemoryWarning: NSObjectProtocol = NotificationCenter.default.addObserver(
    forName: UIApplication.didReceiveMemoryWarningNotification, object: nil, queue: .main
  ) { _ in
    MainActor.assumeIsolated {
      for canvas in all.allObjects {
        canvas.cancelDrawing()
        let visible = canvas.window == nil ? .null : canvas.visibleRect()
        canvas.trim(to: visible)
        canvas.leanUntilMoved = visible
      }
    }
  }
  /// After a memory warning, what the screen showed then: nothing is drawn ahead until that changes.
  private var leanUntilMoved: CGRect?

  override func willMove(toWindow newWindow: UIWindow?) {
    super.willMove(toWindow: newWindow)
    _ = Self.trimOnMemoryWarning
    // Covered by another screen or tab: what was on screen stays, to show at once on the way back.
    if newWindow == nil { trim(to: visibleRect()) }
  }

  private func trim(to rect: CGRect) {
    for (row, tile) in tiles where !tile.frame.intersects(rect) { drop(row) }
  }

  override func didMoveToWindow() {
    super.didMoveToWindow()
    scrollObservation = nil
    guard window != nil else {
      cancelDrawing()
      return
    }
    // Follows the scroll view it is in: a bubble mounted off screen draws once it scrolls in.
    if let scrollView = nearestScrollView() {
      scrollObservation = scrollView.observe(\.contentOffset) { [weak self] _, _ in
        // KVO fires on the main thread for UIKit scrolling.
        MainActor.assumeIsolated { self?.updateTiles() }
      }
    }
    updateTiles()
  }

  override func layoutSubviews() {
    super.layoutSubviews()
    updateTiles()
  }

  override func traitCollectionDidChange(_ previous: UITraitCollection?) {
    super.traitCollectionDidChange(previous)
    if #available(iOS 17.0, *) { return }
    if traitCollection.hasDifferentColorAppearance(comparedTo: previous)
      || traitCollection.displayScale != previous?.displayScale {
      redraw()
    }
  }

  /// Drops every tile and draws the visible ones again now: never blank, never stale.
  private func redraw() {
    cancelDrawing()
    for row in Array(tiles.keys) { drop(row) }
    updateTiles()
  }

  /// Benchmarks and tests: tiles currently drawn.
  var tileCount: Int { tiles.count }

  /// Draws missing tiles on screen now, those just past it in the background; drops far ones.
  func updateTiles() {
    guard window != nil, layout.height > 0 else { return }
    let visible = visibleRect()
    if let lean = leanUntilMoved, lean != visible { leanUntilMoved = nil }
    let ahead = leanUntilMoved == nil ? Self.prefetch : 0
    let near = visible.isEmpty ? visible : visible.insetBy(dx: 0, dy: -ahead).intersection(bounds)
    let keep = visible.isEmpty ? visible : visible.insetBy(dx: 0, dy: -2 * Self.prefetch)
    for (row, tile) in tiles where !tile.frame.intersects(keep) { drop(row) }
    guard !near.isEmpty else { return }
    let started = CACurrentMediaTime()
    var drewNow = false
    for row in rows(in: near) where tiles[row] == nil {
      let rect = tileRect(row)
      if rect.intersects(visible) {
        pending.remove(row)
        install(row, image: Self.render(rect, layout: layout, color: color, traits: traitCollection))
        drewNow = true
      } else if !pending.contains(row) {
        drawAhead(row)
      }
    }
    if drewNow { StreamTextRenderStats.shared.addPhase("canvas.tiles", time: CACurrentMediaTime() - started) }
  }

  private func cancelDrawing() {
    drawToken.cancelled = true
    drawToken = DrawToken(self)
    pending.removeAll()
  }

  private func drawAhead(_ row: Int) {
    pending.insert(row)
    let rect = tileRect(row)
    let traits = traitCollection
    let token = drawToken
    let (layout, color) = (layout, color)
    Self.drawQueue.async {
      // Skipped once scrolled past, redrawn or unmounted: the queue only holds what is still wanted.
      guard !token.cancelled else { return }
      let image = Self.render(rect, layout: layout, color: color, traits: traits)
      DispatchQueue.main.async {
        guard let self = token.canvas, !token.cancelled, self.pending.remove(row) != nil, self.tiles[row] == nil else { return }
        self.install(row, image: image)
      }
    }
  }

  private func install(_ row: Int, image: CGImage?) {
    let tile = CALayer()
    tile.frame = tileRect(row)
    tile.contentsScale = traitCollection.displayScale
    tile.contents = image
    layer.addSublayer(tile)
    tiles[row] = tile
    #if STREAMTEXT_TESTING
    Self.tileBytes += Self.bytes(of: tile)
    #endif
  }

  private func drop(_ row: Int) {
    guard let tile = tiles.removeValue(forKey: row) else { return }
    tile.removeFromSuperlayer()
    #if STREAMTEXT_TESTING
    Self.tileBytes -= Self.bytes(of: tile)
    #endif
  }

  #if STREAMTEXT_TESTING
  private static func bytes(of tile: CALayer) -> Int {
    guard let contents = tile.contents else { return 0 }
    let image = contents as! CGImage
    return image.bytesPerRow * image.height
  }
  #endif

  /// Any thread.
  private static func render(_ rect: CGRect, layout: StreamTextPreparedLayout, color: UIColor,
                             traits: UITraitCollection) -> CGImage? {
    let format = UIGraphicsImageRendererFormat(for: traits)
    format.opaque = false
    format.preferredRange = .standard
    let color = color.resolvedColor(with: traits)
    return UIGraphicsImageRenderer(size: rect.size, format: format).image { context in
      context.cgContext.translateBy(x: -rect.minX, y: -rect.minY)
      traits.performAsCurrent { layout.draw(rect, color: color) }
    }.cgImage
  }

  private func tileRect(_ row: Int) -> CGRect {
    CGRect(x: 0, y: CGFloat(row) * Self.tileHeight, width: bounds.width, height: Self.tileHeight).intersection(bounds)
  }

  private func rows(in rect: CGRect) -> ClosedRange<Int> {
    Int(rect.minY / Self.tileHeight)...Int(max(rect.maxY - 1, rect.minY) / Self.tileHeight)
  }

  /// What the window shows of the canvas: ancestors that clip (a bubble, a list) cut it too.
  private func visibleRect() -> CGRect {
    guard let window else { return .null }
    var visible = bounds.intersection(convert(window.bounds, from: window))
    var ancestor = superview
    while let view = ancestor, view !== window, !visible.isEmpty {
      if view.clipsToBounds { visible = visible.intersection(convert(view.bounds, from: view)) }
      ancestor = view.superview
    }
    return visible.isEmpty ? .null : visible
  }

  private func nearestScrollView() -> UIScrollView? {
    var ancestor = superview
    while let view = ancestor {
      if let scrollView = view as? UIScrollView { return scrollView }
      ancestor = view.superview
    }
    return nil
  }

  // Accessibility: one static text whose value is what the screen shows of it (a snapshot never holds
  // the whole of a long text), and the whole text line by line through UIAccessibilityReadingContent.

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
      if let visible = visibleText() { return visible }
      // Off screen: the end, as before.
      let text = layout.text.string as NSString
      let length = text.length
      guard length > Self.accessibleTailCharacters else { return text as String }
      let start = text.rangeOfComposedCharacterSequence(at: length - Self.accessibleTailCharacters).location
      return "…" + text.substring(from: start)
    }
    set {}
  }

  override var accessibilityFrame: CGRect {
    get {
      let visible = visibleRect()
      if !visible.isNull { return UIAccessibility.convertToScreenCoordinates(visible, in: self) }
      guard let superview, let bounds = visibleBounds?() else { return super.accessibilityFrame }
      return UIAccessibility.convertToScreenCoordinates(bounds, in: superview)
    }
    set {}
  }

  /// The lines the screen shows, whole.
  fileprivate func visibleText() -> String? {
    let visible = visibleRect()
    let lines = layout.lineBoxes
    guard !visible.isNull,
          let first = lines.firstIndex(where: { $0.frame.maxY > visible.minY }),
          let last = lines.lastIndex(where: { $0.frame.minY < visible.maxY }), first <= last else { return nil }
    let start = lines[first].range.location
    return (layout.text.string as NSString).substring(with: NSRange(location: start, length: NSMaxRange(lines[last].range) - start))
  }
}

extension StreamTextCanvas: UIAccessibilityReadingContent {
  /// `point` is in screen coordinates, as the frames below.
  func accessibilityLineNumber(for point: CGPoint) -> Int {
    let lines = layout.lineBoxes
    guard !lines.isEmpty, let screen = window?.screen else { return NSNotFound }
    let y = convert(point, from: screen.coordinateSpace).y
    return lines.firstIndex { $0.frame.maxY > y } ?? lines.count - 1
  }

  func accessibilityContent(forLineNumber lineNumber: Int) -> String? {
    let lines = layout.lineBoxes
    guard lines.indices.contains(lineNumber) else { return nil }
    return (layout.text.string as NSString).substring(with: lines[lineNumber].range)
      .trimmingCharacters(in: .newlines)
  }

  func accessibilityFrame(forLineNumber lineNumber: Int) -> CGRect {
    let lines = layout.lineBoxes
    guard lines.indices.contains(lineNumber) else { return .zero }
    return UIAccessibility.convertToScreenCoordinates(lines[lineNumber].frame, in: self)
  }

  func accessibilityPageContent() -> String? { visibleText() }
}
