import UIKit

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

struct StreamTextMarkdownResult {
  var text: String
  var runs: [StreamTextMarkdownRun]
  var changedStart: Int = 0
}

/// Synchronous only for the caller's worker thread (prepared-layout construction); streaming uses
/// `StreamTextMarkdownPipeline` below. The C document is copied before the parser is reused.
func streamTextMarkdownParse(_ data: Data, github: Bool) -> StreamTextMarkdownResult? {
  guard let parser = stmd_create() else { return nil }
  defer { stmd_destroy(parser) }
  return streamTextMarkdownParse(parser, data, github: github)
}

private func streamTextMarkdownParse(_ parser: OpaquePointer, _ data: Data,
                                     github: Bool) -> StreamTextMarkdownResult? {
  return data.withUnsafeBytes { (bytes: UnsafeRawBufferPointer) -> StreamTextMarkdownResult? in
    let utf8 = bytes.bindMemory(to: CChar.self).baseAddress
    guard let document = stmd_parse(parser, utf8, data.count, github), document.pointee.error == 0 else { return nil }
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
    return StreamTextMarkdownResult(text: string, runs: runs, changedStart: document.pointee.changedStartUTF16)
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
  return attributes
}

func streamTextMarkdownAttributed(_ result: StreamTextMarkdownResult,
                                  base: [NSAttributedString.Key: Any]) -> NSAttributedString {
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
  return output
}

/// A delta against the previous *delivered* document. The parser is not allowed to advance until
/// the main queue acknowledges this patch; skipped pending inputs therefore cannot invalidate it.
struct StreamTextMarkdownUpdate {
  let result: StreamTextMarkdownResult
  let start: Int
  let suffix: NSAttributedString
  let sourceBytes: Int
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
    let revision: Int
    let attributes: [NSAttributedString.Key: Any]
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

  func schedule(_ data: Data, github: Bool, revision: Int,
                attributes: [NSAttributedString.Key: Any],
                deliver: @escaping (StreamTextMarkdownUpdate) -> Void) {
    let start: Bool = lock.withLock {
      guard !cancelled else { return false }
      pending = Job(data: data, github: github, revision: revision, attributes: attributes, deliver: deliver)
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
    let parsed = parser.flatMap { streamTextMarkdownParse($0, job.data, github: job.github) }
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
    let update = StreamTextMarkdownUpdate(result: result, start: start, suffix: suffix,
                                          sourceBytes: job.data.count, revision: job.revision)
    DispatchQueue.main.async { [self] in
      if !lock.withLock({ cancelled }) { job.deliver(update) }
      // One in-flight result per view, including UI delivery. Memory stays bounded if UI is busy.
      Self.queue.addOperation { [weak self] in self?.work() }
    }
  }
}
