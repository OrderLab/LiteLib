#pragma once

#include <boost/interprocess/sync/interprocess_mutex.hpp>

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
  // TODO: init logger only after mode transition so that we can use raw pointer
  bip::offset_ptr<CacheStateInstance> state;
  ShmSharedPtr<Request> req;
  ShmSharedPtr<bip::offset_ptr<ConnectionInstance>> backend_conn_ptr;
  bip::offset_ptr<LogEntry>
      chr_pre = nullptr,
      chr_nxt = nullptr;  // global linked list in chronological order
  bip::offset_ptr<LogEntry> conn_pre = nullptr,
                            conn_nxt = nullptr;  // linked list per connection

  LogEntry(bip::offset_ptr<CacheStateInstance> state, ShmSharedPtr<Request> req,
           ShmVoidAllocator allocator)
      : state(state), req(req) {
    backend_conn_ptr = ShmMakeShared(
        allocator.get_segment_manager()
            ->template construct<bip::offset_ptr<ConnectionInstance>>(
                bip::anonymous_instance)(nullptr),
        *allocator.get_segment_manager());
  }

  LogEntry(bip::offset_ptr<CacheStateInstance> state, ShmSharedPtr<Request> req,
           ShmSharedPtr<bip::offset_ptr<ConnectionInstance>> backend_conn_ptr)
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
  LoggerInner(const std::chrono::milliseconds sliding_window_size,
              ShmAllocator<LogEntryInstance> allocator);

  void Init();

  void Log(bip::offset_ptr<LogEntryInstance> entry,
           bip::offset_ptr<LogEntryInstance> conn_head);

  bool Pop(bip::offset_ptr<LogEntryInstance> &entry);

  bool EraseConnectionLogs(bip::offset_ptr<LogEntryInstance> conn_head,
                           const size_t number_of_entries);

  bool Empty();

  ShmAllocator<LogEntryInstance> log_entry_allocator_;

  bip::interprocess_mutex chr_mutex_;

  SlidingWindow inserting_rate_;

 private:
  LogEntryInstance chr_head_, chr_tail_;
};

}  // namespace lite