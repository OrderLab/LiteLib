#include "sliding_window.hpp"

namespace lite {

SlidingWindow::SlidingWindow(const std::chrono::milliseconds window_size)
    : window_size_(window_size),
      current_window_index_(0),
      last_window_count_(0),
      current_window_count_(0) {}

void SlidingWindow::SlideWindow(size_t now_index) {
  const auto index_delta = now_index - current_window_index_;
  if (index_delta > 1) {
    last_window_count_ = 0;
    current_window_count_ = 0;
    current_window_index_ = now_index;
  } else if (index_delta == 1) {
    last_window_count_ = current_window_count_;
    current_window_count_ = 0;
    current_window_index_ = now_index;
  }
}

SlidingWindow &SlidingWindow::operator++() {
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
  const auto now_index = now / window_size_;
  std::unique_lock<std::mutex> lock(mutex_);
  const auto index_delta = now_index - current_window_index_;
  SlideWindow(now_index);
  ++current_window_count_;
  return *this;
}

SlidingWindow &SlidingWindow::operator--() {
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
  const auto now_index = now / window_size_;
  std::unique_lock<std::mutex> lock(mutex_);
  SlideWindow(now_index);
  --current_window_count_;
  return *this;
}

SlidingWindow::operator double() {
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
  const auto now_index = now / window_size_;
  std::unique_lock<std::mutex> lock(mutex_);
  SlideWindow(now_index);
  const auto current_window_start = now_index * window_size_;
  const auto ratio = 1 - 1.0 * ((now - current_window_start) / window_size_);
  return last_window_count_ * ratio + current_window_count_;
}

void SlidingWindow::Reset(const size_t count) {
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
  const auto now_index = now / window_size_;
  std::unique_lock<std::mutex> lock(mutex_);
  const auto ratio = 1.0 * ((now - now_index * window_size_) / window_size_);
  current_window_index_ = now_index;
  current_window_count_ = count * ratio;
  last_window_count_ = count;
}

}  // namespace lite