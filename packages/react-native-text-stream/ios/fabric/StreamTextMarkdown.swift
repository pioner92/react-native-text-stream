import UIKit

let streamTextMarkdownCodeBlockAttribute = NSAttributedString.Key("StreamTextMarkdownCodeBlock")

private func streamTextMarkdownUsesLightSurface(_ color: UIColor) -> Bool {
  let resolved = color.resolvedColor(with: UITraitCollection.current)
  var red: CGFloat = 0, green: CGFloat = 0, blue: CGFloat = 0, alpha: CGFloat = 0
  guard resolved.getRed(&red, green: &green, blue: &blue, alpha: &alpha) else { return false }
  // A dark requested foreground implies a light host surface. This tracks explicit RN colors,
  // unlike UIUserInterfaceStyle, which does not change when the demo toggles its own background.
  return 0.2126 * red + 0.7152 * green + 0.0722 * blue < 0.48
}

private let streamTextMarkdownTokenMask = UInt32(STMD_RUN_TOKEN_KEYWORD | STMD_RUN_TOKEN_STRING |
  STMD_RUN_TOKEN_COMMENT | STMD_RUN_TOKEN_NUMBER | STMD_RUN_TOKEN_TYPE)

private enum StreamTextMarkdownTokenPalette {
  static let light: [(UInt32, UIColor)] = [
      (UInt32(STMD_RUN_TOKEN_KEYWORD), UIColor(red: 0.43, green: 0.18, blue: 0.70, alpha: 1)),
      (UInt32(STMD_RUN_TOKEN_STRING), UIColor(red: 0.00, green: 0.42, blue: 0.22, alpha: 1)),
      (UInt32(STMD_RUN_TOKEN_COMMENT), UIColor(red: 0.31, green: 0.36, blue: 0.43, alpha: 1)),
      (UInt32(STMD_RUN_TOKEN_NUMBER), UIColor(red: 0.67, green: 0.29, blue: 0.02, alpha: 1)),
      (UInt32(STMD_RUN_TOKEN_TYPE), UIColor(red: 0.00, green: 0.34, blue: 0.63, alpha: 1)),
  ]
  static let dark: [(UInt32, UIColor)] = [
    (UInt32(STMD_RUN_TOKEN_KEYWORD), UIColor(red: 0.76, green: 0.60, blue: 1, alpha: 1)),
    (UInt32(STMD_RUN_TOKEN_STRING), UIColor(red: 0.35, green: 0.90, blue: 0.59, alpha: 1)),
    (UInt32(STMD_RUN_TOKEN_COMMENT), UIColor(red: 0.67, green: 0.72, blue: 0.80, alpha: 1)),
    (UInt32(STMD_RUN_TOKEN_NUMBER), UIColor(red: 1.00, green: 0.76, blue: 0.35, alpha: 1)),
    (UInt32(STMD_RUN_TOKEN_TYPE), UIColor(red: 0.38, green: 0.72, blue: 1, alpha: 1)),
  ]
}

private func streamTextMarkdownTokenColor(flags: UInt32, base: UIColor) -> UIColor? {
  // Most Markdown runs have no syntax token. Avoid dynamic-color resolution and five UIColor
  // allocations for that hot streaming path; both palettes are immutable process-wide values.
  guard flags & streamTextMarkdownTokenMask != 0 else { return nil }
  let palette = streamTextMarkdownUsesLightSurface(base)
    ? StreamTextMarkdownTokenPalette.light : StreamTextMarkdownTokenPalette.dark
  for (flag, color) in palette where flags & flag != 0 { return color }
  return nil
}

final class StreamTextMarkdownCodeStyle: NSObject {
  let language: String
  let lightSurface: Bool
  let background: UIColor
  let labelColor: UIColor
  init(language: String, foreground: UIColor) {
    self.language = language
    lightSurface = streamTextMarkdownUsesLightSurface(foreground)
    background = lightSurface ? UIColor(white: 0.08, alpha: 0.07) : UIColor(white: 1, alpha: 0.12)
    labelColor = lightSurface ? UIColor(red: 0.25, green: 0.29, blue: 0.36, alpha: 1) : UIColor(red: 0.75, green: 0.80, blue: 0.88, alpha: 1)
  }
  override func isEqual(_ object: Any?) -> Bool {
    guard let other = object as? StreamTextMarkdownCodeStyle else { return false }
    return language == other.language && lightSurface == other.lightSurface
  }
  override var hash: Int { language.hashValue ^ (lightSurface ? 1 : 0) }
}

func streamTextMarkdownLanguageLabel(_ flags: UInt32) -> String {
  switch flags & UInt32(STMD_RUN_LANGUAGE_MASK) {
  case UInt32(STMD_RUN_LANGUAGE_JAVASCRIPT): return "JAVASCRIPT"
  case UInt32(STMD_RUN_LANGUAGE_TYPESCRIPT): return "TYPESCRIPT"
  case UInt32(STMD_RUN_LANGUAGE_JSON): return "JSON"
  case UInt32(STMD_RUN_LANGUAGE_PYTHON): return "PYTHON"
  default: return "CODE"
  }
}


// Markdown is deliberately kept off the main thread. The C parser owns its output only until its
// next call, so this object immediately copies the tiny run table and UTF-16 result into Swift
// values before handing the document back to the renderer.
struct StreamTextMarkdownRun: Equatable {
  var range: NSRange
  var flags: UInt32
  var headingLevel: Int
  var indent: Int
  var url: String?
}

struct StreamTextMarkdownCell: Equatable {
  var text: String
  var runs: [StreamTextMarkdownRun]
  var header: Bool
  var alignment: Int
}

struct StreamTextMarkdownBlock: Equatable {
  var range: NSRange
  var kind: UInt32
  var rows: Int
  var columns: Int
  var cells: [StreamTextMarkdownCell]
  var url: String?
  var alt: String
}

struct StreamTextMarkdownResult {
  var text: String
  var runs: [StreamTextMarkdownRun]
  /// Rich blocks are copied while the C document is borrowed. Rendering may retain this result
  /// across async image callbacks, so it must never retain C pointers.
  var blocks: [StreamTextMarkdownBlock] = []
  var changedStart: Int = 0
}

/// Tables and asynchronously loaded images need their live attachment lifecycle. Fenced code and
/// task controls use the same custom TextKit layout manager in the prepared canvas, so they retain
/// the completed-layout cache rather than forcing another parser pass on every mount.
func streamTextMarkdownRequiresLiveTextKit(_ result: StreamTextMarkdownResult) -> Bool {
  !result.blocks.isEmpty
}

/// Synchronous only for the caller's worker thread (prepared-layout construction); streaming uses
/// `StreamTextMarkdownPipeline` below. The C document is copied before the parser is reused.
func streamTextMarkdownParse(_ data: Data, github: Bool) -> StreamTextMarkdownResult? {
  guard let parser = stmd_create() else { return nil }
  defer { stmd_destroy(parser) }
  return streamTextMarkdownParse(parser, data, github: github)
}

private func streamTextMarkdownParse(_ parser: OpaquePointer, _ data: Data,
                                     github: Bool, streamingFinal: Bool? = nil) -> StreamTextMarkdownResult? {
  return data.withUnsafeBytes { (bytes: UnsafeRawBufferPointer) -> StreamTextMarkdownResult? in
    let utf8 = bytes.bindMemory(to: CChar.self).baseAddress
    let document = streamingFinal.map { stmd_parse_streaming(parser, utf8, data.count, github, $0) }
      ?? stmd_parse(parser, utf8, data.count, github)
    guard let document, document.pointee.error == 0 else { return nil }
    let string = document.pointee.text.map {
      String(decoding: UnsafeBufferPointer(start: $0, count: document.pointee.textLength), as: UTF16.self)
    } ?? ""
    var runs: [StreamTextMarkdownRun] = []
    runs.reserveCapacity(document.pointee.runCount)
    if let sourceRuns = document.pointee.runs {
      for index in 0..<document.pointee.runCount {
        let run = sourceRuns[index]
        guard run.start <= Int.max, run.length <= Int.max else { continue }
        runs.append(StreamTextMarkdownRun(range: NSRange(location: Int(run.start), length: Int(run.length)),
                                          flags: run.flags, headingLevel: Int(run.headingLevel),
                                          indent: Int(run.indent), url: run.url.map { String(cString: $0) }))
      }
    }
    var blocks: [StreamTextMarkdownBlock] = []
    blocks.reserveCapacity(document.pointee.blockCount)
    if let sourceBlocks = document.pointee.blocks {
      for index in 0..<document.pointee.blockCount {
        let block = sourceBlocks[index]
        guard block.start <= Int.max, block.length <= Int.max else { continue }
        var cells: [StreamTextMarkdownCell] = []
        let count = block.rowCount.multipliedReportingOverflow(by: block.columnCount)
        if !count.overflow, count.partialValue <= UInt(Int.max), let sourceCells = block.cells {
          let cellCount = Int(count.partialValue)
          cells.reserveCapacity(cellCount)
          for cellIndex in 0..<cellCount {
            let cell = sourceCells[cellIndex]
            let cellText = cell.text.map { String(decoding: UnsafeBufferPointer(start: $0, count: cell.textLength), as: UTF16.self) } ?? ""
            var cellRuns: [StreamTextMarkdownRun] = []
            if let rawRuns = cell.runs {
              cellRuns.reserveCapacity(cell.runCount)
              for runIndex in 0..<cell.runCount {
                let run = rawRuns[runIndex]
                guard run.start <= Int.max, run.length <= Int.max else { continue }
                cellRuns.append(StreamTextMarkdownRun(range: NSRange(location: Int(run.start), length: Int(run.length)),
                  flags: run.flags, headingLevel: Int(run.headingLevel), indent: Int(run.indent),
                  url: run.url.map { String(cString: $0) }))
              }
            }
            cells.append(StreamTextMarkdownCell(text: cellText, runs: cellRuns,
              header: cell.flags & UInt32(STMD_BLOCK_CELL_HEADER) != 0, alignment: Int(cell.alignment)))
          }
        }
        let alt = block.alt.map { String(decoding: UnsafeBufferPointer(start: $0, count: block.altLength), as: UTF16.self) } ?? ""
        blocks.append(StreamTextMarkdownBlock(range: NSRange(location: Int(block.start), length: Int(block.length)),
          kind: block.kind, rows: Int(block.rowCount), columns: Int(block.columnCount), cells: cells,
          url: block.url.map { String(cString: $0) }, alt: alt))
      }
    }
    return StreamTextMarkdownResult(text: string, runs: runs, blocks: blocks, changedStart: document.pointee.changedStartUTF16)
  }
}

func streamTextMarkdownAttributes(for run: StreamTextMarkdownRun,
                                  base: [NSAttributedString.Key: Any]) -> [NSAttributedString.Key: Any] {
  var attributes: [NSAttributedString.Key: Any] = [:]
  let baseFont = base[.font] as? UIFont ?? .systemFont(ofSize: 15)
  var traits = baseFont.fontDescriptor.symbolicTraits
  if run.flags & UInt32(STMD_RUN_BOLD) != 0 || run.headingLevel > 0 { traits.insert(.traitBold) }
  if run.flags & UInt32(STMD_RUN_ITALIC) != 0 { traits.insert(.traitItalic) }
  var font = UIFont(descriptor: baseFont.fontDescriptor.withSymbolicTraits(traits) ?? baseFont.fontDescriptor,
                    size: baseFont.pointSize)
  if run.flags & UInt32(STMD_RUN_CODE) != 0 || run.flags & UInt32(STMD_RUN_CODE_BLOCK) != 0 {
    font = .monospacedSystemFont(ofSize: baseFont.pointSize, weight: .regular)
  } else if run.headingLevel > 0 {
    font = UIFont(descriptor: font.fontDescriptor, size: baseFont.pointSize * (1.55 - CGFloat(run.headingLevel) * 0.08))
  }
  attributes[.font] = font
  if run.flags & UInt32(STMD_RUN_STRIKE) != 0 { attributes[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
  if let url = run.url, run.flags & UInt32(STMD_RUN_LINK) != 0,
     let scheme = URL(string: url)?.scheme?.lowercased(), ["http", "https", "mailto"].contains(scheme) {
    attributes[.link] = url
    attributes[.underlineStyle] = NSUnderlineStyle.single.rawValue
  }
  if run.headingLevel > 0 || run.indent > 0 || run.flags & UInt32(STMD_RUN_QUOTE) != 0 || run.flags & UInt32(STMD_RUN_CODE_BLOCK) != 0 {
    let paragraph = (base[.paragraphStyle] as? NSParagraphStyle)?.mutableCopy() as? NSMutableParagraphStyle
      ?? NSMutableParagraphStyle()
    if run.headingLevel > 0 && paragraph.maximumLineHeight > 0 {
      let scale = font.pointSize / baseFont.pointSize
      paragraph.minimumLineHeight *= scale
      paragraph.maximumLineHeight *= scale
    }
    if run.indent > 0 { paragraph.headIndent += CGFloat(run.indent) * baseFont.pointSize * 1.5 }
    if run.flags & UInt32(STMD_RUN_QUOTE) != 0 { paragraph.headIndent += baseFont.pointSize }
    attributes[.paragraphStyle] = paragraph
  }
  if run.flags & UInt32(STMD_RUN_CODE_BLOCK) != 0 {
    attributes[streamTextMarkdownCodeBlockAttribute] = StreamTextMarkdownCodeStyle(
      language: streamTextMarkdownLanguageLabel(run.flags), foreground: base[.foregroundColor] as? UIColor ?? .label)
  }
  if let tokenColor = streamTextMarkdownTokenColor(flags: run.flags,
    base: base[.foregroundColor] as? UIColor ?? .label) { attributes[.foregroundColor] = tokenColor }
  return attributes
}

/// Apply code-surface insets once per paragraph, with vertical breathing room only at a fenced
/// block's outer edges. Doing it per syntax token would make every code line look double-spaced.
func streamTextMarkdownStyleCodeBlocks(_ output: NSMutableAttributedString) {
  guard output.length > 0 else { return }
  let string = output.string as NSString
  var paragraphs: [(range: NSRange, code: Bool)] = []
  var cursor = 0
  while cursor < string.length {
    let range = string.lineRange(for: NSRange(location: cursor, length: 0))
    let code = output.attribute(streamTextMarkdownCodeBlockAttribute, at: range.location, effectiveRange: nil) != nil
    paragraphs.append((range, code))
    cursor = NSMaxRange(range)
  }
  for index in paragraphs.indices where paragraphs[index].code {
    let range = paragraphs[index].range
    let previousIsCode = index > 0 && paragraphs[index - 1].code
    let nextIsCode = index + 1 < paragraphs.count && paragraphs[index + 1].code
    let current = output.attribute(.paragraphStyle, at: range.location, effectiveRange: nil) as? NSParagraphStyle
    let paragraph = current?.mutableCopy() as? NSMutableParagraphStyle ?? NSMutableParagraphStyle()
    paragraph.firstLineHeadIndent += 8
    paragraph.headIndent += 8
    paragraph.tailIndent -= 8
    // This is a one-time fenced-block header band for its language label, never an interior
    // code-line spacing change.
    paragraph.paragraphSpacingBefore = previousIsCode ? 0 : max(paragraph.paragraphSpacingBefore, 26)
    paragraph.paragraphSpacing = nextIsCode ? 0 : max(paragraph.paragraphSpacing, 5)
    output.addAttribute(.paragraphStyle, value: paragraph, range: range)
  }
}

func streamTextMarkdownAttributed(_ result: StreamTextMarkdownResult,
                                  base: [NSAttributedString.Key: Any], width: CGFloat = 300) -> NSAttributedString {
  let output = NSMutableAttributedString(string: result.text, attributes: base)
  let length = output.length
  var styles: [String: [NSAttributedString.Key: Any]] = [:]
  for run in result.runs {
    let end = min(NSMaxRange(run.range), length)
    guard end > run.range.location else { continue }
    let key = "\(run.flags)-\(run.headingLevel)-\(run.indent)"
    var attributes: [NSAttributedString.Key: Any]
    if let cached = styles[key] { attributes = cached } else {
      var styleRun = run; styleRun.url = nil
      attributes = streamTextMarkdownAttributes(for: styleRun, base: base)
      styles[key] = attributes
    }
    if let url = run.url, run.flags & UInt32(STMD_RUN_LINK) != 0,
       let scheme = URL(string: url)?.scheme?.lowercased(), ["http", "https", "mailto"].contains(scheme) {
      attributes[.link] = url
      attributes[.underlineStyle] = NSUnderlineStyle.single.rawValue
    }
    output.addAttributes(attributes,
                         range: NSRange(location: run.range.location, length: end - run.range.location))
  }
  if result.runs.contains(where: { $0.flags & UInt32(STMD_RUN_CODE_BLOCK) != 0 }) {
    streamTextMarkdownStyleCodeBlocks(output)
  }
  streamTextMarkdownAttachBlocks(result.blocks, to: output, offset: 0, base: base, width: width)
  streamTextMarkdownAttachTaskMarkers(result.runs, to: output, base: base)
  return output
}

/// A delta against the previous *delivered* document. The parser is not allowed to advance until
/// the main queue acknowledges this patch; skipped pending inputs therefore cannot invalidate it.
struct StreamTextMarkdownUpdate {
  let result: StreamTextMarkdownResult
  let start: Int
  let suffix: NSAttributedString
  let sourceBytes: Int
  let final: Bool
  let revision: Int
}

final class StreamTextMarkdownPipeline {
  private static let queue: OperationQueue = {
    let queue = OperationQueue()
    queue.name = "StreamText.markdown"
    queue.qualityOfService = .userInitiated
    queue.maxConcurrentOperationCount = 2
    return queue
  }()
  private struct Job {
    let data: Data
    let github: Bool
    let final: Bool
    let revision: Int
    let attributes: [NSAttributedString.Key: Any]
    let width: CGFloat
    let deliver: (StreamTextMarkdownUpdate) -> Void
  }
  private let lock = NSLock()
  private let parser = stmd_create()
  private var pending: Job?
  private var active = false
  private var cancelled = false
  // Worker-only: reset after parser failure, whose plain fallback differs from its internal state.
  private var parsedBefore = false

  deinit { if let parser { stmd_destroy(parser) } }

  func schedule(_ data: Data, github: Bool, final: Bool, revision: Int,
                attributes: [NSAttributedString.Key: Any], width: CGFloat,
                deliver: @escaping (StreamTextMarkdownUpdate) -> Void) {
    let start: Bool = lock.withLock {
      guard !cancelled else { return false }
      pending = Job(data: data, github: github, final: final, revision: revision, attributes: attributes, width: width, deliver: deliver)
      guard !active else { return false }
      active = true
      return true
    }
    if start { Self.queue.addOperation { [weak self] in self?.work() } }
  }

  func cancel() { lock.withLock { cancelled = true; pending = nil } }

  private func work() {
    let job: Job? = lock.withLock {
      guard !cancelled, let job = pending else { active = false; return nil }
      pending = nil
      return job
    }
    guard let job else { return }
    // Finality is part of the input identity: an open stream intentionally withholds incomplete
    // syntax, while its terminal parse must restore those literal bytes even when no byte changed.
    let parsed = parser.flatMap { streamTextMarkdownParse($0, job.data, github: job.github, streamingFinal: job.final) }
    var result: StreamTextMarkdownResult
    if let parsed { result = parsed } else {
      var raw = String(decoding: job.data, as: UTF8.self)
      if raw.first == "\u{FEFF}" { raw.removeFirst() }
      result = StreamTextMarkdownResult(text: raw, runs: [])
    }
    let text = result.text as NSString
    var start = parsedBefore && parsed != nil ? min(result.changedStart, text.length) : 0
    if start > 0 && start < text.length {
      start = text.rangeOfComposedCharacterSequence(at: start).location
    }
    // Paragraph attributes/layout require invalidating the changed paragraph, not the document.
    while start > 0 && text.character(at: start - 1) != 10 { start -= 1 }
    // A streamed parse can change the final line of a fenced block. Rebuilding only that line
    // would leave the old block's bottom spacing in the retained prefix and treat the new line
    // as a new block (adding its header spacing). Find its contiguous CODE_BLOCK run cluster in
    // one pass: repeatedly searching every run for every earlier code line is quadratic on a
    // large fence.
    var clusterStart: Int?
    var clusterEnd = 0
    var codeBoundaryStart: Int?
    let boundary = NSRange(location: max(0, start - 1), length: min(2, text.length - max(0, start - 1)))
    for run in result.runs where run.flags & UInt32(STMD_RUN_CODE_BLOCK) != 0 {
      let runEnd = NSMaxRange(run.range)
      // Only overlapping code runs are one fenced block. A plain separator can be one character
      // between adjacent fences; treating `clusterEnd + 1` as contiguous would reprocess every
      // earlier fence in a long document.
      if let currentStart = clusterStart, run.range.location <= clusterEnd {
        clusterEnd = max(clusterEnd, runEnd)
        if NSIntersectionRange(NSRange(location: currentStart, length: clusterEnd - currentStart), boundary).length > 0 {
          codeBoundaryStart = currentStart
        }
      } else {
        clusterStart = run.range.location
        clusterEnd = runEnd
        if NSIntersectionRange(run.range, boundary).length > 0 { codeBoundaryStart = run.range.location }
      }
    }
    if let codeBoundaryStart, codeBoundaryStart < text.length {
      start = text.lineRange(for: NSRange(location: codeBoundaryStart, length: 0)).location
    }
    parsedBefore = parsed != nil
    let suffix = NSMutableAttributedString(string: text.substring(from: start), attributes: job.attributes)
    // Cache styles within this patch: repeated paragraphs should not construct the same UIFont
    // descriptor thousands of times. URLs are applied separately and don't inflate the cache.
    var styles: [String: [NSAttributedString.Key: Any]] = [:]
    for run in result.runs {
      let from = max(start, run.range.location)
      let end = min(text.length, NSMaxRange(run.range))
      guard end > from else { continue }
      let key = "\(run.flags)-\(run.headingLevel)-\(run.indent)"
      var attributes: [NSAttributedString.Key: Any]
      if let cached = styles[key] { attributes = cached } else {
        var styleRun = run; styleRun.url = nil
        attributes = streamTextMarkdownAttributes(for: styleRun, base: job.attributes)
        styles[key] = attributes
      }
      if let url = run.url, run.flags & UInt32(STMD_RUN_LINK) != 0,
         let scheme = URL(string: url)?.scheme?.lowercased(), ["http", "https", "mailto"].contains(scheme) {
        attributes[.link] = url
        attributes[.underlineStyle] = NSUnderlineStyle.single.rawValue
      }
      suffix.addAttributes(attributes, range: NSRange(location: from - start, length: end - from))
    }
    if result.runs.contains(where: {
      $0.flags & UInt32(STMD_RUN_CODE_BLOCK) != 0 && NSMaxRange($0.range) > start
    }) {
      streamTextMarkdownStyleCodeBlocks(suffix)
    }
    streamTextMarkdownAttachBlocks(result.blocks, to: suffix, offset: start, base: job.attributes, width: job.width)
    let localRuns = result.runs.compactMap { run -> StreamTextMarkdownRun? in
      let from = max(start, run.range.location), to = min(text.length, NSMaxRange(run.range))
      guard to > from else { return nil }
      var local = run; local.range = NSRange(location: from - start, length: to - from)
      return local
    }
    streamTextMarkdownAttachTaskMarkers(localRuns, to: suffix, base: job.attributes)
    let update = StreamTextMarkdownUpdate(result: result, start: start, suffix: suffix,
                                          sourceBytes: job.data.count, final: job.final, revision: job.revision)
    DispatchQueue.main.async { [self] in
      if !lock.withLock({ cancelled }) { job.deliver(update) }
      // One in-flight result per view, including UI delivery. Memory stays bounded if UI is busy.
      Self.queue.addOperation { [weak self] in self?.work() }
    }
  }
}
