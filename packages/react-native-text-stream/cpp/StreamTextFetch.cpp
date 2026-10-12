#include "StreamTextFetch.hpp"
#include "StreamTextBuffers.hpp"
#include "StreamTextStreams.hpp"
#include "StreamTextFetchTasks.hpp"
#include "../ios/TextStreamBuffer.h"

#include <react/bridging/Function.h>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cmath>

namespace facebook::react {


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
  static std::atomic<uint64_t> owners{0};
  owner_ = ++owners;
  methodMap_["put"] = MethodMetadata{1, put};
  methodMap_["fetch"] = MethodMetadata{3, fetch};
  methodMap_["cancel"] = MethodMetadata{1, cancel};
  methodMap_["abortStream"] = MethodMetadata{1, abortStream};
  methodMap_["releaseStream"] = MethodMetadata{1, releaseStream};
  methodMap_["cancelledBuffer"] = MethodMetadata{1, cancelledBuffer};
  methodMap_["readBuffer"] = MethodMetadata{1, readBuffer};
  methodMap_["streamBuffer"] = MethodMetadata{1, streamBuffer};
  methodMap_["hasStream"] = MethodMetadata{3, hasStream};
  methodMap_["watchStream"] = MethodMetadata{2, watchStream};
  methodMap_["unwatchStream"] = MethodMetadata{1, unwatchStream};
}

StreamTextBuffersModule::~StreamTextBuffersModule() {
  // The runtime is going away (a reload): nothing may write for it any more.
  fetches_->cancelAll();
  for (auto &[token, watch] : watches_) {
    watch.active->store(false);
    StreamTextStreams::shared().unlisten(watch.id, token);
  }
  StreamTextStreams::shared().releaseOwner(owner_);
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
  double timeoutMs = numberProperty(rt, init, "timeout", 300000);
  if (!std::isfinite(timeoutMs) || timeoutMs < 1 || timeoutMs > kStreamTextMaxTimeoutMs || std::floor(timeoutMs) != timeoutMs) {
    throw jsi::JSError(rt, "StreamTextBuffers.fetch: timeout must be an integer between 1 and 2147483647 ms");
  }
  request.timeoutSeconds = timeoutMs / 1000;
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
  if (!(capacity >= 1 && capacity <= double(kStreamTextMaxCapacity)) || std::floor(capacity) != capacity) {
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
    StreamTextStreams::shared().publish(streamId, buffer, stringProperty(rt, init, "requestKey", ""), module.owner_);
  }
  auto listener = std::make_shared<JSFetchListener>(
      AsyncCallback<>(rt, args[2].getObject(rt).getFunction(rt), module.jsInvoker_), tasks, id);
  try {
    tasks->set(id, streamTextStartFetch(std::move(request), buffer, listener));
  } catch (const std::exception &error) {
    buffer->finish(TSB_STATUS_ERROR);
    listener->onComplete(error.what());
  }

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

jsi::Value StreamTextBuffersModule::cancelledBuffer(jsi::Runtime &rt, TurboModule &, const jsi::Value *args, size_t count) {
  double capacity = count && args[0].isNumber() ? args[0].getNumber() : 0;
  if (capacity < 1 || capacity > kStreamTextMaxCapacity || !std::isfinite(capacity) || std::floor(capacity) != capacity)
    throw jsi::JSError(rt, "StreamTextBuffers.cancelledBuffer: invalid capacity");
  auto buffer = StreamTextMappedBuffer::create(uint64_t(capacity));
  if (!buffer) throw jsi::JSError(rt, "StreamTextBuffers: could not map the buffer");
  buffer->finish(TSB_STATUS_CANCELLED);
  return jsi::ArrayBuffer(rt, buffer);
}

jsi::Value StreamTextBuffersModule::readBuffer(jsi::Runtime &rt, TurboModule &, const jsi::Value *args, size_t count) {
  if (count < 1 || !args[0].isObject()) return jsi::Value::null();
  auto object = args[0].getObject(rt);
  if (!object.isArrayBuffer(rt)) return jsi::Value::null();
  auto buffer = object.getArrayBuffer(rt).tryGetMutableBuffer(rt);
  // JS-owned buffers cannot be written by native code; JS may read those normally.
  if (!buffer || buffer->size() < TSB_HEADER_SIZE) return jsi::Value::null();
  auto *header = reinterpret_cast<const TSBHeader *>(buffer->data());
  if (header->magic != TSB_MAGIC || header->version != TSB_VERSION || header->headerSize != TSB_HEADER_SIZE)
    return jsi::Value::null();
  // Status first: when terminal, the following acquire reads the final committed prefix.
  uint32_t status = tsb_status(header);
  size_t bytes = std::min<uint64_t>(tsb_committed(header), buffer->size() - TSB_HEADER_SIZE);
  auto mapped = std::dynamic_pointer_cast<StreamTextMappedBuffer>(buffer);
  jsi::Object result(rt);
  result.setProperty(rt, "status", double(status));
  result.setProperty(rt, "bytes", double(bytes));
  result.setProperty(rt, "serial", mapped ? double(mapped->serial()) : 0);
  return result;
}

jsi::Value StreamTextBuffersModule::streamBuffer(jsi::Runtime &rt, TurboModule &, const jsi::Value *args, size_t count) {
  if (!count || !args[0].isString()) return jsi::Value::null();
  auto buffer = StreamTextStreams::shared().find(args[0].getString(rt).utf8(rt));
  return buffer ? jsi::Value(jsi::ArrayBuffer(rt, buffer)) : jsi::Value::null();
}

jsi::Value StreamTextBuffersModule::hasStream(jsi::Runtime &rt, TurboModule &, const jsi::Value *args, size_t count) {
  bool includeEvicted = count >= 3 && args[2].isBool() && args[2].getBool();
  return count >= 2 && args[0].isString() && args[1].isString() &&
      StreamTextStreams::shared().matches(args[0].getString(rt).utf8(rt), args[1].getString(rt).utf8(rt), includeEvicted);
}

jsi::Value StreamTextBuffersModule::watchStream(jsi::Runtime &rt, TurboModule &turboModule, const jsi::Value *args, size_t count) {
  if (count < 2 || !args[0].isString() || !args[1].isObject() || !args[1].getObject(rt).isFunction(rt))
    throw jsi::JSError(rt, "StreamTextBuffers.watchStream: invalid arguments");
  auto &module = static_cast<StreamTextBuffersModule &>(turboModule);
  auto id = args[0].getString(rt).utf8(rt);
  auto active = std::make_shared<std::atomic<bool>>(true);
  auto callback = AsyncCallback<>(rt, args[1].getObject(rt).getFunction(rt), module.jsInvoker_);
  auto token = StreamTextStreams::shared().listen(id, [id, active, callback] {
    auto buffer = StreamTextStreams::shared().find(id);
    if (!buffer) return;
    callback.call([buffer, active](jsi::Runtime &rt, jsi::Function &fn) {
      if (active->load()) fn.call(rt, jsi::ArrayBuffer(rt, buffer));
    });
  });
  module.watches_.emplace(token, Watch{id, active});
  auto buffer = StreamTextStreams::shared().find(id);
  jsi::Object result(rt);
  result.setProperty(rt, "token", double(token));
  result.setProperty(rt, "buffer", buffer ? jsi::Value(jsi::ArrayBuffer(rt, buffer)) : jsi::Value::null());
  return result;
}

jsi::Value StreamTextBuffersModule::unwatchStream(jsi::Runtime &, TurboModule &turboModule, const jsi::Value *args, size_t count) {
  auto &module = static_cast<StreamTextBuffersModule &>(turboModule);
  if (count && args[0].isNumber()) {
    auto it = module.watches_.find(uint64_t(args[0].getNumber()));
    if (it != module.watches_.end()) {
      it->second.active->store(false);
      StreamTextStreams::shared().unlisten(it->second.id, it->first);
      module.watches_.erase(it);
    }
  }
  return jsi::Value::undefined();
}

} // namespace facebook::react
