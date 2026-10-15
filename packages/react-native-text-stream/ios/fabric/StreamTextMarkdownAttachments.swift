import UIKit
import CoreText

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
  struct Cell { let frame: CTFrame; let rect: CGRect; let header: Bool }
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
      built.append(Cell(frame: CTFramesetterCreateFrame(setter, CFRange(location: 0, length: 0), path, nil),
        rect: rect, header: block.cells[index].header))
    }
    cells = built
    super.init(block: block, size: CGSize(width: available, height: offsets.last! + 1))
    image = Self.transparentImage
  }
  required init?(coder: NSCoder) { return nil }
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
        context.restoreGState()
      }
    }
    context.restoreGState()
  }
}

final class StreamTextMarkdownImageAttachment: StreamTextMarkdownAttachment {
  private let availableWidth: CGFloat
  private(set) var status = "placeholder"
  private var pixelSize = CGSize.zero
  private var cancellation: (() -> Void)?
  private var started = false
  private var onChange: ((StreamTextMarkdownImageAttachment) -> Void)?
  init(block: StreamTextMarkdownBlock, width: CGFloat) {
    availableWidth = max(1, width)
    let size = CGSize(width: min(120, max(1, width)), height: 48)
    super.init(block: block, size: size)
    image = Self.placeholder(block.alt, size: size)
  }
  required init?(coder: NSCoder) { return nil }
  deinit { cancellation?() }
  func cancel() { cancellation?(); cancellation = nil; onChange = nil }
  func fits(width: CGFloat) -> Bool { abs(availableWidth - width) < 0.5 }
  // Started only after the attachment is installed; worker creation never starts stale downloads.
  func start(onChange: @escaping (StreamTextMarkdownImageAttachment) -> Void) {
    self.onChange = onChange
    guard !started else { return }
    started = true
    status = "loading"
    cancellation = StreamTextMarkdownImageLoader.shared.load(url: block.url ?? "") { [weak self] result in
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
      }
      self.bounds = CGRect(origin: .zero, size: self.displaySize)
      self.onChange?(self)
    }
  }
  private static func placeholder(_ alt: String, size: CGSize) -> UIImage {
    let format = UIGraphicsImageRendererFormat(); format.scale = 1
    return UIGraphicsImageRenderer(size: size, format: format).image { context in
      UIColor(white: 0.5, alpha: 0.12).setFill(); context.fill(CGRect(origin: .zero, size: size))
      ((alt.isEmpty ? "Image" : alt) as NSString).draw(in: CGRect(x: 4, y: 4, width: max(1, size.width - 8), height: size.height - 8),
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
