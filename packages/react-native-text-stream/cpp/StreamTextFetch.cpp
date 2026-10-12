#include "StreamTextFetch.hpp"
#include "StreamTextBuffers.hpp"
#include "StreamTextStreams.hpp"
#include "../ios/TextStreamBuffer.h"

#include <react/bridging/Function.h>
#include <sys/mman.h>
#include <atomic>
#include <cmath>

namespace facebook::react {

std::shared_ptr<StreamTextMappedBuffer> StreamTextMappedBuffer::create(uint64_t capacity) {
  size_t size = TSB_HEADER_SIZE + capacity;
  void *base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (base == MAP_FAILED) {
    return nullptr;
  }
  tsb_init(base, capacity);
  return std::make_shared<StreamTextMappedBuffer>(static_cast<uint8_t *>(base), size);
}

StreamTextMappedBuffer::StreamTextMappedBuffer(uint8_t *base, size_t size) : base_(base), size_(size) {
  static std::atomic<uint64_t> lastSerial{0};
  serial_ = ++lastSerial;
}

StreamTextMappedBuffer::~StreamTextMappedBuffer() {
  munmap(base_, size_);
}

uint64_t StreamTextMappedBuffer::observe(Observer observer) {
  std::lock_guard lock(mutex_);
  observers_.emplace_back(++lastToken_, std::make_shared<Observer>(std::move(observer)));
  return lastToken_;
}

void StreamTextMappedBuffer::unobserve(uint64_t token) {
  std::lock_guard lock(mutex_);
  std::erase_if(observers_, [token](const auto &entry) { return entry.first == token; });
}

void StreamTextMappedBuffer::notify() {
  // Called outside the lock: an observer may (un)observe from its callback.
  std::vector<std::shared_ptr<Observer>> observers;
  {
    std::lock_guard lock(mutex_);
    observers.reserve(observers_.size());
    for (const auto &[token, observer] : observers_) {
      observers.push_back(observer);
    }
  }
  for (const auto &observer : observers) {
    (*observer)();
  }
}

/** Requests in flight, by id: `cancel(id)` finds them, and a module going away cancels them. */
class StreamTextFetchTasks {
 public:
  /** An id for a request about to start (no task yet). */
  int reserve() {
    std::lock_guard lock(mutex_);
    int id = ++lastId_;
    tasks_[id] = nullptr;
    return id;
  }
  /** Unless the request already completed (and left). */
  void set(int id, std::shared_ptr<StreamTextFetchTask> task) {
    std::lock_guard lock(mutex_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
      it->second = std::move(task);
    }
  }
  void remove(int id) {
    std::lock_guard lock(mutex_);
    tasks_.erase(id);
  }
  /** The request now streaming under `streamId`; the one before it, if still running, is returned. */
  std::shared_ptr<StreamTextFetchTask> claimStream(const std::string &streamId, int id) {
    std::lock_guard lock(mutex_);
    auto previous = streams_.find(streamId);
    std::shared_ptr<StreamTextFetchTask> replaced;
    if (previous != streams_.end()) {
      auto it = tasks_.find(previous->second);
      if (it != tasks_.end()) {
        replaced = it->second;
      }
    }
    streams_[streamId] = id;
    return replaced;
  }
  std::shared_ptr<StreamTextFetchTask> findStream(const std::string &streamId) {
    std::lock_guard lock(mutex_);
    auto previous = streams_.find(streamId);
    if (previous == streams_.end()) {
      return nullptr;
    }
    auto it = tasks_.find(previous->second);
    return it == tasks_.end() ? nullptr : it->second;
  }
  std::shared_ptr<StreamTextFetchTask> find(int id) {
    std::lock_guard lock(mutex_);
    auto it = tasks_.find(id);
    return it == tasks_.end() ? nullptr : it->second;
  }
  void cancelAll() {
    std::unordered_map<int, std::shared_ptr<StreamTextFetchTask>> tasks;
    {
      std::lock_guard lock(mutex_);
      tasks.swap(tasks_);
    }
    for (auto &[id, task] : tasks) {
      if (task) {
        task->cancel();
      }
    }
  }

 private:
  std::mutex mutex_;
  int lastId_{0};
  std::unordered_map<int, std::shared_ptr<StreamTextFetchTask>> tasks_;
  /** Which request streams under each streamId (it may have ended). */
  std::unordered_map<std::string, int> streams_;
};

namespace {

/** Hands the request's progress to JS: `onEvent('response', status, headers, url)`, then `onEvent('complete', error)`. */
class JSFetchListener : public StreamTextFetchListener {
 public:
  JSFetchListener(AsyncCallback<> onEvent, std::weak_ptr<StreamTextFetchTasks> tasks, int id)
      : onEvent_(std::move(onEvent)), tasks_(std::move(tasks)), id_(id) {}

  void onResponse(int status, StreamTextHeaders headers, std::string url) override {
    onEvent_.call([status, headers = std::move(headers), url = std::move(url)](jsi::Runtime &rt, jsi::Function &fn) {
      jsi::Object object(rt);
      for (const auto &[name, value] : headers) {
        object.setProperty(rt, jsi::PropNameID::forUtf8(rt, name), jsi::String::createFromUtf8(rt, value));
      }
      fn.call(rt, jsi::String::createFromAscii(rt, "response"), status, object, jsi::String::createFromUtf8(rt, url));
    });
  }

  void onComplete(std::string error) override {
    if (auto tasks = tasks_.lock()) {
      tasks->remove(id_);
    }
    onEvent_.call([error = std::move(error)](jsi::Runtime &rt, jsi::Function &fn) {
      fn.call(
          rt,
          jsi::String::createFromAscii(rt, "complete"),
          error.empty() ? jsi::Value::null() : jsi::Value(jsi::String::createFromUtf8(rt, error)));
    });
  }

 private:
  AsyncCallback<> onEvent_;
  std::weak_ptr<StreamTextFetchTasks> tasks_;
  int id_;
};

std::string stringProperty(jsi::Runtime &rt, const jsi::Object &object, const char *name, const char *fallback) {
  auto value = object.getProperty(rt, name);
  return value.isString() ? value.getString(rt).utf8(rt) : fallback;
}

double numberProperty(jsi::Runtime &rt, const jsi::Object &object, const char *name, double fallback) {
  auto value = object.getProperty(rt, name);
  return value.isNumber() ? value.getNumber() : fallback;
}

} // namespace

StreamTextBuffersModule::StreamTextBuffersModule(std::shared_ptr<CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)), fetches_(std::make_shared<StreamTextFetchTasks>()) {
  methodMap_["put"] = MethodMetadata{1, put};
  methodMap_["fetch"] = MethodMetadata{3, fetch};
  methodMap_["cancel"] = MethodMetadata{1, cancel};
  methodMap_["abortStream"] = MethodMetadata{1, abortStream};
  methodMap_["releaseStream"] = MethodMetadata{1, releaseStream};
}

StreamTextBuffersModule::~StreamTextBuffersModule() {
  // The runtime is going away (a reload): nothing may write for it any more.
  fetches_->cancelAll();
  StreamTextStreams::shared().clear();
}

jsi::Value StreamTextBuffersModule::fetch(
    jsi::Runtime &rt,
    TurboModule &turboModule,
    const jsi::Value *args,
    size_t count) {
  auto &module = static_cast<StreamTextBuffersModule &>(turboModule);
  if (count < 3 || !args[0].isString() || !args[1].isObject() || !args[2].isObject() ||
      !args[2].getObject(rt).isFunction(rt)) {
    throw jsi::JSError(rt, "StreamTextBuffers.fetch(url, init, onEvent): invalid arguments");
  }
  auto init = args[1].getObject(rt);
  StreamTextFetchRequest request;
  request.url = args[0].getString(rt).utf8(rt);
  request.method = stringProperty(rt, init, "method", "GET");
  request.timeoutSeconds = numberProperty(rt, init, "timeout", 0) / 1000;
  auto body = init.getProperty(rt, "body");
  if (body.isString()) {
    request.body = body.getString(rt).utf8(rt);
  }
  // Flat [name, value, name, value, …]: the JS side normalizes any HeadersInit to it.
  auto headers = init.getProperty(rt, "headers");
  if (headers.isObject() && headers.getObject(rt).isArray(rt)) {
    auto array = headers.getObject(rt).getArray(rt);
    size_t length = array.size(rt);
    for (size_t i = 0; i + 1 < length; i += 2) {
      auto name = array.getValueAtIndex(rt, i);
      auto value = array.getValueAtIndex(rt, i + 1);
      if (name.isString() && value.isString()) {
        request.headers.emplace_back(name.getString(rt).utf8(rt), value.getString(rt).utf8(rt));
      }
    }
  }
  double capacity = numberProperty(rt, init, "capacity", 0);
  if (!(capacity >= 1 && capacity <= double(1ull << 40)) || std::floor(capacity) != capacity) {
    throw jsi::JSError(rt, "StreamTextBuffers.fetch: capacity must be a whole number of bytes");
  }
  auto buffer = StreamTextMappedBuffer::create(uint64_t(capacity));
  if (!buffer) {
    throw jsi::JSError(rt, "StreamTextBuffers.fetch: could not map the buffer");
  }

  std::string streamId = stringProperty(rt, init, "streamId", "");
  auto &tasks = module.fetches_;
  int id = tasks->reserve();
  if (!streamId.empty()) {
    // A new request under the same id replaces the one before it (e.g. regenerating an answer).
    if (auto replaced = tasks->claimStream(streamId, id)) {
      replaced->cancel();
    }
    // Views showing this id switch to the new buffer.
    StreamTextStreams::shared().publish(streamId, buffer);
  }
  auto listener = std::make_shared<JSFetchListener>(
      AsyncCallback<>(rt, args[2].getObject(rt).getFunction(rt), module.jsInvoker_), tasks, id);
  tasks->set(id, streamTextStartFetch(std::move(request), buffer, std::move(listener)));

  jsi::Object result(rt);
  result.setProperty(rt, "id", id);
  result.setProperty(rt, "buffer", jsi::ArrayBuffer(rt, buffer));
  return result;
}

jsi::Value StreamTextBuffersModule::abortStream(
    jsi::Runtime &rt,
    TurboModule &turboModule,
    const jsi::Value *args,
    size_t count) {
  auto &module = static_cast<StreamTextBuffersModule &>(turboModule);
  if (count >= 1 && args[0].isString()) {
    if (auto task = module.fetches_->findStream(args[0].getString(rt).utf8(rt))) {
      task->cancel();
    }
  }
  return jsi::Value::undefined();
}

jsi::Value StreamTextBuffersModule::releaseStream(
    jsi::Runtime &rt,
    TurboModule & /*module*/,
    const jsi::Value *args,
    size_t count) {
  if (count >= 1 && args[0].isString()) {
    StreamTextStreams::shared().release(args[0].getString(rt).utf8(rt));
  }
  return jsi::Value::undefined();
}

jsi::Value StreamTextBuffersModule::cancel(
    jsi::Runtime &rt,
    TurboModule &turboModule,
    const jsi::Value *args,
    size_t count) {
  auto &module = static_cast<StreamTextBuffersModule &>(turboModule);
  if (count >= 1 && args[0].isNumber()) {
    if (auto task = module.fetches_->find(int(args[0].getNumber()))) {
      task->cancel();
    }
  }
  return jsi::Value::undefined();
}

} // namespace facebook::react
