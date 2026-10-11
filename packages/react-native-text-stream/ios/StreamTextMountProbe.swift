//
//  StreamTextMountProbe.swift
//  Pods
//

import CoreText
import UIKit

/// Benchmarks only: what showing a finished text costs on the main thread, step by step, for the
/// way the renderer does it today and for the alternatives (a text view around a layout prepared
/// off the main thread; drawing a prepared layout without a text view).
enum StreamTextMountProbe {
  /// Times in ms by step. Main thread.
  static func run(text: String, width: CGFloat) -> [String: Double] {
    var times: [String: Double] = [:]
    func time<T>(_ name: String, _ body: () -> T) -> T {
      let started = CACurrentMediaTime()
      let result = body()
      times[name] = (CACurrentMediaTime() - started) * 1000
      return result
    }
    let style = StreamTextStyle()
    style.fontSize = 14
    style.lineHeight = 21
    let attributes = style.attributes()
    let height = StreamTextLayout.textViewHeight
    // A window-attached host, so views lay out and draw the way they would on screen.
    guard let window = UIApplication.shared.connectedScenes
      .compactMap({ ($0 as? UIWindowScene)?.keyWindow }).first else { return ["error": 1] }
    let host = UIView(frame: CGRect(x: 0, y: 0, width: width, height: 600))
    host.clipsToBounds = true
    host.alpha = 0.02
    window.addSubview(host)
    defer { host.removeFromSuperview() }

    // 0. CoreText before anything else touched this text (no warm caches).
    time("coretext.cold.framesetter") {
      let framesetter = CTFramesetterCreateWithAttributedString(NSAttributedString(string: String(text), attributes: attributes))
      _ = CTFramesetterSuggestFrameSizeWithConstraints(
        framesetter, CFRange(location: 0, length: 0), nil, CGSize(width: width, height: .greatestFiniteMagnitude), nil)
    }

    // 1. Today: an empty text view, then the text appended and laid out on the main thread.
    // The stacks are kept in locals: a container holds its layout manager and text storage weakly.
    let freshStack = makeContainer(width: 0, tracks: true)
    let fresh = time("today.create") { makeTextView(container: freshStack.container) }
    time("today.configure") { configure(fresh, attributes: attributes, frame: CGRect(x: 0, y: 0, width: width, height: height)) }
    time("today.addSubview") { host.addSubview(fresh) }
    time("today.append") {
      fresh.textStorage.beginEditing()
      fresh.textStorage.append(NSAttributedString(string: text, attributes: attributes))
      fresh.textStorage.endEditing()
    }
    time("today.layout") { fresh.layoutManager.ensureLayout(for: fresh.textContainer) }
    time("today.draw") { draw(fresh) }
    fresh.removeFromSuperview()

    // 2. The text laid out ahead (in the shadow node this happens off the main thread).
    let preparedStack = makeContainer(width: width, height: height, tracks: false)
    time("prepared.layout") {
      preparedStack.storage.setAttributedString(NSAttributedString(string: text, attributes: attributes))
      preparedStack.layoutManager.ensureLayout(for: preparedStack.container)
    }
    let prepared = preparedStack.container
    let layoutManager = preparedStack.layoutManager

    // 3. A text view around the prepared layout, step by step.
    func stackState(_ tag: String) {
      times["state.\(tag).containerW"] = Double(prepared.size.width)
      times["state.\(tag).containerH"] = Double(prepared.size.height)
      times["state.\(tag).padding"] = Double(prepared.lineFragmentPadding)
      times["state.\(tag).tracksW"] = prepared.widthTracksTextView ? 1 : 0
      times["state.\(tag).usesFontLeading"] = layoutManager.usesFontLeading ? 1 : 0
      times["state.\(tag).nonContiguous"] = layoutManager.allowsNonContiguousLayout ? 1 : 0
      times["state.\(tag).hyphenation"] = Double(layoutManager.hyphenationFactor)
      times["state.\(tag).attrCount"] = Double(preparedStack.storage.attributes(at: 0, effectiveRange: nil).count)
      times["state.\(tag).unlaid"] = Double(layoutManager.firstUnlaidCharacterIndex())
      times["state.\(tag).limitsLayoutForSuspiciousContents"] = layoutManager.limitsLayoutForSuspiciousContents ? 1 : 0
    }
    stackState("before")
    let adopted = time("adopt.init") { makeTextView(container: prepared, frame: CGRect(x: 0, y: 0, width: width, height: height)) }
    stackState("after")
    // The same, with the stack pre-set the way a text view leaves it.
    let pre = makeContainer(width: width, height: height, tracks: false)
    pre.layoutManager.allowsNonContiguousLayout = true
    pre.storage.setAttributedString(NSAttributedString(string: text, attributes: attributes))
    pre.layoutManager.ensureLayout(for: pre.container)
    _ = time("adopt.initNonContiguous") { makeTextView(container: pre.container, frame: CGRect(x: 0, y: 0, width: width, height: height)) }
    times["adopt.initNonContiguous.unlaidBefore"] = 0
    let pre2 = makeContainer(width: width, height: height, tracks: false)
    pre2.storage.setAttributedString(NSAttributedString(string: text, attributes: attributes))
    pre2.layoutManager.ensureLayout(for: pre2.container)
    _ = time("adopt.initZeroFrame") { makeTextView(container: pre2.container, frame: .zero) }
    times["adopt.initZeroFrame.unlaid"] = Double(pre2.layoutManager.firstUnlaidCharacterIndex())
    // The container already the size and settings the text view gives it (frame minus its default 8 pt insets).
    let pre3 = makeContainer(width: width, height: height - 16, tracks: true)
    pre3.layoutManager.allowsNonContiguousLayout = true
    pre3.storage.setAttributedString(NSAttributedString(string: text, attributes: attributes))
    pre3.layoutManager.ensureLayout(for: pre3.container)
    let matched = time("adopt.initMatched") { makeTextView(container: pre3.container, frame: CGRect(x: 0, y: 0, width: width, height: height)) }
    times["adopt.initMatched.containerH"] = Double(pre3.container.size.height)
    times["adopt.initMatched.unlaid"] = Double(pre3.layoutManager.firstUnlaidCharacterIndex())
    time("adopt.initMatched.insetsZero") {
      matched.textContainerInset = .zero
      pre3.layoutManager.ensureLayout(for: pre3.container)
    }
    // Same, contiguous layout kept.
    let pre4 = makeContainer(width: width, height: height - 16, tracks: true)
    pre4.storage.setAttributedString(NSAttributedString(string: text, attributes: attributes))
    pre4.layoutManager.ensureLayout(for: pre4.container)
    _ = time("adopt.initMatchedContiguous") { makeTextView(container: pre4.container, frame: CGRect(x: 0, y: 0, width: width, height: height)) }
    time("adopt.insets") {
      adopted.textContainerInset = .zero
      adopted.textContainer.lineFragmentPadding = 0
    }
    time("adopt.flags") {
      adopted.isEditable = false
      adopted.isSelectable = true
      adopted.isScrollEnabled = false
      adopted.showsVerticalScrollIndicator = false
      adopted.showsHorizontalScrollIndicator = false
      adopted.backgroundColor = .clear
    }
    time("adopt.tint") { adopted.tintColor = nil }
    time("adopt.typingAttributes") { adopted.typingAttributes = attributes }
    time("adopt.tracking") {
      adopted.textContainer.widthTracksTextView = true
      adopted.textContainer.heightTracksTextView = true
    }
    time("adopt.addSubview") { host.addSubview(adopted) }
    times["adopt.unlaidAfter"] = Double(layoutManager.firstUnlaidCharacterIndex())
    times["adopt.length"] = Double(adopted.textStorage.length)
    time("adopt.layout") { layoutManager.ensureLayout(for: prepared) }
    time("adopt.draw") { draw(adopted) }
    adopted.removeFromSuperview()

    // 4. No text view: draw the visible part of a prepared layout into a plain layer.
    let canvasStack = makeContainer(width: width, height: height, tracks: false)
    canvasStack.storage.setAttributedString(NSAttributedString(string: text, attributes: attributes))
    canvasStack.layoutManager.ensureLayout(for: canvasStack.container)
    time("canvas.drawVisible") {
      let visible = CGRect(x: 0, y: 0, width: width, height: 600)
      let glyphs = canvasStack.layoutManager.glyphRange(forBoundingRect: visible, in: canvasStack.container)
      let format = UIGraphicsImageRendererFormat.preferred()
      let image = UIGraphicsImageRenderer(size: visible.size, format: format).image { _ in
        canvasStack.layoutManager.drawBackground(forGlyphRange: glyphs, at: .zero)
        canvasStack.layoutManager.drawGlyphs(forGlyphRange: glyphs, at: .zero)
      }
      let layer = CALayer()
      layer.frame = visible
      layer.contents = image.cgImage
      host.layer.addSublayer(layer)
      CATransaction.flush()
      layer.removeFromSuperlayer()
    }

    // 5. Lower levels: the same text through CoreText directly, the decode alone, a background thread.
    let utf8 = Array(text.utf8)
    time("decode.utf8ToString") { _ = String(decoding: utf8, as: UTF8.self) }
    let attributed = time("decode.attributedString") { NSAttributedString(string: text, attributes: attributes) }
    let framesetterHeight = time("coretext.framesetter") { () -> CGFloat in
      let framesetter = CTFramesetterCreateWithAttributedString(attributed)
      return CTFramesetterSuggestFrameSizeWithConstraints(
        framesetter, CFRange(location: 0, length: 0), nil,
        CGSize(width: width, height: .greatestFiniteMagnitude), nil
      ).height
    }
    var ctRanges: [NSRange] = []
    let typesetterLines = time("coretext.typesetterLines") { () -> Int in
      let typesetter = CTTypesetterCreateWithAttributedString(attributed)
      let length = attributed.length
      var start = 0
      var lines = 0
      while start < length {
        let count = CTTypesetterSuggestLineBreak(typesetter, start, Double(width))
        _ = CTTypesetterCreateLine(typesetter, CFRange(location: start, length: count))
        ctRanges.append(NSRange(location: start, length: count))
        start += max(count, 1)
        lines += 1
      }
      return lines
    }
    let textKitHeight = canvasStack.layoutManager.usedRect(for: canvasStack.container).height
    times["height.textKit"] = Double(textKitHeight)
    times["height.framesetter"] = Double(framesetterHeight)
    times["lines.typesetter"] = Double(typesetterLines)
    var tkRanges: [NSRange] = []
    let lm = canvasStack.layoutManager
    lm.enumerateLineFragments(forGlyphRange: lm.glyphRange(for: canvasStack.container)) { _, _, _, glyphs, _ in
      tkRanges.append(lm.characterRange(forGlyphRange: glyphs, actualGlyphRange: nil))
    }
    times["lines.textKit"] = Double(tkRanges.count)
    if let first = tkRanges.first {
      let tkGlyphs = lm.glyphRange(forCharacterRange: first, actualCharacterRange: nil)
      // Up to the last visible glyph (the used rect also counts trailing spaces).
      times["width.textKit.bounding"] = Double(lm.boundingRect(forGlyphRange: tkGlyphs, in: canvasStack.container).width)
      times["width.textKit.used"] = Double(lm.lineFragmentUsedRect(forGlyphAt: tkGlyphs.location, effectiveRange: nil).width)
      let lineText = (text as NSString).substring(with: first)
      func ctWidth(_ attrs: [NSAttributedString.Key: Any]) -> Double {
        let line = CTLineCreateWithAttributedString(NSAttributedString(string: lineText, attributes: attrs))
        return CTLineGetTypographicBounds(line, nil, nil, nil)
      }
      let font = attributes[.font] as! UIFont
      times["width.ct.uikitAttrs"] = ctWidth(attributes)
      times["width.ct.fontOnly"] = ctWidth([.font: font])
      times["width.ct.kern0"] = ctWidth([.font: font, .kern: 0])
      let tracked = UIFont(descriptor: font.fontDescriptor, size: font.pointSize)
      times["width.ct.fontFromDescriptor"] = ctWidth([.font: tracked])
      times["width.ct.trailingWS"] = Double(CTLineGetTrailingWhitespaceWidth(
        CTLineCreateWithAttributedString(NSAttributedString(string: lineText, attributes: [.font: font]))))
      times["width.tk.glyphs"] = Double(tkGlyphs.length)
      // Where the last glyph of line 0 starts: TextKit, the engine's typesetter, a typesetter over the UIKit text.
      let lastGlyph = NSMaxRange(tkGlyphs) - 1
      let fragment = lm.lineFragmentRect(forGlyphAt: lastGlyph, effectiveRange: nil)
      times["lastX.textKit"] = Double(fragment.minX + lm.location(forGlyphAt: lastGlyph).x)
      func lastX(_ attributed: NSAttributedString) -> Double {
        let typesetter = CTTypesetterCreateWithAttributedString(attributed)
        let line = CTTypesetterCreateLine(typesetter, CFRange(location: 0, length: first.length))
        guard let run = (CTLineGetGlyphRuns(line) as! [CTRun]).last else { return -1 }
        let count = CTRunGetGlyphCount(run)
        var positions = [CGPoint](repeating: .zero, count: count)
        CTRunGetPositions(run, CFRange(location: 0, length: 0), &positions)
        return Double(positions.last?.x ?? -1)
      }
      let engineAttrs: [NSAttributedString.Key: Any] = [
        .font: font, NSAttributedString.Key(kCTForegroundColorFromContextAttributeName as String): true,
      ]
      times["lastX.engine"] = lastX(NSAttributedString(string: text, attributes: engineAttrs))
      times["lastX.uikitText"] = lastX(NSAttributedString(string: text, attributes: attributes))
      times["lastX.fontOnly"] = lastX(NSAttributedString(string: text, attributes: [.font: font]))
      times["lastX.tkLastGlyphIsSpace"] = (text as NSString).character(at: NSMaxRange(first) - 1) == 0x20 ? 1 : 0
      times["width.chars"] = Double(first.length)
    }
    // The first mismatched lines, as start/length pairs.
    var shown = 0
    for (i, (tk, ct)) in zip(tkRanges, ctRanges).enumerated() where tk != ct && shown < 3 {
      times["mismatch\(shown).line"] = Double(i)
      times["mismatch\(shown).tk"] = Double(tk.location) + Double(tk.length) / 1000
      times["mismatch\(shown).ct"] = Double(ct.location) + Double(ct.length) / 1000
      shown += 1
    }
    // TextKit without its default line break strategy (CoreText's suggestLineBreak has none).
    var plain = attributes
    let paragraph = (attributes[.paragraphStyle] as! NSParagraphStyle).mutableCopy() as! NSMutableParagraphStyle
    paragraph.lineBreakStrategy = []
    plain[.paragraphStyle] = paragraph
    let plainStack = makeContainer(width: width, height: height, tracks: false)
    plainStack.storage.setAttributedString(NSAttributedString(string: text, attributes: plain))
    plainStack.layoutManager.ensureLayout(for: plainStack.container)
    var plainRanges: [NSRange] = []
    let plm = plainStack.layoutManager
    plm.enumerateLineFragments(forGlyphRange: plm.glyphRange(for: plainStack.container)) { _, _, _, glyphs, _ in
      plainRanges.append(plm.characterRange(forGlyphRange: glyphs, actualGlyphRange: nil))
    }
    times["lines.mismatched.noStrategy"] =
      Double(zip(plainRanges, ctRanges).filter { $0 != $1 }.count + abs(plainRanges.count - ctRanges.count))
    times["lines.textKitVsNoStrategy"] =
      Double(zip(plainRanges, tkRanges).filter { $0 != $1 }.count + abs(plainRanges.count - tkRanges.count))
    times["lines.mismatched"] = Double(zip(tkRanges, ctRanges).filter { $0 != $1 }.count + abs(tkRanges.count - ctRanges.count))
    times["height.extraLineFragment"] = Double(lm.extraLineFragmentRect.height)
    times["text.endsWithNewline"] = text.hasSuffix("\n") ? 1 : 0
    var background = 0.0
    DispatchQueue.global(qos: .userInitiated).sync {
      let started = CACurrentMediaTime()
      let stack = makeContainer(width: width, height: height, tracks: false)
      stack.storage.setAttributedString(NSAttributedString(string: text, attributes: attributes))
      stack.layoutManager.ensureLayout(for: stack.container)
      background = (CACurrentMediaTime() - started) * 1000
    }
    times["textkit.backgroundLayout"] = background
    times["textBytes"] = Double(text.utf8.count)
    // The prepared-layout engines as shipped, twice each (the second warm).
    let bytes = Array(text.utf8)
    let coreTextWas = StreamTextPreparedLayout.coreText
    for (name, coreText) in [("engine.coreText", true), ("engine.textKit", false)] {
      StreamTextPreparedLayout.coreText = coreText
      for pass in ["cold", "warm"] {
        bytes.withUnsafeBufferPointer { buffer in
          time("\(name).\(pass)") {
            _ = StreamTextPreparedLayout(utf8: buffer.baseAddress!, count: buffer.count, bytes: UInt64(buffer.count),
                                         style: style, width: width, textKit: true)
          }
        }
      }
    }
    StreamTextPreparedLayout.coreText = coreTextWas
    // Kerning on long paragraphs: x of the last glyph of line 0, short vs long paragraph, both engines.
    let sentence = "Streaming text straight from the network into a native view, every token lands in a buffer. "
    func longParagraph(_ chars: Int) -> String {
      var out = ""
      while out.utf16.count < chars { out += sentence }
      return out
    }
    func tkLastX(_ string: String) -> Double {
      let stack = makeContainer(width: width, height: height, tracks: false)
      stack.storage.setAttributedString(NSAttributedString(string: string, attributes: attributes))
      let glyphs = stack.layoutManager.glyphRange(forBoundingRect: CGRect(x: 0, y: 0, width: width, height: 1), in: stack.container)
      var last = 0
      stack.layoutManager.enumerateLineFragments(forGlyphRange: NSRange(location: 0, length: 1)) { _, _, _, range, _ in last = NSMaxRange(range) - 2 }
      _ = glyphs
      return Double(stack.layoutManager.location(forGlyphAt: last).x)
    }
    func ctLastX(_ string: String) -> Double {
      let typesetter = CTTypesetterCreateWithAttributedString(NSAttributedString(string: string, attributes: [.font: attributes[.font]!]))
      let count = CTTypesetterSuggestLineBreak(typesetter, 0, Double(width))
      let line = CTTypesetterCreateLine(typesetter, CFRange(location: 0, length: count))
      var xs: [CGFloat] = []
      for run in CTLineGetGlyphRuns(line) as! [CTRun] {
        var positions = [CGPoint](repeating: .zero, count: CTRunGetGlyphCount(run))
        CTRunGetPositions(run, CFRange(location: 0, length: 0), &positions)
        xs += positions.map(\.x)
      }
      return Double(xs.count >= 2 ? xs[xs.count - 2] : -1)
    }
    if text.utf8.count < 1500 {
      for chars in [200, 1_000, 2_000, 4_000, 8_000, 16_000, 32_000, 64_000] {
        let string = longParagraph(chars)
        times["para.\(chars).tk"] = tkLastX(string)
        times["para.\(chars).ct"] = ctLastX(string)
      }
    }
    return times
  }

  private static func makeContainer(width: CGFloat, height: CGFloat = 0, tracks: Bool)
    -> (storage: NSTextStorage, layoutManager: NSLayoutManager, container: NSTextContainer) {
    let storage = NSTextStorage()
    let layoutManager = NSLayoutManager()
    layoutManager.allowsNonContiguousLayout = false
    storage.addLayoutManager(layoutManager)
    let container = NSTextContainer(size: CGSize(width: width, height: height))
    container.lineFragmentPadding = 0
    container.widthTracksTextView = tracks
    container.heightTracksTextView = tracks
    layoutManager.addTextContainer(container)
    return (storage, layoutManager, container)
  }

  private static func makeTextView(container: NSTextContainer, frame: CGRect = .zero) -> UITextView {
    UITextView(frame: frame, textContainer: container)
  }

  private static func configure(_ textView: UITextView, attributes: [NSAttributedString.Key: Any], frame: CGRect) {
    textView.isEditable = false
    textView.isScrollEnabled = false
    textView.showsVerticalScrollIndicator = false
    textView.showsHorizontalScrollIndicator = false
    textView.backgroundColor = .clear
    textView.textContainerInset = .zero
    textView.textContainer.lineFragmentPadding = 0
    textView.layoutManager.allowsNonContiguousLayout = false
    textView.typingAttributes = attributes
    textView.frame = frame
  }

  /// Lays out and draws the text view's layers now, as the next Core Animation commit would.
  private static func draw(_ view: UIView) {
    view.setNeedsLayout()
    view.layoutIfNeeded()
    func display(_ layer: CALayer) {
      layer.displayIfNeeded()
      layer.sublayers?.forEach(display)
    }
    display(view.layer)
    CATransaction.flush()
  }
}
