// https://blog.cloudflare.com/counting-things-a-lot-of-different-things/
#pragma once
#include <chrono>
#include <mutex>

namespace lite {

class SlidingWindow {
 public:
  explicit SlidingWindow(const std::chrono::milliseconds window_size);

  SlidingWindow &operator++();

  SlidingWindow &operator--();

  operator double();

  void Reset(const size_t count = 0);

 private:
  void SlideWindow(size_t now_index);

  mutable std::mutex mutex_;
  std::chrono::milliseconds window_size_;
  size_t current_window_index_;
  ssize_t last_window_count_, current_window_count_;
};

}  // namespace lite