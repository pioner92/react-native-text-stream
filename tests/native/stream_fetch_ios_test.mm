#include <cassert>
#include <iostream>
#include "../../packages/react-native-text-stream/ios/fabric/StreamTextFetch.mm"
using namespace facebook::react;
struct Listener : StreamTextFetchListener {
  int responses = 0, completions = 0;
  void onResponse(int, StreamTextHeaders, std::string) override { ++responses; }
  void onComplete(std::string) override { ++completions; }
};
int main() { @autoreleasepool {
  for (int capacity = 1; capacity <= 8; ++capacity) for (int length = 0; length <= 12; ++length) {
    for (int split = 0; split <= length; ++split) {
      auto buffer = StreamTextMappedBuffer::create(capacity);
      auto listener = std::make_shared<Listener>();
      auto delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener];
      std::string body(length, 'x');
      [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:body.data() length:split]];
      [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:body.data() + split length:length - split]];
      [delegate URLSession:nil task:nil didCompleteWithError:nil];
      auto committed = tsb_committed(buffer->data());
      assert(committed == std::min(capacity, length));
      auto status = tsb_status(buffer->data());
      assert(status == (length > capacity ? TSB_STATUS_OVERFLOW : TSB_STATUS_DONE));
      [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:"late" length:4]];
      [delegate cancelTask:nil];
      [delegate URLSession:nil task:nil didCompleteWithError:nil];
      assert(tsb_committed(buffer->data()) == committed && tsb_status(buffer->data()) == status);
      assert(listener->completions == 1);
    }
  }
  auto buffer = StreamTextMappedBuffer::create(8);
  auto listener = std::make_shared<Listener>();
  int wakes = 0;
  buffer->observe([&] { ++wakes; });
  StreamTextFetchRequest invalid; invalid.url = "not a url"; invalid.method = "GET";
  assert(!streamTextStartFetch(invalid, buffer, listener));
  assert(wakes == 1 && listener->completions == 1 && tsb_status(buffer->data()) == TSB_STATUS_ERROR);

  buffer = StreamTextMappedBuffer::create(8); listener = std::make_shared<Listener>();
  auto delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener];
  [delegate cancelTask:nil];
  [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:"late" length:4]];
  [delegate URLSession:nil task:nil didCompleteWithError:nil];
  assert(tsb_committed(buffer->data()) == 0 && tsb_status(buffer->data()) == TSB_STATUS_CANCELLED && listener->completions == 1);
  std::cout << "streamFetch iOS capacity/splits, late callbacks, cancel and startup error tests passed\n";
} }
