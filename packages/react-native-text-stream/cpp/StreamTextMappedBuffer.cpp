#include "StreamTextFetch.hpp"
#include "StreamTextStreams.hpp"
#include "../ios/TextStreamBuffer.h"
#include <sys/mman.h>
#include <atomic>

namespace facebook::react {

std::shared_ptr<StreamTextMappedBuffer> StreamTextMappedBuffer::create(uint64_t capacity) {
  if (capacity < 1 || capacity > kStreamTextMaxCapacity) return nullptr;
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

void StreamTextMappedBuffer::finish(uint32_t status) {
  tsb_set_status(base_, status);
  notify();
  StreamTextStreams::shared().sweep();
}

} // namespace facebook::react
