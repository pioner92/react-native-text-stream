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
#include "../../cpp/StreamTextFetch.hpp"
#include "../../cpp/StreamTextStreams.hpp"
#include <atomic>
#include "../../cpp/StreamTextViewShadowNode.hpp"
#include <utility>

using namespace facebook;
using namespace facebook::react;

// Benchmarks: times a phase when StreamTextRenderStats is enabled (only with STREAMTEXT_TESTING).
#ifndef STREAMTEXT_TESTING
#define STREAMTEXT_PHASE(name, body) \
  do {                               \
    body;                            \
  } while (0)
#else
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
#endif

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
  uint64_t _pendingSerial;
  // The JS wrapper's generation of the attached source, sent back with each status event.
  int _generation;
  int _propGeneration;
  // A new `source` prop, attached in finalizeUpdates: once the layout metrics of the same
  // transaction are applied, so a mounted view lays its text out once, at its real width.
  std::shared_ptr<jsi::MutableBuffer> _propSource;
  BOOL _hasPropSource;
  // The buffer the renderer shows (the heights it reports are for it), even once `_source` is released.
  const void *_attachedData;
  // A buffer from streamFetch() wakes the renderer on every write instead of being polled.
  std::shared_ptr<StreamTextMappedBuffer> _observed;
  uint64_t _observerToken;
  // The `streamId` prop, listened to: a request started (or replaced) under it later is shown too.
  std::string _streamId;
  std::string _listenedStreamId;
  uint64_t _streamToken;
  BOOL _followsStream;
  // Which streamFetch() buffer was attached last (0: another kind), kept once it is let go of.
  uint64_t _attachedSerial;
}

- (void)dealloc {
  _followsStream = NO;
  [self updateStreamSubscription];
  [self stopObserving];
  // Before the ivars go, `_source` among them: the renderer's watcher reads that buffer.
  [_renderer invalidate];
}

/** Subscribes only while the prop, rather than an imperative command, owns the named stream. */
- (void)updateStreamSubscription {
  if (!_listenedStreamId.empty()) {
    StreamTextStreams::shared().unlisten(_listenedStreamId, _streamToken);
    _listenedStreamId.clear();
  }
  if (!_followsStream || _streamId.empty()) {
    return;
  }
  __weak StreamTextComponentView *weakSelf = self;
  auto wake = [weakSelf] {
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf streamPublished];
    });
  };
  _listenedStreamId = _streamId;
  _streamToken = StreamTextStreams::shared().listen(_streamId, wake);
  // Published between the props' lookup and now: not missed.
  wake();
}

/** A request streams under `streamId` (a new one, or one that replaced it). Main thread. */
- (void)streamPublished {
  if (!_followsStream || _streamId.empty()) {
    return;
  }
  auto buffer = StreamTextStreams::shared().find(_streamId);
  // Already shown, or about to be (the props' own lookup found it): attaching again would restart it.
  if (!buffer || buffer->serial() == _attachedSerial || (_hasPropSource && buffer == _propSource)) {
    return;
  }
  [self attachSource:buffer prepared:nil generation:_propGeneration];
}

- (void)stopObserving {
  if (_observed) {
    _observed->unobserve(_observerToken);
    _observed = nullptr;
  }
}

/** Wakes the renderer, on the main thread, once per batch of writes to `buffer`. */
- (BOOL)observe:(const std::shared_ptr<jsi::MutableBuffer> &)source {
  [self stopObserving];
  auto mapped = std::dynamic_pointer_cast<StreamTextMappedBuffer>(source);
  if (!mapped) {
    return NO;
  }
  __weak StreamTextRenderer *renderer = _renderer;
  auto pending = std::make_shared<std::atomic<bool>>(false);
  _observerToken = mapped->observe([renderer, pending] {
    if (pending->exchange(true)) {
      return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      pending->store(false);
      [renderer bufferDidChange];
    });
  });
  _observed = std::move(mapped);
  return YES;
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
#ifdef STREAMTEXT_TESTING
  if (next.recordDisplayLog != previous.recordDisplayLog || next.nativeId != previous.nativeId) {
    _renderer.displayLogKey = next.recordDisplayLog ? StreamTextNSString(next.nativeId) : nil;
  }
#endif
  // Source identity is independent of its pointer: named A -> missing named B and explicit A ->
  // named B both need to detach what was previously visible, even if their prop pointers are null.
  BOOL sourceIdentityChanged = next.sourceGeneration != previous.sourceGeneration ||
      next.sourceOwner != previous.sourceOwner || next.streamId != previous.streamId;
  if (sourceIdentityChanged) {
    _propSource = next.source;
    _propGeneration = next.sourceGeneration;
    _hasPropSource = YES;
    _streamId = next.streamId;
    _followsStream = next.sourceOwner == 2;
    [self updateStreamSubscription];
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
  auto mapped = std::dynamic_pointer_cast<StreamTextMappedBuffer>(source);
  _attachedSerial = mapped ? mapped->serial() : 0;
  _generation = generation;
  // The old buffer is released only after the renderer moved off it; the new one may already be
  // released inside attach, when it is finished and shown in one go.
  auto previous = std::exchange(_source, source);
  // Subscribed before attaching: a write after the renderer's first read still wakes it.
  BOOL pushed = [self observe:source];
  [_renderer attachWithBase:source ? source->data() : nullptr
                       size:source ? (NSInteger)source->size() : 0
                   prepared:prepared
                     pushed:pushed];
}

// Commands from the JS wrapper's ref: `attach(id)` takes a buffer put in StreamTextBuffers.
- (void)handleCommand:(const NSString *)commandName args:(const NSArray *)args {
  if ([commandName isEqualToString:@"attach"]) {
    int bufferId = [args.firstObject intValue];
    int generation = args.count > 1 ? [args[1] intValue] : 0;
    _followsStream = NO;
    [self updateStreamSubscription];
    [self attachSource:bufferId > 0 ? StreamTextBufferRegistry::shared().take(bufferId) : nullptr
              prepared:nil
            generation:generation];
  } else if ([commandName isEqualToString:@"clear"]) {
    _followsStream = NO;
    [self updateStreamSubscription];
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
      emitter->onStatusChange(RCTStringFromNSString(status), _pendingBytes, _pendingGeneration, double(_pendingSerial));
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
    _pendingSerial = _attachedSerial;
    return;
  }
  // The renderer reports only about what it shows: the source attached last.
  emitter->onStatusChange(RCTStringFromNSString(status), bytes, _generation, double(_attachedSerial));
}

@end
