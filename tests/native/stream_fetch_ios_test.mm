#include <cassert>
#include <iostream>
#include "../../packages/react-native-text-stream/ios/fabric/StreamTextFetch.mm"
using namespace facebook::react;
struct Listener : StreamTextFetchListener {
  int responses = 0, completions = 0;
  std::string error;
  void onResponse(int, StreamTextHeaders, std::string) override { ++responses; }
  void onComplete(std::string message) override { ++completions; error = std::move(message); }
};
int main() { @autoreleasepool {
  for (int capacity = 1; capacity <= 8; ++capacity) for (int length = 0; length <= 12; ++length) {
    for (int split = 0; split <= length; ++split) {
      auto buffer = StreamTextMappedBuffer::create(capacity);
      auto listener = std::make_shared<Listener>();
      auto delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:StreamTextParserOptions{}];
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
  auto delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:StreamTextParserOptions{}];
  [delegate cancelTask:nil];
  [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:"late" length:4]];
  [delegate URLSession:nil task:nil didCompleteWithError:nil];
  assert(tsb_committed(buffer->data()) == 0 && tsb_status(buffer->data()) == TSB_STATUS_CANCELLED && listener->completions == 1);

  StreamTextParserOptions parsed;
  parsed.protocol = StreamTextProtocol::SSE;
  parsed.extraction = StreamTextExtraction::OpenAIChat;
  const std::string events = "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"hello\"}}]}\r\n\r\ndata: [DONE]\r\n\r\n";
  for (int capacity = 1; capacity <= 8; ++capacity) {
    for (size_t split = 0; split <= events.size(); ++split) {
      buffer = StreamTextMappedBuffer::create(capacity); listener = std::make_shared<Listener>();
      delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:parsed];
      [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:events.data() length:split]];
      [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:events.data() + split length:events.size() - split]];
      // Protocol completion must finish immediately, without waiting for EOF or its cancellation.
      const auto status = capacity < 5 ? TSB_STATUS_OVERFLOW : TSB_STATUS_DONE;
      assert(tsb_status(buffer->data()) == status && listener->completions == 1 && listener->error.empty());
      assert(tsb_committed(buffer->data()) == std::min(capacity, 5));
      assert(std::string(reinterpret_cast<char *>(buffer->data()) + TSB_HEADER_SIZE, tsb_committed(buffer->data())) == std::string("hello").substr(0, capacity));
      [delegate cancelTask:nil];
      [delegate URLSession:nil task:nil didCompleteWithError:nil];
      assert(tsb_status(buffer->data()) == status && listener->completions == 1);
    }
  }
  for (const std::string input : {"data: {invalid}\n\n", "data: {\"choices\":[]}\n\n"}) {
    buffer = StreamTextMappedBuffer::create(8); listener = std::make_shared<Listener>();
    delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:parsed];
    [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:input.data() length:input.size()]];
    [delegate URLSession:nil task:nil didCompleteWithError:nil];
    assert(tsb_status(buffer->data()) == TSB_STATUS_ERROR && !listener->error.empty() && listener->completions == 1);
  }
  // Cancellation cannot flush or diagnose a partially received provider event.
  buffer = StreamTextMappedBuffer::create(8); listener = std::make_shared<Listener>();
  delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:parsed];
  [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:events.data() length:20]];
  [delegate cancelTask:nil];
  [delegate URLSession:nil task:nil didCompleteWithError:nil];
  assert(tsb_status(buffer->data()) == TSB_STATUS_CANCELLED && tsb_committed(buffer->data()) == 0 && listener->error.empty());

  // The final NDJSON record flushes only on a successful EOF, and preserves embedded NUL.
  parsed.protocol = StreamTextProtocol::NDJSON;
  parsed.extraction = StreamTextExtraction::JsonPointer;
  parsed.textPointer = "/text";
  const std::string record = "{\"text\":\"A\\u0000🚀\"}";
  buffer = StreamTextMappedBuffer::create(16); listener = std::make_shared<Listener>();
  delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:parsed];
  for (const auto &byte : record)
    [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:&byte length:1]];
  assert(tsb_committed(buffer->data()) == 0);
  [delegate URLSession:nil task:nil didCompleteWithError:nil];
  assert(tsb_status(buffer->data()) == TSB_STATUS_DONE && tsb_committed(buffer->data()) == 6);
  assert(std::string(reinterpret_cast<char *>(buffer->data()) + TSB_HEADER_SIZE, 6) == std::string("A\0🚀", 6));

  // Transport errors must not publish an otherwise valid unterminated NDJSON record.
  for (const NSInteger code : {NSURLErrorNetworkConnectionLost, NSURLErrorCancelled}) {
    buffer = StreamTextMappedBuffer::create(16); listener = std::make_shared<Listener>();
    delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:parsed];
    [delegate URLSession:nil dataTask:nil didReceiveData:[NSData dataWithBytes:record.data() length:record.size()]];
    auto error = [NSError errorWithDomain:NSURLErrorDomain code:code userInfo:nil];
    [delegate URLSession:nil task:nil didCompleteWithError:error];
    assert(tsb_committed(buffer->data()) == 0 && listener->completions == 1);
    assert(tsb_status(buffer->data()) == (code == NSURLErrorCancelled ? TSB_STATUS_CANCELLED : TSB_STATUS_ERROR));
    [delegate URLSession:nil task:nil didCompleteWithError:nil];
    assert(tsb_committed(buffer->data()) == 0 && listener->completions == 1);
  }

  buffer = StreamTextMappedBuffer::create(16); listener = std::make_shared<Listener>();
  delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:buffer listener:listener options:parsed];
  auto response = [[NSHTTPURLResponse alloc] initWithURL:[NSURL URLWithString:@"https://example.com"] statusCode:403 HTTPVersion:@"HTTP/1.1" headerFields:nil];
  __block bool rejected = false;
  [delegate URLSession:nil dataTask:nil didReceiveResponse:response completionHandler:^(NSURLSessionResponseDisposition disposition) {
    rejected = disposition == NSURLSessionResponseCancel;
  }];
  assert(rejected && listener->responses == 1 && listener->completions == 1 && tsb_status(buffer->data()) == TSB_STATUS_ERROR);
  std::cout << "streamFetch iOS capacity/splits, late callbacks, cancel and startup error tests passed\n";
} }
