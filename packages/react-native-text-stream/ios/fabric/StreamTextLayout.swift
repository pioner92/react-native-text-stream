import UIKit

/// Layout constants.
enum StreamTextLayout {
  /// The text view's frame never follows the text line by line: a text view whose frame grows
  /// re-lays out all of its text. It starts this tall (about 4 MB of 14 pt text in a chat bubble)
  /// and doubles when the text gets close, which re-lays out everything once (~0.4 s per MB).
  /// UIKit only draws the visible part; 4M pt stays below the 2^24 px where Core Animation's
  /// float geometry starts losing precision.
  static let textViewHeight: CGFloat = 4_000_000
  /// A finished buffer up to this size is shown in a single frame when attached.
  static let instantFillBytes: UInt64 = 64 * 1024
}

/// The text style the component view hands to the renderer; it builds the text attributes.
@objc(StreamTextStyle)
public final class StreamTextStyle: NSObject {
  @objc public var fontSize: CGFloat = 15
  @objc public var fontFamily: String?
  @objc public var fontWeight: String?
  @objc public var textColor: UIColor?
  /// 0 = the font's own line height.
  @objc public var lineHeight: CGFloat = 0
  @objc public var letterSpacing: CGFloat = 0
  @objc public var textAlign: String?

  func attributes() -> [NSAttributedString.Key: Any] {
    let font = makeFont()
    let paragraph = NSMutableParagraphStyle()
    switch textAlign {
    case "left": paragraph.alignment = .left
    case "right": paragraph.alignment = .right
    case "center": paragraph.alignment = .center
    case "justify": paragraph.alignment = .justified
    default: paragraph.alignment = .natural
    }
    var attributes: [NSAttributedString.Key: Any] = [
      .font: font,
      .foregroundColor: textColor ?? .label,
      .paragraphStyle: paragraph,
    ]
    if lineHeight > 0 {
      paragraph.minimumLineHeight = lineHeight
      paragraph.maximumLineHeight = lineHeight
      // TextKit puts the extra height above the glyphs; centre them like RN <Text> does.
      attributes[.baselineOffset] = (lineHeight - font.lineHeight) / 2
    }
    if letterSpacing != 0 {
      attributes[.kern] = letterSpacing
    }
    return attributes
  }

  private func makeFont() -> UIFont {
    let size = fontSize > 0 ? fontSize : 15
    let weight = parseFontWeight(fontWeight)
    guard let family = fontFamily, !family.isEmpty else {
      return .systemFont(ofSize: size, weight: weight ?? .regular)
    }
    // A PostScript name ("Menlo-Bold") resolves directly; a family name goes through a descriptor.
    if weight == nil, let named = UIFont(name: family, size: size) { return named }
    var descriptor = UIFontDescriptor(fontAttributes: [.family: family])
    if let weight {
      descriptor = descriptor.addingAttributes([.traits: [UIFontDescriptor.TraitKey.weight: weight]])
    }
    return UIFont(descriptor: descriptor, size: size)
  }
}

/// "normal", "bold" or "100"…"900", as in React Native.
private func parseFontWeight(_ value: String?) -> UIFont.Weight? {
  switch value {
  case "100": return .ultraLight
  case "200": return .thin
  case "300": return .light
  case "normal", "400": return .regular
  case "500": return .medium
  case "600": return .semibold
  case "bold", "700": return .bold
  case "800": return .heavy
  case "900": return .black
  default: return nil
  }
}
