#pragma once

#include <mutex>

#include "concept.hpp"

namespace lite {

template <typename Request>
  requires IsProtocolMessage<Request>
class LoggerInner {
 public:
  LoggerInner()
      : chr_head_(nullptr, nullptr, nullptr),
        chr_tail_(nullptr, nullptr, nullptr) {
    chr_head_.chr_nxt = &chr_tail_;
    chr_tail_.chr_pre = &chr_head_;
  }

  class LogEntry {
   public:
    void *state;  // Cache::State *
    std::shared_ptr<Request> req;
    std::shared_ptr<void *> backend_conn_ptr;
    LogEntry *chr_pre = nullptr,
             *chr_nxt = nullptr;  // global linked list in chronological order
    LogEntry *conn_pre = nullptr,
             *conn_nxt = nullptr;  // linked list per connection

    LogEntry(void *state, std::shared_ptr<Request> req,
             std::shared_ptr<void *> backend_conn_ptr)
        : state(state), req(req), backend_conn_ptr(backend_conn_ptr) {}

    void Delink() {
      if (chr_pre) chr_pre->chr_nxt = chr_nxt;
      if (chr_nxt) chr_nxt->chr_pre = chr_pre;
      // TODO: lock for connection?
      if (conn_pre) conn_pre->conn_nxt = conn_nxt;
      if (conn_nxt) conn_nxt->conn_pre = conn_pre;
    }
  };

  void Log(LogEntry *entry,
           LogEntry *conn_head) {  // TODO: deal with capacity issues
    std::unique_lock<std::mutex> chr_lock(chr_mutex_);
    entry->chr_pre = &chr_head_;
    entry->chr_nxt = chr_head_.chr_nxt;
    entry->chr_nxt->chr_pre = entry;
    chr_head_.chr_nxt = entry;
    chr_lock.unlock();
    if (conn_head->conn_nxt) conn_head->conn_nxt->conn_pre = entry;
    entry->conn_nxt = conn_head->conn_nxt;
    entry->conn_pre = conn_head;
    conn_head->conn_nxt = entry;
  }

  bool Pop(LogEntry *&entry) {
    std::unique_lock<std::mutex> chr_lock(chr_mutex_);
    if (chr_tail_.chr_pre == &chr_head_) return false;
    entry = chr_tail_.chr_pre;
    entry->Delink();
    return true;
  }

  bool EraseConnectionLogs(LogEntry *conn_head,
                           const size_t number_of_entries) {
    std::unique_lock<std::mutex> chr_lock(chr_mutex_);
    LogEntry *entry = conn_head->conn_nxt, *nxt_entry;
    for (size_t i = 0; i < number_of_entries; ++i, entry = nxt_entry) {
      if (!entry) {
        std::cerr << "Expected to erase " << number_of_entries
                  << " entries, but only erased " << i << " entries"
                  << std::endl;
        return false;
      }
      if (entry->state) {
        std::cerr << "Expected to erase " << number_of_entries
                  << " requests in Log, but found a dirty state in " << i
                  << "th entry" << std::endl;
        return false;
      }
      nxt_entry = entry->conn_nxt;
      entry->Delink();
      delete entry;
    }
    chr_lock.unlock();
    return true;
  }

  bool Empty() { return chr_tail_.chr_pre == &chr_head_; }

  std::mutex chr_mutex_;

 private:
  LogEntry chr_head_, chr_tail_;
};

}  // namespace lite