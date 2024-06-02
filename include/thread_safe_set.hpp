#pragma once

#include <functional>
#include <mutex>
#include <set>

namespace lite {

template <typename T>
class ThreadSafeSet {
 public:
  auto erase(const T &value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return set_.erase(value);
  }

  size_t erase_if(const std::function<bool(const T &)> &predicate) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return std::erase_if(set_, predicate);
  }

  auto insert(const T &value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return set_.insert(value);
  }

  void visit_all(const std::function<void(const T &)> &visitor) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto &value : set_) {
      visitor(value);
    }
  }
  size_t size(){
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return set_.size();
  }

  void clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    set_.clear();
  }

 private:
  std::set<T> set_;
  std::shared_mutex mutex_;
};

}  // namespace lite