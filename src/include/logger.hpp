#pragma once

#include <mutex>

#include "concept.hpp"

namespace lite {

template <typename Data>
  requires IsLogEntry<Data>
class Logger {
 public:
  Logger() {
    chr_head_.chr_nxt = &chr_tail_;
    chr_tail_.chr_pre = &chr_head_;
  }

  struct LogEntry {
    Data data;
    std::shared_ptr<evutil_socket_t> backend_fd;
    LogEntry *chr_pre = nullptr,
             *chr_nxt = nullptr;  // global linked list in chronological order
    LogEntry *conn_pre = nullptr,
             *conn_nxt = nullptr;  // linked list per connection
  };

  void Log(const Data &data, LogEntry &conn_head) {
    LogEntry *entry = new LogEntry{
        data, conn_head.backend_fd, nullptr, nullptr, nullptr, nullptr};
    std::unique_lock<std::mutex> chr_lock(chr_mutex_);
    entry->chr_pre = &chr_head_;
    entry->chr_nxt = chr_head_.chr_nxt;
    chr_lock.unlock();
    if (conn_head.conn_nxt) conn_head.conn_nxt->conn_pre = entry;
    entry->conn_nxt = conn_head.conn_nxt;
    entry->conn_pre = &conn_head;
    conn_head.conn_nxt = entry;
  }  // TODO: deal with capacity issues

  bool Pop(LogEntry *entry) {
    std::unique_lock<std::mutex> chr_lock(chr_mutex_);
    if (chr_tail_.chr_pre == &chr_head_) return false;
    entry = chr_tail_.chr_pre;
    entry->chr_pre->chr_nxt = entry->chr_nxt;
    entry->chr_nxt->chr_pre = entry->chr_pre;
    // TODO: lock for connection?
    entry->conn_pre->conn_nxt = entry->conn_nxt;
    if (entry->conn_nxt) entry->conn_nxt->conn_pre = entry->conn_pre;
    return true;
  }

  bool EraseConnectionLogs(LogEntry &conn_head,
                           const size_t number_of_entries) {
    std::unique_lock<std::mutex> chr_lock(chr_mutex_);
    LogEntry *entry = conn_head.conn_nxt, *nxt_entry;
    for (size_t i = 0; i < number_of_entries; ++i, entry = nxt_entry) {
      if (!entry) {
        std::cerr << "Expected to erase " << number_of_entries
                  << " entries, but only erased " << i << " entries"
                  << std::endl;
        return false;
      }
      nxt_entry = entry->conn_nxt;
      entry->conn_pre->conn_nxt = entry->conn_nxt;
      if (entry->conn_nxt) entry->conn_nxt->conn_pre = entry->conn_pre;
      entry->chr_pre->chr_nxt = entry->chr_nxt;
      entry->chr_nxt->chr_pre = entry->chr_pre;
      delete entry;
    }
    chr_lock.unlock();
    return true;
  }

  bool Empty() { return chr_tail_.chr_pre == &chr_head_; }

 private:
  std::mutex chr_mutex_;
  LogEntry chr_head_, chr_tail_;
};

}  // namespace lite