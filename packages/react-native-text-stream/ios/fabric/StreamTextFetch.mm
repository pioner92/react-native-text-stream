// streamFetch() on iOS: URLSession appends the body to the TSB1 buffer on its delegate queue.

#import <Foundation/Foundation.h>

#include "../../cpp/StreamTextFetch.hpp"
#include "../TextStreamBuffer.h"

#include <algorithm>

using namespace facebook::react;

/** One request's delegate (URLSession keeps it until the task completes): the buffer's only writer. */
@interface StreamTextFetchDelegate : NSObject <NSURLSessionDataDelegate>
- (instancetype)initWithBuffer:(std::shared_ptr<StreamTextMappedBuffer>)buffer
                      listener:(std::shared_ptr<StreamTextFetchListener>)listener;
@end

@implementation StreamTextFetchDelegate {
  std::shared_ptr<StreamTextMappedBuffer> _buffer;
  std::shared_ptr<StreamTextFetchListener> _listener;
  BOOL _overflowed;
}

- (instancetype)initWithBuffer:(std::shared_ptr<StreamTextMappedBuffer>)buffer
                      listener:(std::shared_ptr<StreamTextFetchListener>)listener {
  if (self = [super init]) {
    _buffer = std::move(buffer);
    _listener = std::move(listener);
  }
  return self;
}

- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
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
  completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask didReceiveData:(NSData *)data {
  if (_overflowed) {
    return;
  }
  void *base = _buffer->data();
  auto *header = static_cast<TSBHeader *>(base);
  // A body arrives in pieces that need not be contiguous: append each without joining them.
  [data enumerateByteRangesUsingBlock:^(const void *bytes, NSRange range, BOOL *stop) {
    uint64_t room = header->capacity - __atomic_load_n(&header->committed, __ATOMIC_RELAXED);
    // Fill the buffer to the end, as on Android, then stop: the status says the rest is gone.
    tsb_append(base, bytes, std::min<uint64_t>(range.length, room));
    if (range.length > room) {
      tsb_set_status(base, TSB_STATUS_OVERFLOW);
      self->_overflowed = YES;
      *stop = YES;
    }
  }];
  // Wakes the views showing it (no polling).
  _buffer->notify();
  if (_overflowed) {
    [dataTask cancel];
  }
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
  uint32_t status = TSB_STATUS_DONE;
  if (error) {
    BOOL cancelled = [error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled;
    status = cancelled ? TSB_STATUS_CANCELLED : TSB_STATUS_ERROR;
  }
  tsb_set_status(_buffer->data(), status);
  _buffer->notify();
  std::string message;
  if (error && !_overflowed && status == TSB_STATUS_ERROR) {
    message = error.localizedDescription.UTF8String ?: "network error";
  }
  _listener->onComplete(std::move(message));
  _listener = nullptr;
  _buffer = nullptr;
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
  explicit IOSFetchTask(NSURLSessionDataTask *task) : task_(task) {}
  void cancel() override {
    [task_ cancel];
  }

 private:
  NSURLSessionDataTask *task_;
};

} // namespace

namespace facebook::react {

std::shared_ptr<StreamTextFetchTask> streamTextStartFetch(
    StreamTextFetchRequest request,
    std::shared_ptr<StreamTextMappedBuffer> buffer,
    std::shared_ptr<StreamTextFetchListener> listener) {
  NSURL *url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url.c_str()]];
  if (url == nil || url.scheme == nil) {
    tsb_set_status(buffer->data(), TSB_STATUS_ERROR);
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
  task.delegate = [[StreamTextFetchDelegate alloc] initWithBuffer:std::move(buffer) listener:std::move(listener)];
  [task resume];
  return std::make_shared<IOSFetchTask>(task);
}

} // namespace facebook::react
