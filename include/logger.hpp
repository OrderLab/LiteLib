#pragma once

#include <boost/lockfree/spsc_queue.hpp>
#include <mutex>

namespace lite {

template <typename Entry>
class Logger {
 public:
  Logger() = default;

  void Log(const Entry &entry, bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    q_.push(entry);
  }  // TODO: deal with capacity issues

  bool Pop(Entry &entry, bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    return q_.pop(entry);
  }

  std::unique_lock<std::shared_mutex> TransactionLock() {
    return std::unique_lock<std::shared_mutex>{transaction_mutex_};
  }

 private:
  boost::lockfree::spsc_queue<Entry, boost::lockfree::capacity<1024> > q_;
  std::shared_mutex transaction_mutex_;
};

}  // namespace lite