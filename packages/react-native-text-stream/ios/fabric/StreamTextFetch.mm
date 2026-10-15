// streamFetch() on iOS: URLSession appends the body to the TSB1 buffer on its delegate queue.

#import <Foundation/Foundation.h>

#include "../../cpp/StreamTextFetch.hpp"
#include "../TextStreamBuffer.h"

#include <algorithm>

using namespace facebook::react;

/** One request's delegate (URLSession keeps it until the task completes): the buffer's only writer. */
@interface StreamTextFetchDelegate : NSObject <NSURLSessionDataDelegate>
- (instancetype)initWithBuffer:(std::shared_ptr<StreamTextMappedBuffer>)buffer
                      listener:(std::shared_ptr<StreamTextFetchListener>)listener
                       options:(StreamTextParserOptions)options;
- (void)cancelTask:(NSURLSessionTask *)task;
- (void)armDeadlineForTask:(NSURLSessionTask *)task seconds:(double)seconds;
@end

@implementation StreamTextFetchDelegate {
  std::shared_ptr<StreamTextMappedBuffer> _buffer;
  std::shared_ptr<StreamTextFetchListener> _listener;
  std::unique_ptr<StreamTextResponseParser> _parser;
  BOOL _overflowed;
  BOOL _finished;
  BOOL _responded;
  dispatch_source_t _deadline;
}

- (instancetype)initWithBuffer:(std::shared_ptr<StreamTextMappedBuffer>)buffer
                      listener:(std::shared_ptr<StreamTextFetchListener>)listener
                       options:(StreamTextParserOptions)options {
  if (self = [super init]) {
    _buffer = std::move(buffer);
    _listener = std::move(listener);
    if (options.protocol != StreamTextProtocol::Text) {
      // The lifecycle lock protects the parser and this borrowed pointer. finish() destroys
      // the parser before releasing its buffer; the sink never retains the delegate.
      auto *mapped = _buffer.get();
      _parser = std::make_unique<StreamTextResponseParser>(std::move(options), [mapped](std::string_view text) {
        if (text.empty()) return true;
        auto *base = mapped->data();
        auto *header = reinterpret_cast<TSBHeader *>(base);
        const uint64_t room = header->capacity - tsb_committed(base);
        const auto prefix = std::min<uint64_t>(text.size(), room);
        if (prefix) tsb_append(base, text.data(), prefix);
        return text.size() <= room;
      });
    }
  }
  return self;
}

// All methods touching the buffer/listener run under the same lock, including cancellation.
- (void)clearDeadline {
  if (_deadline) {
    dispatch_source_cancel(_deadline);
    _deadline = nil;
  }
}

- (void)dealloc {
  [self clearDeadline];
  _parser.reset();
}

- (void)finish:(uint32_t)status error:(std::string)error {
  if (_finished) return;
  _finished = YES;
  [self clearDeadline];
  _parser.reset();
  auto buffer = std::move(_buffer);
  auto listener = std::move(_listener);
  buffer->finish(status);
  listener->onComplete(std::move(error));
}

- (void)cancelTask:(NSURLSessionTask *)task {
  @synchronized(self) { [self finish:TSB_STATUS_CANCELLED error:std::string()]; }
  [task cancel];
}

- (void)armDeadlineForTask:(NSURLSessionTask *)task seconds:(double)seconds {
  __weak StreamTextFetchDelegate *weakSelf = self;
  __weak NSURLSessionTask *weakTask = task;
  _deadline = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                    dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
  dispatch_source_set_timer(_deadline, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)),
                            DISPATCH_TIME_FOREVER, 0);
  dispatch_source_set_event_handler(_deadline, ^{
    StreamTextFetchDelegate *delegate = weakSelf;
    if (!delegate) return;
    @synchronized(delegate) {
      if (delegate->_finished || delegate->_responded) return;
      [delegate finish:TSB_STATUS_ERROR error:std::string("streamFetch: response timeout")];
    }
    [weakTask cancel];
  });
  dispatch_resume(_deadline);
}

- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
  @synchronized(self) {
    if (_finished) { completionHandler(NSURLSessionResponseCancel); return; }
    _responded = YES;
    [self clearDeadline];
    int status = 0;
    __block StreamTextHeaders headers;
    if ([response isKindOfClass:NSHTTPURLResponse.class]) {
      auto http = (NSHTTPURLResponse *)response;
      status = (int)http.statusCode;
      [http.allHeaderFields enumerateKeysAndObjectsUsingBlock:^(id name, id value, BOOL *) {
        if ([name isKindOfClass:NSString.class] && [value isKindOfClass:NSString.class]) {
          headers.emplace_back(((NSString *)name).lowercaseString.UTF8String, ((NSString *)value).UTF8String);
        }
      }];
    }
    _listener->onResponse(status, std::move(headers), response.URL.absoluteString.UTF8String ?: "");
    if (_parser && (status < 200 || status >= 300)) {
      [self finish:TSB_STATUS_ERROR error:std::string("streamFetch: HTTP ") + std::to_string(status)];
      completionHandler(NSURLSessionResponseCancel);
      return;
    }
    completionHandler(NSURLSessionResponseAllow);
  }
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask didReceiveData:(NSData *)data {
  @synchronized(self) {
    if (_finished) return;
    if (_parser) {
      const uint64_t before = tsb_committed(_buffer->data());
      __block StreamTextParseStatus status = StreamTextParseStatus::Continue;
      [data enumerateByteRangesUsingBlock:^(const void *bytes, NSRange range, BOOL *stop) {
        status = self->_parser->feed(static_cast<const uint8_t *>(bytes), range.length);
        *stop = status != StreamTextParseStatus::Continue;
      }];
      if (status != StreamTextParseStatus::Continue) {
        const auto error = status == StreamTextParseStatus::Error ? _parser->error() : std::string();
        [self finish:static_cast<uint32_t>(status) error:error];
        [dataTask cancel];
      } else if (tsb_committed(_buffer->data()) != before) {
        _buffer->notify();
      }
      return;
    }
    void *base = _buffer->data();
    auto *header = static_cast<TSBHeader *>(base);
    [data enumerateByteRangesUsingBlock:^(const void *bytes, NSRange range, BOOL *stop) {
      uint64_t room = header->capacity - tsb_committed(base);
      tsb_append(base, bytes, std::min<uint64_t>(range.length, room));
      if (range.length > room) {
        self->_overflowed = YES;
        *stop = YES;
      }
    }];
    if (_overflowed) {
      [self finish:TSB_STATUS_OVERFLOW error:std::string()];
      [dataTask cancel];
    } else {
      _buffer->notify();
    }
  }
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
  @synchronized(self) {
    if (_finished) return;
    if (!error && _parser) {
      const auto status = _parser->finish();
      const auto message = status == StreamTextParseStatus::Error ? _parser->error() : std::string();
      [self finish:static_cast<uint32_t>(status) error:message];
      return;
    }
    BOOL cancelled = [error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled;
    uint32_t status = !error ? TSB_STATUS_DONE : cancelled ? TSB_STATUS_CANCELLED : TSB_STATUS_ERROR;
    std::string message = status == TSB_STATUS_ERROR ? (error.localizedDescription.UTF8String ?: "network error") : "";
    [self finish:status error:std::move(message)];
  }
}

@end

namespace {

NSURLSession *streamFetchSession() {
  static NSURLSession *session = [] {
    NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration.defaultSessionConfiguration copy];
    // A stream is read once, as it arrives: nothing to cache.
    configuration.URLCache = nil;
    configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    // A model may think for minutes before its first token.
    configuration.timeoutIntervalForRequest = 300;
    NSOperationQueue *queue = [NSOperationQueue new];
    queue.name = @"StreamTextFetch";
    queue.maxConcurrentOperationCount = 1;
    queue.qualityOfService = NSQualityOfServiceUserInitiated;
    return [NSURLSession sessionWithConfiguration:configuration delegate:nil delegateQueue:queue];
  }();
  return session;
}

class IOSFetchTask : public StreamTextFetchTask {
 public:
  IOSFetchTask(NSURLSessionDataTask *task, StreamTextFetchDelegate *delegate) : task_(task), delegate_(delegate) {}
  void cancel() override {
    [delegate_ cancelTask:task_];
  }

 private:
  NSURLSessionDataTask *task_;
  StreamTextFetchDelegate *delegate_;
};

} // namespace

namespace facebook::react {

std::shared_ptr<StreamTextFetchTask> streamTextStartFetch(
    StreamTextFetchRequest request,
    std::shared_ptr<StreamTextMappedBuffer> buffer,
    std::shared_ptr<StreamTextFetchListener> listener) {
  NSURL *url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url.c_str()]];
  if (url == nil || !([url.scheme.lowercaseString isEqualToString:@"http"] || [url.scheme.lowercaseString isEqualToString:@"https"]) || url.host.length == 0) {
    buffer->finish(TSB_STATUS_ERROR);
    listener->onComplete("invalid URL: " + request.url);
    return nullptr;
  }
  NSMutableURLRequest *urlRequest = [NSMutableURLRequest requestWithURL:url];
  urlRequest.HTTPMethod = [NSString stringWithUTF8String:request.method.c_str()];
  for (const auto &[name, value] : request.headers) {
    [urlRequest addValue:[NSString stringWithUTF8String:value.c_str()]
        forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
  }
  if (request.body) {
    urlRequest.HTTPBody = [NSData dataWithBytes:request.body->data() length:request.body->size()];
  }
  if (request.timeoutSeconds > 0) {
    urlRequest.timeoutInterval = request.timeoutSeconds;
  }
  NSURLSessionDataTask *task = [streamFetchSession() dataTaskWithRequest:urlRequest];
  auto delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:std::move(buffer)
                                                       listener:std::move(listener)
                                                        options:std::move(request.parser)];
  task.delegate = delegate;
  [delegate armDeadlineForTask:task seconds:request.timeoutSeconds];
  [task resume];
  return std::make_shared<IOSFetchTask>(task, delegate);
}

} // namespace facebook::react
