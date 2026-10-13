#include "StreamTextStreams.hpp"
#include "../ios/TextStreamBuffer.h"

#include <algorithm>

namespace facebook::react {

StreamTextStreams &StreamTextStreams::shared() {
  static StreamTextStreams streams;
  return streams;
}

void StreamTextStreams::publish(const std::string &id, std::shared_ptr<StreamTextMappedBuffer> buffer,
                               std::string requestKey, uint64_t owner, bool retainEvictedRequest) {
  std::vector<std::shared_ptr<Listener>> listeners;
  {
    std::lock_guard lock(mutex_);
    auto &entry = entries_[id];
    entry.buffer = std::move(buffer);
    entry.requestKey = std::move(requestKey);
    entry.owner = owner;
    entry.retainEvictedRequest = retainEvictedRequest;
    entry.lastUse = ++clock_;
    retainedEvicted_.erase(id);
    evicted_.erase(id);
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

bool StreamTextStreams::matches(const std::string &id, const std::string &requestKey, bool includeEvicted) {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(id);
  if (it != entries_.end() && it->second.buffer) {
    return it->second.requestKey == requestKey;
  }
  if (!includeEvicted) {
    return false;
  }
  auto retained = retainedEvicted_.find(id);
  if (retained != retainedEvicted_.end()) {
    return retained->second.keyHash == std::hash<std::string>{}(requestKey);
  }
  auto evicted = evicted_.find(id);
  return evicted != evicted_.end() && evicted->second.keyHash == std::hash<std::string>{}(requestKey);
}

void StreamTextStreams::sweep() {
  std::lock_guard lock(mutex_);
  evictLocked();
}

void StreamTextStreams::releaseOwner(uint64_t owner) {
  std::lock_guard lock(mutex_);
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.owner != owner) { ++it; continue; }
    if (it->second.listeners.empty()) it = entries_.erase(it);
    else {
      it->second.buffer.reset();
      it->second.requestKey.clear();
      ++it;
    }
  }
  std::erase_if(retainedEvicted_, [owner](const auto &entry) { return entry.second.owner == owner; });
  std::erase_if(evicted_, [owner](const auto &entry) { return entry.second.owner == owner; });
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
  retainedEvicted_.erase(id);
  evicted_.erase(id);
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return;
  }
  // Views still listening keep their entry (and what they show); the buffer goes.
  if (it->second.listeners.empty()) {
    entries_.erase(it);
  } else {
    it->second.buffer = nullptr;
    it->second.requestKey.clear();
  }
}

void StreamTextStreams::clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
  retainedEvicted_.clear();
  evicted_.clear();
}

void StreamTextStreams::evictLocked() {
  // Kept for a remount: finished streams no view shows, the most recently used first. An open
  // stream, or one a view shows, is never forgotten.
  std::vector<std::pair<uint64_t, const std::string *>> finished;
  uint64_t bytes = 0;
  for (const auto &[id, entry] : entries_) {
    if (entry.listeners.empty() && entry.buffer && tsb_status(entry.buffer->data()) != TSB_STATUS_OPEN) {
      finished.emplace_back(entry.lastUse, &id);
      bytes += tsb_committed(entry.buffer->data()) + TSB_HEADER_SIZE + entry.requestKey.size();
    }
  }
  if (finished.size() <= kKeptFinished && bytes <= kKeptBytes) {
    return;
  }
  std::sort(finished.begin(), finished.end());
  std::vector<std::string> evicted;
  for (size_t i = 0; i < finished.size() && (finished.size() - i > kKeptFinished || bytes > kKeptBytes); i++) {
    const auto &id = *finished[i].second;
    const auto &entry = entries_.at(id);
    bytes -= tsb_committed(entry.buffer->data()) + TSB_HEADER_SIZE + entry.requestKey.size();
    if (!entry.requestKey.empty()) {
      auto tombstone = Evicted{std::hash<std::string>{}(entry.requestKey), entry.owner, entry.lastUse};
      if (entry.retainEvictedRequest) {
        retainedEvicted_[id] = tombstone;
      } else {
        evicted_[id] = tombstone;
      }
    }
    evicted.push_back(id);
  }
  for (const auto &id : evicted) {
    entries_.erase(id);
  }
  if (evicted_.size() > kKeptEvictedKeys) {
    // Drop the oldest quarter at once, so a full set isn't re-sorted on every eviction.
    std::vector<std::pair<uint64_t, std::string>> byAge;
    byAge.reserve(evicted_.size());
    for (const auto &[id, entry] : evicted_) {
      byAge.emplace_back(entry.lastUse, id);
    }
    auto drop = byAge.begin() + (evicted_.size() - kKeptEvictedKeys * 3 / 4);
    std::nth_element(byAge.begin(), drop, byAge.end());
    for (auto it = byAge.begin(); it != drop; ++it) {
      evicted_.erase(it->second);
    }
  }
}

} // namespace facebook::react
