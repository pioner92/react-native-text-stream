// Fabric component "StreamTextView": a text view that renders a TSB1 buffer and sizes itself to its text.
// The native view measures the laid-out text and reports the height through the state, so the
// view grows without the JS thread.
#pragma once

#include <cmath>
#include <jsi/jsi.h>
#include <memory>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/propsConversions.h>
#include <react/renderer/graphics/Color.h>
#include <yoga/Yoga.h>

namespace facebook::react {

inline constexpr const char StreamTextViewComponentName[] = "StreamTextView";

/**
 * Reads `source` straight from the JS value: Fabric's folly-based prop conversion would lose an
 * ArrayBuffer. Needs RawPropsParser's JSI mode, which is the default since React Native 0.85.
 */
inline std::shared_ptr<jsi::MutableBuffer> streamTextSourceFromRawValue(const RawValue &rawValue) {
  std::pair<jsi::Runtime *, jsi::Value> jsiValue;
  try {
    jsiValue = static_cast<std::pair<jsi::Runtime *, jsi::Value>>(rawValue);
  } catch (const std::exception &) {
    return nullptr; // props that arrived as folly::dynamic carry no buffer
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
        color(convertRawProp(context, rawProps, "color", sourceProps.color, SharedColor{})) {
    // Always look the key up: the parser only forwards keys that the constructor asks for.
    if (const RawValue *rawSource = rawProps.at("source", nullptr, nullptr)) {
      source = streamTextSourceFromRawValue(*rawSource);
    }
  }

  std::shared_ptr<jsi::MutableBuffer> source;
  Float fontSize{15};
  SharedColor color{};
};

struct StreamTextViewState {
  Float height{0};
  int revision{0};
};

class StreamTextViewShadowNode final : public ConcreteViewShadowNode<
                                           StreamTextViewComponentName,
                                           StreamTextViewProps,
                                           ViewEventEmitter,
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
