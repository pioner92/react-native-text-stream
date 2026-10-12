#pragma once
#include "StreamTextFetch.hpp"
#include <unordered_map>

namespace facebook::react {

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
    auto stream = ids_.find(id);
    if (stream != ids_.end()) {
      auto current = streams_.find(stream->second);
      if (current != streams_.end() && current->second == id) streams_.erase(current);
      ids_.erase(stream);
    }
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
    ids_[id] = streamId;
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
      streams_.clear();
      ids_.clear();
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
  std::unordered_map<int, std::string> ids_;
  /** Active requests only; completion removes both indices. */
  std::unordered_map<std::string, int> streams_;
};

} // namespace facebook::react
