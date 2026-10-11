// Fabric host view for "StreamTextView": hands the buffer to StreamTextRenderer and feeds the
// renderer's laid-out height back into the shadow node's state, so layout follows the text
// without the JS thread.

#import <React/RCTComponentViewFactory.h>
#import <React/RCTConversions.h>
#import <React/RCTViewComponentView.h>
#import <UIKit/UIKit.h>

// The pod also hosts Nitro objects; their umbrella header imports TextStream-Swift.h after the C++
// types it needs.
#import "TextStream-Swift-Cxx-Umbrella.hpp"
#include "../../cpp/StreamTextBuffers.hpp"
#include "../../cpp/StreamTextViewShadowNode.hpp"
#include <utility>

using namespace facebook;
using namespace facebook::react;

static NSString *_Nullable StreamTextNSString(const std::string &value) {
  return value.empty() ? nil : RCTNSStringFromString(value);
}

static StreamTextStyle *StreamTextStyleFromProps(const StreamTextViewProps &props) {
  StreamTextStyle *style = [StreamTextStyle new];
  style.fontSize = props.fontSize;
  style.fontFamily = StreamTextNSString(props.fontFamily);
  style.fontWeight = StreamTextNSString(props.fontWeight);
  style.textColor = RCTUIColorFromSharedColor(props.color);
  style.lineHeight = props.lineHeight;
  style.letterSpacing = props.letterSpacing;
  style.textAlign = StreamTextNSString(props.textAlign);
  return style;
}

@interface StreamTextComponentView : RCTViewComponentView
@end

@implementation StreamTextComponentView {
  StreamTextRenderer *_renderer;
  std::shared_ptr<const StreamTextViewShadowNode::ConcreteState> _state;
  // Keeps the buffer's memory alive while the renderer reads it.
  std::shared_ptr<jsi::MutableBuffer> _source;
  int _revision;
  CGFloat _height;
  // A status reported before the event emitter arrived (props are mounted first).
  NSString *_pendingStatus;
  double _pendingBytes;
  // A new `source` prop, attached in finalizeUpdates: once the layout metrics of the same
  // transaction are applied, so a mounted view lays its text out once, at its real width.
  std::shared_ptr<jsi::MutableBuffer> _propSource;
  BOOL _hasPropSource;
}

- (void)dealloc {
  // Before the ivars go, `_source` among them: the renderer's watcher reads that buffer.
  [_renderer invalidate];
}

+ (void)load {
  [super load];
  [RCTComponentViewFactory.currentComponentViewFactory registerComponentViewClass:self];
}

+ (ComponentDescriptorProvider)componentDescriptorProvider {
  return concreteComponentDescriptorProvider<StreamTextViewComponentDescriptor>();
}

+ (BOOL)shouldBeRecycled {
  return NO;
}

- (instancetype)initWithFrame:(CGRect)frame {
  if (self = [super initWithFrame:frame]) {
    static const auto defaultProps = std::make_shared<const StreamTextViewProps>();
    _props = defaultProps;
    _renderer = [[StreamTextRenderer alloc] initWithFrame:self.bounds];
    __weak StreamTextComponentView *weakSelf = self;
    _renderer.onHeightChange = ^(CGFloat height) {
      [weakSelf rendererDidChangeHeight:height];
    };
    _renderer.onStatusChange = ^(NSString *status, double bytes) {
      [weakSelf rendererDidChangeStatus:status bytes:bytes];
    };
    self.contentView = _renderer;
  }
  return self;
}

- (void)updateProps:(const Props::Shared &)props oldProps:(const Props::Shared &)oldProps {
  const auto &next = *std::static_pointer_cast<const StreamTextViewProps>(props);
  const auto &previous = *std::static_pointer_cast<const StreamTextViewProps>(_props);

  // Restyling touches the whole text, so only when a style prop actually changed.
  if (next.fontSize != previous.fontSize || next.fontFamily != previous.fontFamily ||
      next.fontWeight != previous.fontWeight || next.color != previous.color ||
      next.lineHeight != previous.lineHeight || next.letterSpacing != previous.letterSpacing ||
      next.textAlign != previous.textAlign) {
    _renderer.textStyle = StreamTextStyleFromProps(next);
    [_renderer applyTextStyle];
  }
  if (next.selectable != previous.selectable) {
    _renderer.selectable = next.selectable;
  }
  if (next.selectionColor != previous.selectionColor) {
    _renderer.selectionColor = RCTUIColorFromSharedColor(next.selectionColor);
  }
  if (next.recordDisplayLog != previous.recordDisplayLog || next.nativeId != previous.nativeId) {
    _renderer.displayLogKey = next.recordDisplayLog ? StreamTextNSString(next.nativeId) : nil;
  }
  // Compared with the previous props, not with what is attached: an `attach` command may have
  // replaced the buffer since, and an unrelated prop change must not undo it.
  if (next.source != previous.source) {
    _propSource = next.source;
    _hasPropSource = YES;
  }
  [super updateProps:props oldProps:oldProps];
}

- (void)finalizeUpdates:(RNComponentViewUpdateMask)updateMask {
  [super finalizeUpdates:updateMask];
  if (_hasPropSource) {
    _hasPropSource = NO;
    [self attachSource:std::exchange(_propSource, nullptr)];
  }
}

// A scroll view walks every descendant after each mount and scroll to clip subviews. This view has
// no React children, only the text view's own internals, so there is nothing to clip below it.
- (void)updateClippedSubviewsWithClipRect:(CGRect)clipRect relativeToView:(UIView *)clipView {
}

- (void)attachSource:(std::shared_ptr<jsi::MutableBuffer>)source {
  // The old buffer is released only after the renderer moved off it; the new one may already be
  // released inside attach, when it is finished and shown in one go.
  auto previous = std::exchange(_source, source);
  [_renderer attachWithBase:source ? source->data() : nullptr size:source ? (NSInteger)source->size() : 0];
}

// Commands from the JS wrapper's ref: `attach(id)` takes a buffer put in StreamTextBuffers.
- (void)handleCommand:(const NSString *)commandName args:(const NSArray *)args {
  if ([commandName isEqualToString:@"attach"]) {
    int bufferId = [args.firstObject intValue];
    [self attachSource:bufferId > 0 ? StreamTextBufferRegistry::shared().take(bufferId) : nullptr];
  } else if ([commandName isEqualToString:@"clear"]) {
    [self attachSource:nullptr];
  } else {
    [super handleCommand:commandName args:args];
  }
}

- (void)updateState:(const State::Shared &)state oldState:(const State::Shared &)oldState {
  _state = std::static_pointer_cast<const StreamTextViewShadowNode::ConcreteState>(state);
  // A height measured before the first state arrived.
  if (_state && _height > 0 && _state->getData().height != _height) {
    [self rendererDidChangeHeight:_height];
  }
}

- (void)rendererDidChangeHeight:(CGFloat)height {
  _height = height;
  if (!_state) {
    return;
  }
  _revision++;
  // Immediate: applied on this (main) thread; the default mode waits for the JS thread.
  _state->updateState(
      StreamTextViewState{static_cast<Float>(height), _revision}, EventQueue::UpdateMode::unstable_Immediate);
}

- (void)updateEventEmitter:(const EventEmitter::Shared &)eventEmitter {
  [super updateEventEmitter:eventEmitter];
  if (_pendingStatus) {
    NSString *status = _pendingStatus;
    _pendingStatus = nil;
    [self rendererDidChangeStatus:status bytes:_pendingBytes];
  }
}

- (void)rendererDidChangeStatus:(NSString *)status bytes:(double)bytes {
  if (![status isEqualToString:@"streaming"] && ![status isEqualToString:@"idle"]) {
    _source = nullptr; // the renderer has let go of it: all of its text is in the view
  }
  const auto emitter = std::static_pointer_cast<const StreamTextViewEventEmitter>(_eventEmitter);
  if (!emitter) {
    _pendingStatus = status;
    _pendingBytes = bytes;
    return;
  }
  emitter->onStatusChange(RCTStringFromNSString(status), bytes);
}

@end
