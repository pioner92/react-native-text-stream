// Fabric host view for "StreamTextView": hands the buffer to StreamTextRenderer and feeds the
// renderer's laid-out height back into the shadow node's state, so layout follows the text
// without the JS thread.

#import <React/RCTComponentViewFactory.h>
#import <React/RCTConversions.h>
#import <React/RCTViewComponentView.h>
#import <UIKit/UIKit.h>

#import "StreamTextSwift.h"
#import "StreamTextPrepared.h"
#include "../../cpp/StreamTextBuffers.hpp"
#include "../../cpp/StreamTextViewShadowNode.hpp"
#include <utility>

using namespace facebook;
using namespace facebook::react;

// Benchmarks: times a phase when StreamTextRenderStats is enabled.
#define STREAMTEXT_PHASE(name, body)                                                       \
  do {                                                                                     \
    if (StreamTextRenderStats.enabled) {                                                   \
      CFTimeInterval started_ = CACurrentMediaTime();                                      \
      body;                                                                                \
      [StreamTextRenderStats.shared addPhase:name time:CACurrentMediaTime() - started_];   \
    } else {                                                                               \
      body;                                                                                \
    }                                                                                      \
  } while (0)

static NSString *_Nullable StreamTextNSString(const std::string &value) {
  return value.empty() ? nil : RCTNSStringFromString(value);
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
  int _pendingGeneration;
  // The JS wrapper's generation of the attached source, sent back with each status event.
  int _generation;
  int _propGeneration;
  // A new `source` prop, attached in finalizeUpdates: once the layout metrics of the same
  // transaction are applied, so a mounted view lays its text out once, at its real width.
  std::shared_ptr<jsi::MutableBuffer> _propSource;
  BOOL _hasPropSource;
  // The buffer the renderer shows (the heights it reports are for it), even once `_source` is released.
  const void *_attachedData;
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
    STREAMTEXT_PHASE(@"renderer.init", _renderer = [[StreamTextRenderer alloc] initWithFrame:self.bounds]);
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
  BOOL layoutChanged = next.fontSize != previous.fontSize || next.fontFamily != previous.fontFamily ||
      next.fontWeight != previous.fontWeight || next.lineHeight != previous.lineHeight ||
      next.letterSpacing != previous.letterSpacing || next.textAlign != previous.textAlign;
  if (layoutChanged || next.color != previous.color) {
    STREAMTEXT_PHASE(@"props.style", {
      _renderer.textStyle = StreamTextStyleFromProps(next);
      [_renderer applyTextStyleWithLayoutChanged:layoutChanged];
    });
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
    _propGeneration = next.sourceGeneration;
    _hasPropSource = YES;
  }
  [super updateProps:props oldProps:oldProps];
}

- (void)finalizeUpdates:(RNComponentViewUpdateMask)updateMask {
  [super finalizeUpdates:updateMask];
  // The layout the shadow node prepared for the current `source` prop, if any (in the state).
  const auto &props = *std::static_pointer_cast<const StreamTextViewProps>(_props);
  StreamTextPreparedLayout *prepared = _state ? StreamTextPreparedLayoutOf(_state->getData().prepared, props.source) : nil;
  if (_hasPropSource) {
    _hasPropSource = NO;
    STREAMTEXT_PHASE(@"attach", [self attachSource:std::exchange(_propSource, nullptr)
                                          prepared:prepared
                                        generation:_propGeneration]);
  } else {
    // Same text, maybe a new width or style.
    [_renderer updatePrepared:prepared];
  }
}

// A scroll view walks every descendant as it scrolls: a tall canvas draws the tiles coming into view.
- (void)updateClippedSubviewsWithClipRect:(CGRect)clipRect relativeToView:(UIView *)clipView {
  [_renderer visibleAreaChanged];
}

- (void)didMoveToWindow {
  [super didMoveToWindow];
  // Unmounted after showing a finished `source` in a text view: lay it out in the background for
  // the next mount (texts that stay on screen never pay for it).
  if (!self.window && !_source && _attachedData) {
    auto props = std::static_pointer_cast<const StreamTextViewProps>(_props);
    if (props && props->source && props->source->data() == _attachedData && !(_state && _state->getData().prepared)) {
      StreamTextPrepareInBackground(props, _renderer.bounds.size.width);
    }
  }
}

// `prepared`: the shadow node's layout of a finished `source` prop; an `attach` command has none.
- (void)attachSource:(std::shared_ptr<jsi::MutableBuffer>)source
            prepared:(StreamTextPreparedLayout *)prepared
          generation:(int)generation {
  _attachedData = source ? source->data() : nullptr;
  _generation = generation;
  // The old buffer is released only after the renderer moved off it; the new one may already be
  // released inside attach, when it is finished and shown in one go.
  auto previous = std::exchange(_source, source);
  [_renderer attachWithBase:source ? source->data() : nullptr
                       size:source ? (NSInteger)source->size() : 0
                   prepared:prepared];
}

// Commands from the JS wrapper's ref: `attach(id)` takes a buffer put in StreamTextBuffers.
- (void)handleCommand:(const NSString *)commandName args:(const NSArray *)args {
  if ([commandName isEqualToString:@"attach"]) {
    int bufferId = [args.firstObject intValue];
    int generation = args.count > 1 ? [args[1] intValue] : 0;
    [self attachSource:bufferId > 0 ? StreamTextBufferRegistry::shared().take(bufferId) : nullptr
              prepared:nil
            generation:generation];
  } else if ([commandName isEqualToString:@"clear"]) {
    [self attachSource:nullptr prepared:nil generation:args.count > 0 ? [args.firstObject intValue] : 0];
  } else {
    [super handleCommand:commandName args:args];
  }
}

- (void)updateState:(const State::Shared &)state oldState:(const State::Shared &)oldState {
  _state = std::static_pointer_cast<const StreamTextViewShadowNode::ConcreteState>(state);
  // A height the shadow node prepared itself is the view's height too.
  if (_state && _state->getData().prepared) {
    _height = _state->getData().height;
    return;
  }
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
  // The view's height is for whatever it shows now: it wins until the `source` prop changes.
  const auto &source = std::static_pointer_cast<const StreamTextViewProps>(_props)->source;
  const void *propSource = source ? source->data() : nullptr;
  // Immediate: applied on this (main) thread; the default mode waits for the JS thread.
  STREAMTEXT_PHASE(@"height.commit", _state->updateState(
      StreamTextViewState{static_cast<Float>(height), _revision, propSource, nullptr},
      EventQueue::UpdateMode::unstable_Immediate));
}

- (void)updateEventEmitter:(const EventEmitter::Shared &)eventEmitter {
  [super updateEventEmitter:eventEmitter];
  if (_pendingStatus) {
    NSString *status = _pendingStatus;
    _pendingStatus = nil;
    const auto emitter = std::static_pointer_cast<const StreamTextViewEventEmitter>(_eventEmitter);
    if (emitter) {
      emitter->onStatusChange(RCTStringFromNSString(status), _pendingBytes, _pendingGeneration);
    }
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
    _pendingGeneration = _generation;
    return;
  }
  // The renderer reports only about what it shows: the source attached last.
  emitter->onStatusChange(RCTStringFromNSString(status), bytes, _generation);
}

@end
