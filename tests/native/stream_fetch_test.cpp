#include "StreamTextFetch.hpp"
#include "StreamTextFetchTasks.hpp"
#include "StreamTextStreams.hpp"
#include "TextStreamBuffer.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace facebook::react;

struct Task : StreamTextFetchTask {
  int cancellations = 0;
  void cancel() override { ++cancellations; }
};

int main() {
  auto &streams = StreamTextStreams::shared();
  std::vector<std::weak_ptr<StreamTextMappedBuffer>> buffers;
  for (int i = 0; i < 100; ++i) {
    auto buffer = StreamTextMappedBuffer::create(16);
    buffers.push_back(buffer);
    streams.publish(std::to_string(i), buffer, "key", 1);
  }
  for (auto &weak : buffers) if (auto buffer = weak.lock()) buffer->finish(TSB_STATUS_DONE);
  size_t alive = 0;
  for (auto &weak : buffers) alive += !weak.expired();
  assert(alive == StreamTextStreams::kKeptFinished);
  streams.clear();

  auto a = StreamTextMappedBuffer::create(16), b = StreamTextMappedBuffer::create(16);
  streams.publish("same", a, "first", 1);
  int publications = 0;
  auto token = streams.listen("same", [&] { ++publications; assert(streams.find("same")); });
  streams.publish("same", b, "second", 2);
  assert(publications == 1 && streams.matches("same", "second") && !streams.matches("same", "first"));
  a->finish(TSB_STATUS_CANCELLED); // An old completion must not remove the replacement.
  streams.releaseOwner(1);
  assert(streams.find("same") == b);
  streams.release("same");
  assert(!streams.find("same") && !streams.matches("same", "second"));
  streams.publish("same", a, "third", 1);
  assert(publications == 2); // Release preserves mounted subscriptions.
  streams.unlisten("same", token);
  streams.clear();

  // Enforce the byte budget too, not merely the number of mappings.
  auto large = StreamTextMappedBuffer::create(1);
  streams.publish("metadata", large, std::string(StreamTextStreams::kKeptBytes, 'k'));
  large->finish(TSB_STATUS_DONE);
  assert(!streams.find("metadata"));
  streams.clear();

  // Eviction remembers the request (by key hash): only an explicit release or reload forgets it.
  auto publishFinished = [&](const std::string &id, uint64_t owner) {
    auto buffer = StreamTextMappedBuffer::create(1);
    streams.publish(id, buffer, "key-" + id, owner);
    buffer->finish(TSB_STATUS_DONE);
  };
  for (size_t i = 0; i <= StreamTextStreams::kKeptFinished + 1; ++i) publishFinished(std::to_string(i), 3);
  assert(!streams.find("0") && !streams.matches("0", "key-0"));
  assert(streams.matches("0", "key-0", true) && !streams.matches("0", "other", true));
  streams.release("0");
  assert(!streams.matches("0", "key-0", true));
  publishFinished("1", 3); // Published again: the live entry decides, not the old eviction.
  assert(streams.matches("1", "key-1", true) && streams.find("1"));
  assert(streams.matches("2", "key-2", true));
  streams.releaseOwner(3);
  assert(!streams.matches("2", "key-2", true));
  streams.clear();
  const size_t total = StreamTextStreams::kKeptFinished + StreamTextStreams::kKeptEvictedKeys + 1;
  for (size_t i = 0; i < total; ++i) publishFinished("t" + std::to_string(i), 4);
  assert(!streams.matches("t0", "key-t0", true)); // The oldest evicted keys are bounded too.
  auto newest = "t" + std::to_string(total - StreamTextStreams::kKeptFinished - 1);
  assert(streams.matches(newest, "key-" + newest, true) && !streams.find(newest));
  streams.clear();

  assert(!StreamTextMappedBuffer::create(0));
  assert(!StreamTextMappedBuffer::create(kStreamTextMaxCapacity + 1));

  StreamTextFetchTasks tasks;
  auto first = std::make_shared<Task>(), second = std::make_shared<Task>();
  auto id1 = tasks.reserve(); tasks.claimStream("id", id1); tasks.set(id1, first);
  auto id2 = tasks.reserve(); assert(tasks.claimStream("id", id2) == first); tasks.set(id2, second);
  tasks.remove(id1);
  assert(tasks.findStream("id") == second);
  tasks.cancelAll();
  assert(second->cancellations == 1 && !tasks.findStream("id"));
  auto finished = tasks.reserve(); tasks.remove(finished); tasks.set(finished, first);
  assert(!tasks.find(finished)); // Synchronous completion during platform startup.

  // Concurrent append/read observes only an immutable, fully initialized prefix.
  auto buffer = StreamTextMappedBuffer::create(100000);
  std::thread writer([&] {
    for (int i = 0; i < 100000; ++i) { char c = char(i % 127); assert(tsb_append(buffer->data(), &c, 1)); }
    buffer->finish(TSB_STATUS_DONE);
  });
  uint64_t read = 0;
  while (true) {
    auto status = tsb_status(buffer->data());
    auto end = tsb_committed(buffer->data());
    assert(end >= read && end <= 100000);
    for (; read < end; ++read) assert(buffer->data()[TSB_HEADER_SIZE + read] == read % 127);
    if (status != TSB_STATUS_OPEN) { assert(read == 100000); break; }
  }
  writer.join();
  std::cout << "streamFetch native registry, lifecycle and publication tests passed\n";
}
