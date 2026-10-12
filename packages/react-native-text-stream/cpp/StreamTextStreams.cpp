#include "StreamTextStreams.hpp"
#include "../ios/TextStreamBuffer.h"

#include <algorithm>

namespace facebook::react {

StreamTextStreams &StreamTextStreams::shared() {
  static StreamTextStreams streams;
  return streams;
}

void StreamTextStreams::publish(const std::string &id, std::shared_ptr<StreamTextMappedBuffer> buffer) {
  std::vector<std::shared_ptr<Listener>> listeners;
  {
    std::lock_guard lock(mutex_);
    auto &entry = entries_[id];
    entry.buffer = std::move(buffer);
    entry.lastUse = ++clock_;
    for (const auto &[token, listener] : entry.listeners) {
      listeners.push_back(listener);
    }
    evictLocked();
  }
  // Outside the lock: a listener may look the stream up.
  for (const auto &listener : listeners) {
    (*listener)();
  }
}

std::shared_ptr<StreamTextMappedBuffer> StreamTextStreams::find(const std::string &id) {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return nullptr;
  }
  it->second.lastUse = ++clock_;
  return it->second.buffer;
}

uint64_t StreamTextStreams::listen(const std::string &id, Listener listener) {
  std::lock_guard lock(mutex_);
  auto &entry = entries_[id];
  entry.listeners.emplace_back(++lastToken_, std::make_shared<Listener>(std::move(listener)));
  entry.lastUse = ++clock_;
  return lastToken_;
}

void StreamTextStreams::unlisten(const std::string &id, uint64_t token) {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return;
  }
  auto &listeners = it->second.listeners;
  std::erase_if(listeners, [token](const auto &entry) { return entry.first == token; });
  it->second.lastUse = ++clock_;
  // Nothing streamed under it and no one waiting: nothing to keep.
  if (listeners.empty() && !it->second.buffer) {
    entries_.erase(it);
  } else {
    evictLocked();
  }
}

void StreamTextStreams::release(const std::string &id) {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return;
  }
  // Views still listening keep their entry (and what they show); the buffer goes.
  if (it->second.listeners.empty()) {
    entries_.erase(it);
  } else {
    it->second.buffer = nullptr;
  }
}

void StreamTextStreams::clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
}

void StreamTextStreams::evictLocked() {
  // Kept for a remount: finished streams no view shows, the most recently used first. An open
  // stream, or one a view shows, is never forgotten.
  std::vector<std::pair<uint64_t, const std::string *>> finished;
  for (const auto &[id, entry] : entries_) {
    if (entry.listeners.empty() && entry.buffer && tsb_status(entry.buffer->data()) != TSB_STATUS_OPEN) {
      finished.emplace_back(entry.lastUse, &id);
    }
  }
  if (finished.size() <= kKeptFinished) {
    return;
  }
  std::sort(finished.begin(), finished.end());
  std::vector<std::string> evicted;
  for (size_t i = 0; i < finished.size() - kKeptFinished; i++) {
    evicted.push_back(*finished[i].second);
  }
  for (const auto &id : evicted) {
    entries_.erase(id);
  }
}

} // namespace facebook::react
