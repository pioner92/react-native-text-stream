import UIKit
import CoreText

func streamTextMarkdownAccessibleText(_ text: NSAttributedString, range: NSRange) -> String {
  let raw = text.string as NSString
  var cursor = range.location
  var value = ""
  text.enumerateAttribute(.attachment, in: range) { attachment, attachmentRange, _ in
    guard attachmentRange.location >= cursor else { return }
    if attachmentRange.location > cursor {
      value += raw.substring(with: NSRange(location: cursor, length: attachmentRange.location - cursor))
    }
    var nextCursor = NSMaxRange(attachmentRange)
    switch attachment {
    case let task as StreamTextMarkdownTaskAttachment:
      value += task.checked ? "Checked task " : "Unchecked task "
      // The three zero-width UTF-16 units retain source offsets after the four-byte `[x] `
      // marker is replaced by a single attachment. They are presentation padding, not words.
      nextCursor = min(NSMaxRange(range), NSMaxRange(attachmentRange) + 3)
    case let image as StreamTextMarkdownImageAttachment:
      value += image.block.alt.isEmpty ? "Image" : image.block.alt
    case let table as StreamTextMarkdownTableAttachment:
      for (index, cell) in table.block.cells.enumerated() {
        if index > 0 { value += index % max(1, table.block.columns) == 0 ? "\n" : " | " }
        value += cell.text
      }
    default:
      value += raw.substring(with: attachmentRange)
    }
    cursor = nextCursor
  }
  if cursor < NSMaxRange(range) {
    value += raw.substring(with: NSRange(location: cursor, length: NSMaxRange(range) - cursor))
  }
  // U+200B is an internal UTF-16 offset placeholder for task attachments; VoiceOver must never
  // receive it even when an attributed-range boundary changes during TextKit replacement.
  return value.replacingOccurrences(of: "\u{200B}", with: "")
}

/// One small attachment rather than four styled marker glyphs. The source remains in the stream
/// buffer; this is only the native presentation of MD4C's generated `[x] `/`[ ] ` task prefix.
final class StreamTextMarkdownTaskAttachment: NSTextAttachment {
  private static let imageLock = NSLock()
  private static var images: [String: UIImage] = [:]
  let checked: Bool
  init(checked: Bool, font: UIFont, foreground: UIColor) {
    self.checked = checked
    super.init(data: nil, ofType: nil)
    let edge = max(13, min(18, round(font.pointSize)))
    bounds = CGRect(x: 0, y: -1, width: edge + 4, height: edge)
    image = Self.cachedImage(checked: checked, edge: edge, foreground: foreground)
  }
  required init?(coder: NSCoder) { nil }
  override func attachmentBounds(for textContainer: NSTextContainer?, proposedLineFragment lineFrag: CGRect,
                                 glyphPosition position: CGPoint, characterIndex charIndex: Int) -> CGRect {
    bounds
  }
  private static func cachedImage(checked: Bool, edge: CGFloat, foreground: UIColor) -> UIImage {
    let resolved = foreground.resolvedColor(with: UITraitCollection.current)
    var red: CGFloat = 0, green: CGFloat = 0, blue: CGFloat = 0, alpha: CGFloat = 0
    let hasRGB = resolved.getRed(&red, green: &green, blue: &blue, alpha: &alpha)
    let key = hasRGB
      ? "\(checked ? 1 : 0):\(Int(edge.rounded())):\(Int((red * 255).rounded())):\(Int((green * 255).rounded())):\(Int((blue * 255).rounded())):\(Int((alpha * 255).rounded())):\(UIScreen.main.scale)"
      : "\(checked ? 1 : 0):\(Int(edge.rounded())):fallback:\(UIScreen.main.scale)"
    if let existing = imageLock.withLock({ images[key] }) { return existing }
    let rendered = image(checked: checked, edge: edge, foreground: hasRGB ? UIColor(red: red, green: green, blue: blue, alpha: alpha) : .label)
    return imageLock.withLock {
      // Two states times a small number of Dynamic Type sizes: keep this cache bounded.
      if images.count >= 16 { images.removeAll(keepingCapacity: true) }
      images[key] = rendered
      return rendered
    }
  }
  private static func image(checked: Bool, edge: CGFloat, foreground: UIColor) -> UIImage {
    let side = edge + 4
    let format = UIGraphicsImageRendererFormat(); format.scale = UIScreen.main.scale
    return UIGraphicsImageRenderer(size: CGSize(width: side, height: edge), format: format).image { context in
      let rect = CGRect(x: 1, y: 1, width: edge - 2, height: edge - 2)
      let path = UIBezierPath(roundedRect: rect, cornerRadius: max(2, edge * 0.2))
      if checked {
        UIColor.systemGreen.setFill(); path.fill()
        UIColor.white.setStroke()
        let check = UIBezierPath()
        check.move(to: CGPoint(x: edge * 0.23, y: edge * 0.51))
        check.addLine(to: CGPoint(x: edge * 0.43, y: edge * 0.70))
        check.addLine(to: CGPoint(x: edge * 0.78, y: edge * 0.30))
        check.lineWidth = max(1.5, edge * 0.13); check.lineCapStyle = .round; check.lineJoinStyle = .round; check.stroke()
      } else {
        // RN may draw a dark custom surface while UIKit's trait collection remains Light.
        // Derive this edge from the requested text color, not `secondaryLabel`.
        foreground.withAlphaComponent(max(0.58, foreground.cgColor.alpha)).setStroke()
        path.lineWidth = 1.5; path.stroke()
      }
    }
  }
}

/// The parser marks only generated GFM task prefixes, so ordinary `[x]` text and code are never
/// changed by this native presentation pass.
func streamTextMarkdownAttachTaskMarkers(_ runs: [StreamTextMarkdownRun], to output: NSMutableAttributedString,
                                         base: [NSAttributedString.Key: Any]) {
  guard output.length >= 4 else { return }
  let string = output.string as NSString
  let listRuns = runs.filter { $0.flags & UInt32(STMD_RUN_TASK_MARKER) != 0 }
  guard !listRuns.isEmpty else { return }
  var cursor = 0
  var runIndex = 0
  while cursor < string.length {
    let line = string.lineRange(for: NSRange(location: cursor, length: 0))
    let contentEnd = line.length > 0 && string.character(at: NSMaxRange(line) - 1) == 10 ? NSMaxRange(line) - 1 : NSMaxRange(line)
    var marker = line.location
    // Markdown's display prefix can include quoted/list nesting before the generated task marker.
    while marker + 1 < contentEnd && string.character(at: marker) == 62 && string.character(at: marker + 1) == 32 { marker += 2 }
    while marker < contentEnd && (string.character(at: marker) == 32 || string.character(at: marker) == 9) { marker += 1 }
    guard marker + 4 <= contentEnd else { cursor = NSMaxRange(line); continue }
    let first = string.character(at: marker), state = string.character(at: marker + 1)
    let isMarker = first == 91 && (state == 120 || state == 88 || state == 32) &&
      string.character(at: marker + 2) == 93 && string.character(at: marker + 3) == 32
    while runIndex < listRuns.count && NSMaxRange(listRuns[runIndex].range) <= marker { runIndex += 1 }
    let parserOwned = runIndex < listRuns.count && listRuns[runIndex].range.location == marker &&
      NSMaxRange(listRuns[runIndex].range) >= marker + 4
    guard isMarker, parserOwned else { cursor = NSMaxRange(line); continue }
    let checked = listRuns[runIndex].flags & UInt32(STMD_RUN_TASK_CHECKED) != 0
    let attachment = StreamTextMarkdownTaskAttachment(checked: checked,
      font: base[.font] as? UIFont ?? .systemFont(ofSize: 15),
      foreground: base[.foregroundColor] as? UIColor ?? .label)
    // Preserve UTF-16 offsets for the worker diff, selection and later rich-block placeholders.
    output.replaceCharacters(in: NSRange(location: marker, length: 4), with: "\u{FFFC}\u{200B}\u{200B}\u{200B}")
    output.addAttribute(.attachment, value: attachment, range: NSRange(location: marker, length: 1))
    output.addAttributes([.foregroundColor: UIColor.clear, .font: UIFont.systemFont(ofSize: 0.1)],
                         range: NSRange(location: marker + 1, length: 3))
    cursor = NSMaxRange(line)
  }
}

func streamTextMarkdownAttachBlocks(_ blocks: [StreamTextMarkdownBlock], to output: NSMutableAttributedString,
                                   offset: Int, base: [NSAttributedString.Key: Any], width: CGFloat) {
  // Several inline blocks can live in one paragraph. Resolve its effective style once, both to
  // retain Markdown's quote/list indentation and to avoid repeating an O(paragraph) mutation.
  var adjustedParagraphStarts = Set<Int>()
  for block in blocks where block.range.location >= offset {
    let range = NSRange(location: block.range.location - offset, length: block.range.length)
    guard range.length == 1, NSMaxRange(range) <= output.length else { continue }
    let paragraphRange = (output.string as NSString).paragraphRange(for: range)
    let effectiveParagraph = output.attribute(.paragraphStyle, at: paragraphRange.location, effectiveRange: nil) as? NSParagraphStyle
    // An attachment is laid out after its paragraph's head/tail indents. Use the same usable
    // width for rich blocks so a quoted/list table or image cannot overflow its text column.
    let headEdge = max(effectiveParagraph?.headIndent ?? 0, effectiveParagraph?.firstLineHeadIndent ?? 0)
    let tailIndent = effectiveParagraph?.tailIndent ?? 0
    // UIKit interprets a positive tail indent from the leading edge and a non-positive one from
    // the trailing edge. Subtracting a negative value would accidentally make the block wider.
    let trailingEdge = tailIndent > 0 ? min(width, tailIndent) : width + tailIndent
    let attachmentWidth = max(1, trailingEdge - headEdge)
    let attachment: StreamTextMarkdownAttachment
    if block.kind == UInt32(STMD_BLOCK_TABLE) {
      attachment = StreamTextMarkdownTableAttachment(block: block, base: base, width: attachmentWidth)
    } else {
      attachment = StreamTextMarkdownImageAttachment(block: block, width: attachmentWidth)
    }
    output.addAttribute(.attachment, value: attachment, range: range)
    // TextKit takes paragraph metrics from the paragraph's leading regular text, not necessarily
    // from U+FFFC. Reset the whole paragraph so an inline attachment is never cropped by an
    // explicit lineHeight carried by text before it ("Before ![image] after").
    guard adjustedParagraphStarts.insert(paragraphRange.location).inserted else { continue }
    let paragraph = (effectiveParagraph ?? (base[.paragraphStyle] as? NSParagraphStyle))?.mutableCopy() as? NSMutableParagraphStyle
      ?? NSMutableParagraphStyle()
    if paragraph.minimumLineHeight > 0 || paragraph.maximumLineHeight > 0 {
      paragraph.minimumLineHeight = 0
      paragraph.maximumLineHeight = 0
      output.addAttribute(.paragraphStyle, value: paragraph, range: paragraphRange)
    }
  }
}

class StreamTextMarkdownAttachment: NSTextAttachment {
  let block: StreamTextMarkdownBlock
  var displaySize: CGSize
  init(block: StreamTextMarkdownBlock, size: CGSize) {
    self.block = block
    self.displaySize = size
    super.init(data: nil, ofType: nil)
    bounds = CGRect(origin: .zero, size: size)
  }
  required init?(coder: NSCoder) { return nil }
  override func attachmentBounds(for textContainer: NSTextContainer?, proposedLineFragment lineFrag: CGRect,
                                 glyphPosition position: CGPoint, characterIndex charIndex: Int) -> CGRect {
    return CGRect(origin: .zero, size: displaySize)
  }
  var debugBlock: [String: Any] {
    ["kind": block.kind == UInt32(STMD_BLOCK_TABLE) ? "table" : "image",
     "start": block.range.location, "width": displaySize.width, "height": displaySize.height,
     "rowCount": block.rows, "columnCount": block.columns,
     "cells": block.cells.enumerated().map { index, cell -> [String: Any] in
       ["row": index / max(1, block.columns), "column": index % max(1, block.columns),
        "text": cell.text, "header": cell.header, "alignment": cell.alignment]
     }]
  }
}

/// Immutable vector cell layouts built on the Markdown worker. No table-sized bitmap: a large
/// table retains glyphs and row coordinates, and drawing visits only rows intersecting the clip.
final class StreamTextMarkdownTableAttachment: StreamTextMarkdownAttachment {
  // TextKit otherwise supplies its generic file icon for a nil attachment image.
  private static let transparentImage: UIImage = {
    let format = UIGraphicsImageRendererFormat(); format.scale = 1
    return UIGraphicsImageRenderer(size: CGSize(width: 1, height: 1), format: format).image { _ in }
  }()
  /// CoreText draws underline attributes but does not paint UIKit's
  /// `NSStrikethroughStyleAttributeName` in a CTFrame. Keep the tiny amount of strike geometry
  /// alongside the immutable frame, so drawing a visible table row never typesets or scans runs.
  private struct Strike { let rect: CGRect; let color: CGColor }
  private struct Cell { let frame: CTFrame; let rect: CGRect; let header: Bool; let strikes: [Strike] }
  private let cells: [Cell]
  private let rowOffsets: [CGFloat]
  private let columns: Int
  private let color: UIColor
  private let cellWidth: CGFloat
  init(block: StreamTextMarkdownBlock, base: [NSAttributedString.Key: Any], width: CGFloat) {
    columns = max(1, block.columns)
    let available = max(1, width)
    cellWidth = available / CGFloat(columns)
    let padding = min(6, cellWidth / 4)
    let textWidth = max(1, cellWidth - padding * 2)
    color = base[.foregroundColor] as? UIColor ?? .label
    let font = base[.font] as? UIFont ?? .systemFont(ofSize: 15)
    var setters: [CTFramesetter] = []
    var attributedCells: [NSMutableAttributedString] = []
    var heights = Array(repeating: font.lineHeight + padding * 2, count: max(1, block.rows))
    for (index, cell) in block.cells.enumerated() {
      var attributes = base
      if cell.header { attributes[.font] = UIFont(descriptor: font.fontDescriptor.withSymbolicTraits(.traitBold) ?? font.fontDescriptor, size: font.pointSize) }
      let text = NSMutableAttributedString(attributedString: streamTextMarkdownAttributed(
        StreamTextMarkdownResult(text: cell.text, runs: cell.runs.map { run in
          var run = run; if cell.header { run.flags |= UInt32(STMD_RUN_BOLD) }; return run
        }), base: attributes))
      let paragraph = (base[.paragraphStyle] as? NSParagraphStyle)?.mutableCopy() as? NSMutableParagraphStyle
        ?? NSMutableParagraphStyle()
      paragraph.alignment = cell.alignment == Int(STMD_ALIGN_CENTER) ? .center : cell.alignment == Int(STMD_ALIGN_RIGHT) ? .right : .left
      paragraph.lineBreakMode = .byWordWrapping
      text.addAttribute(.paragraphStyle, value: paragraph, range: NSRange(location: 0, length: text.length))
      let setter = CTFramesetterCreateWithAttributedString(text)
      setters.append(setter)
      attributedCells.append(text)
      let size = CTFramesetterSuggestFrameSizeWithConstraints(setter, CFRange(location: 0, length: 0), nil,
        CGSize(width: textWidth, height: CGFloat.greatestFiniteMagnitude), nil)
      let row = index / columns
      if row < heights.count { heights[row] = max(heights[row], ceil(size.height) + padding * 2 + 2) }
    }
    var offsets: [CGFloat] = [0]
    for height in heights { offsets.append(offsets.last! + height) }
    rowOffsets = offsets
    var built: [Cell] = []
    for (index, setter) in setters.enumerated() {
      let row = index / columns
      guard row < heights.count else { break }
      let rect = CGRect(x: CGFloat(index % columns) * cellWidth + padding,
        y: offsets[row] + padding, width: textWidth, height: heights[row] - padding * 2)
      let path = CGPath(rect: CGRect(origin: .zero, size: rect.size), transform: nil)
      let frame = CTFramesetterCreateFrame(setter, CFRange(location: 0, length: 0), path, nil)
      built.append(Cell(frame: frame, rect: rect, header: block.cells[index].header,
        strikes: block.cells[index].runs.contains { $0.flags & UInt32(STMD_RUN_STRIKE) != 0 }
          ? Self.strikes(in: frame, attributedText: attributedCells[index]) : []))
    }
    cells = built
    super.init(block: block, size: CGSize(width: available, height: offsets.last! + 1))
    image = Self.transparentImage
  }
  required init?(coder: NSCoder) { return nil }
  override var debugBlock: [String: Any] {
    var value = super.debugBlock
    value["strikeCount"] = cells.reduce(0) { $0 + $1.strikes.count }
    return value
  }
  func draw(at origin: CGPoint, context: CGContext) {
    context.saveGState()
    context.translateBy(x: origin.x, y: origin.y)
    let clip = context.boundingBoxOfClipPath
    // Binary search the first visible row, rather than scanning a long table on each frame.
    var lo = 0, hi = max(0, rowOffsets.count - 1)
    while lo < hi { let mid = (lo + hi) / 2; if rowOffsets[mid + 1] < clip.minY { lo = mid + 1 } else { hi = mid } }
    let first = lo
    var last = first
    while last + 1 < rowOffsets.count && rowOffsets[last] <= clip.maxY { last += 1 }
    context.setStrokeColor(color.withAlphaComponent(0.25).cgColor)
    context.setLineWidth(0.5)
    for row in first..<last {
      let rect = CGRect(x: 0, y: rowOffsets[row], width: displaySize.width, height: rowOffsets[row + 1] - rowOffsets[row])
      if row * columns < cells.count && cells[row * columns].header {
        context.setFillColor(color.withAlphaComponent(0.08).cgColor); context.fill(rect)
      }
      for column in 0..<columns {
        context.stroke(CGRect(x: CGFloat(column) * cellWidth, y: rect.minY, width: cellWidth, height: rect.height))
        let index = row * columns + column
        guard index < cells.count else { continue }
        let cell = cells[index]
        context.saveGState()
        context.translateBy(x: cell.rect.minX, y: cell.rect.maxY)
        context.scaleBy(x: 1, y: -1)
        context.textMatrix = .identity
        CTFrameDraw(cell.frame, context)
        // These rectangles are prepared together with the CTFrame. This is O(strikes) for only
        // visible rows, without a per-frame CoreText layout or attributed-string walk.
        for strike in cell.strikes {
          context.setFillColor(strike.color)
          context.fill(strike.rect)
        }
        context.restoreGState()
      }
    }
    context.restoreGState()
  }

  private static func strikes(in frame: CTFrame, attributedText text: NSAttributedString) -> [Strike] {
    guard text.length > 0 else { return [] }
    let lines = CTFrameGetLines(frame) as NSArray
    guard lines.count > 0 else { return [] }
    var origins = Array(repeating: CGPoint.zero, count: lines.count)
    CTFrameGetLineOrigins(frame, CFRange(location: 0, length: 0), &origins)
    let pixel = 1 / UIScreen.main.scale
    var result: [Strike] = []
    for index in 0..<lines.count {
      let line = lines[index] as! CTLine
      let lineRange = CTLineGetStringRange(line)
      guard lineRange.location != kCFNotFound, lineRange.length > 0 else { continue }
      let range = NSRange(location: lineRange.location, length: lineRange.length)
      text.enumerateAttribute(.strikethroughStyle, in: range) { value, candidate, _ in
        let style = (value as? NSNumber)?.intValue ?? (value as? Int ?? 0)
        guard style != 0 else { return }
        let overlap = NSIntersectionRange(range, candidate)
        guard overlap.length > 0 else { return }
        let start = CTLineGetOffsetForStringIndex(line, overlap.location, nil)
        let end = CTLineGetOffsetForStringIndex(line, NSMaxRange(overlap), nil)
        guard abs(end - start) > 0.25 else { return }
        let font = text.attribute(.font, at: overlap.location, effectiveRange: nil) as? UIFont
          ?? .systemFont(ofSize: 15)
        let thickness = max(pixel, font.pointSize * 0.06)
        let y = origins[index].y + font.ascender * 0.30 - thickness / 2
        let foreground = text.attribute(.foregroundColor, at: overlap.location, effectiveRange: nil) as? UIColor ?? .label
        result.append(Strike(rect: CGRect(x: origins[index].x + min(start, end), y: y,
          width: abs(end - start), height: thickness),
                             color: foreground.cgColor))
      }
    }
    return result
  }
}

final class StreamTextMarkdownImageAttachment: StreamTextMarkdownAttachment {
  // The image is fixed-size; TextKit scales it to the attachment bounds for narrow columns. It is
  // shared by all incomplete-image attachments, so a growing alt label never causes raster work.
  private static let pendingPlaceholder: UIImage = {
    let size = CGSize(width: 120, height: 48)
    // UIKit's default renderer format uses the display scale, keeping this shared raster crisp
    // on Retina while retaining the same 120 × 48 point attachment geometry.
    let format = UIGraphicsImageRendererFormat()
    return UIGraphicsImageRenderer(size: size, format: format).image { _ in
      UIColor(white: 0.5, alpha: 0.12).setFill(); UIRectFill(CGRect(origin: .zero, size: size))
      ("Image" as NSString).draw(in: CGRect(x: 4, y: 4, width: 112, height: 40),
        withAttributes: [.font: UIFont.systemFont(ofSize: 12), .foregroundColor: UIColor.gray])
    }
  }()
  private let availableWidth: CGFloat
  private let isPending: Bool
  private(set) var status: String
  private var pixelSize = CGSize.zero
  private var cancellation: (() -> Void)?
  private var started = false
  private var onChange: ((StreamTextMarkdownImageAttachment) -> Void)?
  init(block: StreamTextMarkdownBlock, width: CGFloat) {
    availableWidth = max(1, width)
    let size = CGSize(width: min(120, max(1, width)), height: 48)
    // An unfinished image has no trustworthy network address. Its attachment is deliberately
    // visible now, but the loader is not even asked to validate or queue a request until a later
    // parser result supplies the complete URL.
    isPending = block.kind == UInt32(STMD_BLOCK_IMAGE_PENDING) || block.url == nil
    status = isPending ? "pending" : "placeholder"
    super.init(block: block, size: size)
    image = Self.pendingPlaceholder
  }
  required init?(coder: NSCoder) { return nil }
  deinit { cancellation?() }
  func cancel() { cancellation?(); cancellation = nil; onChange = nil }
  /// A decoded attachment can survive a rebuilt attributed document only when its immutable
  /// Markdown block and *effective text-column width* agree. Comparing against the view width
  /// would incorrectly retain a quote/list image after its paragraph indentation changed.
  func canReuseLayout(for replacement: StreamTextMarkdownImageAttachment) -> Bool {
    block == replacement.block && abs(availableWidth - replacement.availableWidth) < 0.5
  }
  /// A width change needs a new attachment geometry, but a completed immutable image must not
  /// flash back to its placeholder while the shared loader cache answers again.
  @discardableResult
  func adoptLoadedState(from existing: StreamTextMarkdownImageAttachment) -> Bool {
    guard block == existing.block, existing.status == "loaded", let loaded = existing.image else { return false }
    image = loaded
    pixelSize = existing.pixelSize
    let width = min(availableWidth, loaded.size.width)
    displaySize = CGSize(width: width, height: width * loaded.size.height / max(1, loaded.size.width))
    bounds = CGRect(origin: .zero, size: displaySize)
    status = "loaded"
    started = true
    return true
  }
  // Started only after the attachment is installed; worker creation never starts stale downloads.
  func start(onChange: @escaping (StreamTextMarkdownImageAttachment) -> Void) {
    self.onChange = onChange
    guard !isPending else {
      status = "pending"
      return
    }
    // A completed empty target is not an unfinished image. Route it through the ordinary loader
    // so it reports the same invalid/error state as any other malformed completed URL.
    let url = block.url ?? ""
    guard !started else { return }
    started = true
    status = "loading"
    cancellation = StreamTextMarkdownImageLoader.shared.load(url: url) { [weak self] result in
      guard let self else { return }
      switch result {
      case .success(let image):
        self.image = image
        self.pixelSize = CGSize(width: image.cgImage?.width ?? 0, height: image.cgImage?.height ?? 0)
        let width = min(self.availableWidth, image.size.width)
        self.displaySize = CGSize(width: width, height: width * image.size.height / max(1, image.size.width))
        self.status = "loaded"
      case .failure(let error):
        self.status = (error as? StreamTextMarkdownImageError)?.status ?? "error"
        // The shared generic skeleton is intentionally cheap while the source is streaming.
        // Once a completed request fails, render its alt text once as the persistent fallback.
        self.image = Self.failurePlaceholder(self.block.alt, size: self.displaySize)
      }
      self.bounds = CGRect(origin: .zero, size: self.displaySize)
      self.onChange?(self)
    }
  }
  private static func failurePlaceholder(_ alt: String, size: CGSize) -> UIImage {
    let format = UIGraphicsImageRendererFormat()
    return UIGraphicsImageRenderer(size: size, format: format).image { _ in
      UIColor(white: 0.5, alpha: 0.12).setFill(); UIRectFill(CGRect(origin: .zero, size: size))
      ((alt.isEmpty ? "Image" : alt) as NSString).draw(in: CGRect(x: 4, y: 4,
        width: max(1, size.width - 8), height: max(1, size.height - 8)),
        withAttributes: [.font: UIFont.systemFont(ofSize: 12), .foregroundColor: UIColor.gray])
    }
  }
  override var debugBlock: [String: Any] {
    var value = super.debugBlock
    value["url"] = block.url ?? ""; value["alt"] = block.alt; value["status"] = status
    value["pixelWidth"] = pixelSize.width; value["pixelHeight"] = pixelSize.height
    return value
  }
}

final class StreamTextMarkdownLayoutManager: NSLayoutManager {
  var drawsTables = false
  override func drawBackground(forGlyphRange glyphsToShow: NSRange, at origin: CGPoint) {
    super.drawBackground(forGlyphRange: glyphsToShow, at: origin)
    guard let storage = textStorage, let context = UIGraphicsGetCurrentContext() else { return }
    let characters = characterRange(forGlyphRange: glyphsToShow, actualGlyphRange: nil)
    var drawnStarts = Set<Int>()
    storage.enumerateAttribute(streamTextMarkdownCodeBlockAttribute, in: characters) { value, range, _ in
      guard value != nil else { return }
      var fullCharacters = NSRange()
      guard let style = storage.attribute(streamTextMarkdownCodeBlockAttribute, at: range.location,
                                          longestEffectiveRange: &fullCharacters,
                                          in: NSRange(location: 0, length: storage.length)) as? StreamTextMarkdownCodeStyle,
            drawnStarts.insert(fullCharacters.location).inserted else { return }
      let glyphs = self.glyphRange(forCharacterRange: fullCharacters, actualCharacterRange: nil)
      guard glyphs.length > 0,
            let container = self.textContainer(forGlyphAt: glyphs.location, effectiveRange: nil) else { return }
      // One rounded surface per fenced block, drawn behind all of its lines. This is O(blocks),
      // not O(glyphs), and works for both a live TextKit view and reused attributed suffixes.
      let glyphRect = self.boundingRect(forGlyphRange: glyphs, in: container)
      let fragment = self.lineFragmentRect(forGlyphAt: glyphs.location, effectiveRange: nil,
                                           withoutAdditionalLayout: true)
      guard !glyphRect.isNull, !glyphRect.isEmpty, !fragment.isNull, !fragment.isEmpty else { return }
      // Keep the surface inside the line fragment: expanding a glyph rect past a container edge
      // clips the rounded corner and leaves a language label flush against the visible edge.
      let left = max(fragment.minX, glyphRect.minX - 8)
      let right = min(fragment.maxX, glyphRect.maxX + 8)
      guard right > left else { return }
      // Reserve a true header band: the 9pt label has 4pt top padding and at least 7pt before
      // the first code glyph. Only the block's first paragraph supplies this vertical room.
      let rect = CGRect(x: left, y: glyphRect.minY - 22, width: right - left,
                        height: glyphRect.height + 24).offsetBy(dx: origin.x, dy: origin.y)
      context.saveGState()
      style.background.setFill()
      UIBezierPath(roundedRect: rect, cornerRadius: 4).fill()
      if NSIntersectionRange(glyphsToShow, NSRange(location: glyphs.location, length: 1)).length > 0 {
        (style.language as NSString).draw(at: CGPoint(x: rect.minX + 8, y: rect.minY + 4), withAttributes: [
          .font: UIFont.monospacedSystemFont(ofSize: 9, weight: .semibold),
          .foregroundColor: style.labelColor,
        ])
      }
      context.restoreGState()
    }
  }
  override func drawGlyphs(forGlyphRange glyphsToShow: NSRange, at origin: CGPoint) {
    super.drawGlyphs(forGlyphRange: glyphsToShow, at: origin)
    guard drawsTables, let storage = textStorage, let context = UIGraphicsGetCurrentContext() else { return }
    let range = characterRange(forGlyphRange: glyphsToShow, actualGlyphRange: nil)
    storage.enumerateAttribute(.attachment, in: range) { value, range, _ in
      guard let table = value as? StreamTextMarkdownTableAttachment else { return }
      let glyph = self.glyphIndexForCharacter(at: range.location)
      guard let container = self.textContainer(forGlyphAt: glyph, effectiveRange: nil) else { return }
      let rect = self.boundingRect(forGlyphRange: NSRange(location: glyph, length: 1), in: container)
      table.draw(at: CGPoint(x: origin.x + rect.minX, y: origin.y + rect.minY), context: context)
    }
  }
}
