#pragma once

#include <boost/lockfree/spsc_queue.hpp>
#include <mutex>

#include "concept.hpp"

namespace lite {

template <typename Entry>
  requires IsLogEntry<Entry>
class Logger {
 public:
  Logger() = default;

  void Log(const Entry &entry) {
    q_.push(entry);
  }  // TODO: deal with capacity issues

  bool Pop(Entry &entry) { return q_.pop(entry); }

  bool Empty() { return q_.empty(); }

 private:
  boost::lockfree::spsc_queue<Entry, boost::lockfree::capacity<1024> > q_;
};

}  // namespace lite