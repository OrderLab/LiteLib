#pragma once

#include <mutex>

#include "concept.hpp"

namespace lite {

template <typename Request>
  requires IsProtocolMessage<Request>
class LoggerInner {
 public:
  LoggerInner();

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

  void Log(LogEntry *entry, LogEntry *conn_head);

  bool Pop(LogEntry *&entry);

  bool EraseConnectionLogs(LogEntry *conn_head, const size_t number_of_entries);

  bool Empty();

  std::mutex chr_mutex_;

 private:
  LogEntry chr_head_, chr_tail_;
};

}  // namespace lite