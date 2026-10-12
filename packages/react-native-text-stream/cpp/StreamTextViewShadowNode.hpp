// Fabric component "StreamTextView": a text view that renders a TSB1 buffer and sizes itself to its text.
// The native view measures the laid-out text and reports the height through the state, so the
// view grows without the JS thread.
#pragma once

#include <array>
#include <cmath>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
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

class StreamTextViewProps;
/** Starts laying out a new finished `source` in the background (ios/fabric/StreamTextPrepared.mm). */
void streamTextPrepareEarly(const StreamTextViewProps &props);

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
        recordDisplayLog(convertRawProp(context, rawProps, "recordDisplayLog", sourceProps.recordDisplayLog, false)),
        sourceGeneration(convertRawProp(context, rawProps, "sourceGeneration", sourceProps.sourceGeneration, 0)) {
    // Always look the key up: the parser only forwards keys that the constructor asks for.
    if (const RawValue *rawSource = rawProps.at("source", nullptr, nullptr)) {
      if (auto parsed = streamTextSourceFromRawValue(*rawSource)) {
        source = std::move(*parsed);
      }
    }
    if (source && source != sourceProps.source) {
      streamTextPrepareEarly(*this);
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
  /** Set by the JS wrapper with each new `source`; status events carry it back. */
  int sourceGeneration{0};
};

class StreamTextViewEventEmitter final : public ViewEventEmitter {
 public:
  using ViewEventEmitter::ViewEventEmitter;

  void onStatusChange(std::string status, double bytes, int generation) const {
    dispatchEvent("statusChange", [status = std::move(status), bytes, generation](jsi::Runtime &runtime) {
      auto payload = jsi::Object(runtime);
      payload.setProperty(runtime, "status", jsi::String::createFromUtf8(runtime, status));
      payload.setProperty(runtime, "bytes", bytes);
      payload.setProperty(runtime, "generation", generation);
      return payload;
    });
  }
};

struct StreamTextPreparedHandle; // ios/fabric/StreamTextPrepared.mm
/** A layout of a finished `source`, prepared in measure; the view draws it without a text view. */
using StreamTextPreparedRef = std::shared_ptr<const StreamTextPreparedHandle>;

struct StreamTextViewState {
  Float height{0};
  int revision{0};
  /**
   * The `source` prop in effect when the view reported `height`. Until that prop changes, the view
   * decides what it shows (it streamed the text, swapped in a text view, or an `attach`/`clear`
   * command replaced it), so its height wins over a prepared layout of the prop.
   */
  const void *propSource{nullptr};
  StreamTextPreparedRef prepared;
};

/**
 * A layout of these props' `source` at exactly `width` if it is finished and small: `current` when
 * it still fits, a cached one, or a new one. Else null.
 */
StreamTextPreparedRef
streamTextPrepare(const StreamTextViewProps &props, Float width, const StreamTextPreparedRef &current);
Float streamTextPreparedHeight(const StreamTextPreparedRef &prepared);
Float streamTextPreparedWidth(const StreamTextPreparedRef &prepared);
/** `prepared` is of these props' text and style, at `width` give or take Yoga's pixel rounding. */
bool streamTextPreparedFits(const StreamTextPreparedRef &prepared, const StreamTextViewProps &props, Float width);

class StreamTextViewShadowNode final : public ConcreteViewShadowNode<
                                           StreamTextViewComponentName,
                                           StreamTextViewProps,
                                           StreamTextViewEventEmitter,
                                           StreamTextViewState> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  StreamTextViewShadowNode(const ShadowNode &sourceShadowNode, const ShadowNodeFragment &fragment)
      : ConcreteViewShadowNode(sourceShadowNode, fragment) {
    // Yoga caches measurements: a new height from the native view, a new text or a style that moves
    // the lines must invalidate them (none of these are Yoga styles).
    const auto &source = static_cast<const StreamTextViewShadowNode &>(sourceShadowNode);
    const auto &props = getConcreteProps();
    const auto &previous = source.getConcreteProps();
    if (getStateData().revision != source.getStateData().revision || props.source != previous.source ||
        props.fontSize != previous.fontSize || props.fontFamily != previous.fontFamily ||
        props.fontWeight != previous.fontWeight || props.lineHeight != previous.lineHeight ||
        props.letterSpacing != previous.letterSpacing || props.textAlign != previous.textAlign) {
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
    // Fills the available width; the height is that of a prepared layout, or else whatever the
    // native view last laid out.
    Float width = std::isfinite(constraints.maximumSize.width) ? constraints.maximumSize.width : 0;
    auto prepared = viewHeightWins() ? nullptr : streamTextPrepare(getConcreteProps(), width, getStateData().prepared);
    if (prepared) {
      measured_[measuredNext_++ % measured_.size()] = prepared;
    }
    return constraints.clamp(Size{width, prepared ? streamTextPreparedHeight(prepared) : getStateData().height});
  }

  void layout(LayoutContext layoutContext) override {
    ConcreteViewShadowNode::layout(layoutContext);
    // The state carries to the view the layout Yoga sized this frame with, and its height to later
    // Yoga passes on any thread.
    Float width = getLayoutMetrics().getContentFrame().size.width;
    const auto &props = getConcreteProps();
    const auto &state = getStateData();
    StreamTextPreparedRef prepared;
    if (!viewHeightWins()) {
      for (const auto &candidate : measured_) {
        if (streamTextPreparedFits(candidate, props, width) &&
            (!prepared ||
             std::fabs(streamTextPreparedWidth(candidate) - width) < std::fabs(streamTextPreparedWidth(prepared) - width))) {
          prepared = candidate;
        }
      }
      // Yoga reused an earlier measurement: the layout from then is still in the state.
      if (!prepared && streamTextPreparedFits(state.prepared, props, width)) {
        prepared = state.prepared;
      }
      if (!prepared) {
        prepared = streamTextPrepare(props, width, nullptr);
      }
    }
    if (prepared != state.prepared || (prepared && state.height != streamTextPreparedHeight(prepared))) {
      setStateData(StreamTextViewState{
          prepared ? streamTextPreparedHeight(prepared) : state.height, state.revision, state.propSource, prepared});
    }
  }

 private:
  /** The layouts measure produced (Yoga may measure at a few widths), for layout() to pick from. */
  mutable std::array<StreamTextPreparedRef, 3> measured_;
  mutable size_t measuredNext_{0};

  bool viewHeightWins() const {
    const auto &state = getStateData();
    const auto &source = getConcreteProps().source;
    return state.revision > 0 && !state.prepared && state.propSource == (source ? source->data() : nullptr);
  }
};

using StreamTextViewComponentDescriptor = ConcreteComponentDescriptor<StreamTextViewShadowNode>;

} // namespace facebook::react
