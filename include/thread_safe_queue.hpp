#pragma once

#include <functional>
#include <mutex>
#include <queue>

namespace lite {

template <typename T>
class ThreadSafeQueue {
 public:
  void push_back(const T &value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    queue_.push(value);
  }

  T &front() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return queue_.front();
  }

  void pop_front() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    queue_.pop();
  }

  bool empty() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return queue_.empty();
  }

 private:
  std::queue<T> queue_;
  std::shared_mutex mutex_;
};

}  // namespace lite