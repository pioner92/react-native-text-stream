// Fabric host view for "StreamTextView": hands the buffer to StreamTextRenderer and feeds the
// renderer's laid-out height back into the shadow node's state, so layout follows the text
// without the JS thread.

#import <React/RCTComponentViewFactory.h>
#import <React/RCTConversions.h>
#import <React/RCTViewComponentView.h>
#import <UIKit/UIKit.h>

// The pod still hosts Nitro objects; their umbrella header imports TextStream-Swift.h after the C++ types it needs.
#import "TextStream-Swift-Cxx-Umbrella.hpp"
#include "../../cpp/StreamTextViewShadowNode.hpp"

using namespace facebook;
using namespace facebook::react;

@interface StreamTextComponentView : RCTViewComponentView
@end

@implementation StreamTextComponentView {
  StreamTextRenderer *_renderer;
  std::shared_ptr<const StreamTextViewShadowNode::ConcreteState> _state;
  // Keeps the buffer's memory alive while the renderer reads it.
  std::shared_ptr<jsi::MutableBuffer> _source;
  int _revision;
  CGFloat _height;
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
    self.contentView = _renderer;
  }
  return self;
}

- (void)updateProps:(const Props::Shared &)props oldProps:(const Props::Shared &)oldProps {
  const auto &newProps = *std::static_pointer_cast<const StreamTextViewProps>(props);
  const auto &previous = *std::static_pointer_cast<const StreamTextViewProps>(_props);

  if (newProps.fontSize != previous.fontSize) {
    _renderer.fontSize = newProps.fontSize;
  }
  if (newProps.color != previous.color) {
    _renderer.textColor = RCTUIColorFromSharedColor(newProps.color);
  }
  if (newProps.nativeId != previous.nativeId) {
    _renderer.displayLogKey = newProps.nativeId.empty() ? nil : RCTNSStringFromString(newProps.nativeId);
  }
  if (newProps.source != _source) {
    auto next = newProps.source;
    [_renderer attachWithBase:next ? next->data() : nullptr size:next ? (NSInteger)next->size() : 0];
    _source = std::move(next); // released only after the renderer moved off the old buffer
  }
  [super updateProps:props oldProps:oldProps];
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

@end
