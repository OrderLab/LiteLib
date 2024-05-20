#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace lite {

template <typename T>
class ThreadSafeQueue {
 public:
  ~ThreadSafeQueue() {
    cv_.notify_one();
  }

  void push_back(const T &value) {
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.push(value);
  }

  T &front() {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.front();
  }

  [[nodiscard]] T pop_front() {
    std::unique_lock<std::mutex> lock(mutex_);
    T value = queue_.front();
    queue_.pop();
    if (queue_.empty()) {
      lock.unlock();
      cv_.notify_one();
    }
    return value;
  }

  bool empty() {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.empty();
  }

  void wait_for_empty() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) return;
    cv_.wait(lock, [&] { return queue_.empty(); });
  }

  auto size() {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
  }

  void clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
      queue_.pop();
    }
  }

 private:
  std::queue<T> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace lite