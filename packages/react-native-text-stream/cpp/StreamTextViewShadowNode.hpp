// Fabric component "StreamTextView": a text view that renders a TSB1 buffer and sizes itself to its text.
// The native view measures the laid-out text and reports the height through the state, so the
// view grows without the JS thread.
#pragma once

#include <cmath>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/propsConversions.h>
#include <react/renderer/graphics/Color.h>
#include <string>
#include <yoga/Yoga.h>

namespace facebook::react {

inline constexpr const char StreamTextViewComponentName[] = "StreamTextView";

/**
 * Reads `source` straight from the JS value: Fabric's folly-based prop conversion would lose an
 * ArrayBuffer. Needs RawPropsParser's JSI mode, which is the default since React Native 0.85.
 * Returns nullopt for props that arrived as folly::dynamic: once setNativeProps was used, Fabric
 * merges every later update through folly, and the buffer in it is gone, so keep the current one.
 */
inline std::optional<std::shared_ptr<jsi::MutableBuffer>> streamTextSourceFromRawValue(const RawValue &rawValue) {
  std::pair<jsi::Runtime *, jsi::Value> jsiValue;
  try {
    jsiValue = static_cast<std::pair<jsi::Runtime *, jsi::Value>>(rawValue);
  } catch (const std::exception &) {
    return std::nullopt;
  }
  auto &[runtime, value] = jsiValue;
  if (runtime == nullptr || !value.isObject()) {
    return nullptr;
  }
  auto object = value.getObject(*runtime);
  if (!object.isArrayBuffer(*runtime)) {
    return nullptr;
  }
  // Only a natively-backed buffer can be read off the JS thread; for one created in JS this is null.
  return object.getArrayBuffer(*runtime).tryGetMutableBuffer(*runtime);
}

class StreamTextViewProps final : public ViewProps {
 public:
  StreamTextViewProps() = default;
  StreamTextViewProps(
      const PropsParserContext &context,
      const StreamTextViewProps &sourceProps,
      const RawProps &rawProps)
      : ViewProps(context, sourceProps, rawProps),
        source(sourceProps.source),
        fontSize(convertRawProp(context, rawProps, "fontSize", sourceProps.fontSize, Float{15})),
        fontFamily(convertRawProp(context, rawProps, "fontFamily", sourceProps.fontFamily, {})),
        fontWeight(convertRawProp(context, rawProps, "fontWeight", sourceProps.fontWeight, {})),
        color(convertRawProp(context, rawProps, "color", sourceProps.color, SharedColor{})),
        lineHeight(convertRawProp(context, rawProps, "lineHeight", sourceProps.lineHeight, Float{0})),
        letterSpacing(convertRawProp(context, rawProps, "letterSpacing", sourceProps.letterSpacing, Float{0})),
        textAlign(convertRawProp(context, rawProps, "textAlign", sourceProps.textAlign, {})),
        selectable(convertRawProp(context, rawProps, "selectable", sourceProps.selectable, true)),
        selectionColor(convertRawProp(context, rawProps, "selectionColor", sourceProps.selectionColor, SharedColor{})),
        recordDisplayLog(convertRawProp(context, rawProps, "recordDisplayLog", sourceProps.recordDisplayLog, false)) {
    // Always look the key up: the parser only forwards keys that the constructor asks for.
    if (const RawValue *rawSource = rawProps.at("source", nullptr, nullptr)) {
      if (auto parsed = streamTextSourceFromRawValue(*rawSource)) {
        source = std::move(*parsed);
      }
    }
  }

  std::shared_ptr<jsi::MutableBuffer> source;
  Float fontSize{15};
  std::string fontFamily;
  std::string fontWeight;
  SharedColor color{};
  /** 0 = the font's own line height. */
  Float lineHeight{0};
  Float letterSpacing{0};
  std::string textAlign;
  bool selectable{true};
  SharedColor selectionColor{};
  bool recordDisplayLog{false};
};

class StreamTextViewEventEmitter final : public ViewEventEmitter {
 public:
  using ViewEventEmitter::ViewEventEmitter;

  void onStatusChange(std::string status, double bytes) const {
    dispatchEvent("statusChange", [status = std::move(status), bytes](jsi::Runtime &runtime) {
      auto payload = jsi::Object(runtime);
      payload.setProperty(runtime, "status", jsi::String::createFromUtf8(runtime, status));
      payload.setProperty(runtime, "bytes", bytes);
      return payload;
    });
  }
};

struct StreamTextViewState {
  Float height{0};
  int revision{0};
};

class StreamTextViewShadowNode final : public ConcreteViewShadowNode<
                                           StreamTextViewComponentName,
                                           StreamTextViewProps,
                                           StreamTextViewEventEmitter,
                                           StreamTextViewState> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  StreamTextViewShadowNode(const ShadowNode &sourceShadowNode, const ShadowNodeFragment &fragment)
      : ConcreteViewShadowNode(sourceShadowNode, fragment) {
    // Yoga caches measurements: a new height from the native view must invalidate them.
    const auto &source = static_cast<const StreamTextViewShadowNode &>(sourceShadowNode);
    if (getStateData().revision != source.getStateData().revision) {
      YGNodeMarkDirty(&yogaNode_);
    }
  }

  static ShadowNodeTraits BaseTraits() {
    auto traits = ConcreteViewShadowNode::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(ShadowNodeTraits::Trait::MeasurableYogaNode);
    return traits;
  }

  Size measureContent(const LayoutContext & /*layoutContext*/, const LayoutConstraints &constraints)
      const override {
    // Fills the available width; the height is whatever the native view last laid out.
    Float width = std::isfinite(constraints.maximumSize.width) ? constraints.maximumSize.width : 0;
    return constraints.clamp(Size{width, getStateData().height});
  }
};

using StreamTextViewComponentDescriptor = ConcreteComponentDescriptor<StreamTextViewShadowNode>;

} // namespace facebook::react
