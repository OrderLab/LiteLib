#pragma once

#include <boost/interprocess/containers/deque.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

namespace lite {

template <typename T>
class ShmThreadSafeQueue {
 public:
  explicit ShmThreadSafeQueue(ShmVoidAllocator allocator)
      : queue_(ShmAllocator<T>(allocator.get_segment_manager())) {}

  ~ShmThreadSafeQueue() {
    while (!queue_.empty()) {
      queue_.pop_front();
    }
    cv_.notify_all();
  }

  void push_back(const T &value) {
    bip::scoped_lock<bip::interprocess_mutex> lock(mutex_);
    queue_.push_back(value);
  }

  T &front() {
    bip::scoped_lock<bip::interprocess_mutex> lock(mutex_);
    return queue_.front();
  }

  [[nodiscard]] T pop_front() {
    bip::scoped_lock<bip::interprocess_mutex> lock(mutex_);
    T value = queue_.front();
    queue_.pop_front();
    if (queue_.empty()) {
      lock.unlock();
      cv_.notify_all();
    }
    return value;
  }

  bool empty() {
    bip::scoped_lock<bip::interprocess_mutex> lock(mutex_);
    return queue_.empty();
  }

  void wait_for_empty() {
    bip::scoped_lock<bip::interprocess_mutex> lock(mutex_);
    if (queue_.empty()) return;
    cv_.wait(lock, [&] { return queue_.empty(); });
  }

  auto size() {
    bip::scoped_lock<bip::interprocess_mutex> lock(mutex_);
    return queue_.size();
  }

  void clear() {
    bip::scoped_lock<bip::interprocess_mutex> lock(mutex_);
    while (!queue_.empty()) {
      queue_.pop_front();
    }
  }

 private:
  ShmDeque<T> queue_;
  bip::interprocess_mutex mutex_;
  bip::interprocess_condition cv_;
};

}  // namespace lite