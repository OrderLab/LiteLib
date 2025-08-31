#pragma once

#include <mutex>

#include "concept.hpp"
#include "sliding_window.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Connection;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request>
class LogEntry {
  using CacheStateInstance = CacheState<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using ConnectionInstance = Connection<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;

 public:
  CacheStateInstance *state;  // Cache::State *
  std::shared_ptr<Request> req;
  std::shared_ptr<ConnectionInstance *> backend_conn_ptr;
  LogEntry *chr_pre = nullptr,
           *chr_nxt = nullptr;  // global linked list in chronological order
  LogEntry *conn_pre = nullptr,
           *conn_nxt = nullptr;  // linked list per connection

  LogEntry(CacheStateInstance *state, std::shared_ptr<Request> req,
           std::shared_ptr<ConnectionInstance *> backend_conn_ptr)
      : state(state), req(req), backend_conn_ptr(backend_conn_ptr) {}

  void Delink() {
    if (chr_pre) chr_pre->chr_nxt = chr_nxt;
    if (chr_nxt) chr_nxt->chr_pre = chr_pre;
    if (conn_pre) conn_pre->conn_nxt = conn_nxt;
    if (conn_nxt) conn_nxt->conn_pre = conn_pre;
  }
};

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class LoggerInner {
  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;

 public:
  LoggerInner(const std::chrono::milliseconds sliding_window_size);

  void Log(LogEntryInstance *entry, LogEntryInstance *conn_head);

  bool Pop(LogEntryInstance *&entry, bool &last_one_in_connection);

  bool EraseConnectionLogs(LogEntryInstance *conn_head,
                           const size_t number_of_entries);

  bool Empty();

  std::mutex chr_mutex_;

  SlidingWindow inserting_rate_;

 private:
  LogEntryInstance chr_head_, chr_tail_;
};

}  // namespace lite