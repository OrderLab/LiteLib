#pragma once

#include "logger_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::LoggerInner(const std::chrono::milliseconds
                                         sliding_window_size,
                                     ShmAllocator<LogEntryInstance> allocator)
    : chr_head_(nullptr, ShmSharedPtr<Request>{}, allocator),
      chr_tail_(nullptr, ShmSharedPtr<Request>{}, allocator),
      inserting_rate_(sliding_window_size),
      log_entry_allocator_(allocator) {
  Init();
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::Init() {
  chr_head_.chr_nxt = &chr_tail_;
  chr_tail_.chr_pre = &chr_head_;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::Log(bip::offset_ptr<LogEntryInstance> entry,
                                  bip::offset_ptr<LogEntryInstance>
                                      conn_head) {  // TODO: deal with capacity
                                                    // issues
  bip::scoped_lock<bip::interprocess_mutex> chr_lock(chr_mutex_);
  ++inserting_rate_;
  entry->chr_pre = &chr_head_;
  entry->chr_nxt = chr_head_.chr_nxt;
  entry->chr_nxt->chr_pre = entry;
  chr_head_.chr_nxt = entry;
  if (conn_head->conn_nxt) conn_head->conn_nxt->conn_pre = entry;
  entry->conn_nxt = conn_head->conn_nxt;
  entry->conn_pre = conn_head;
  conn_head->conn_nxt = entry;
  chr_lock.unlock();
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::Pop(bip::offset_ptr<LogEntryInstance> &entry) {
  bip::scoped_lock<bip::interprocess_mutex> chr_lock(chr_mutex_);
  if (chr_tail_.chr_pre == &chr_head_) return false;
  entry = chr_tail_.chr_pre;
  entry->Delink();
  if (entry->state) {
    entry->state->dirty_node = nullptr;
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::
    EraseConnectionLogs(bip::offset_ptr<LogEntryInstance> conn_head,
                        const size_t number_of_entries) {
  bip::scoped_lock<bip::interprocess_mutex> chr_lock(chr_mutex_);
  bip::offset_ptr<LogEntryInstance> entry = conn_head->conn_nxt, nxt_entry;
  for (size_t i = 0; i < number_of_entries; ++i, entry = nxt_entry) {
    if (!entry) {
      LOG(ERROR) << "Expected to erase " << number_of_entries
                 << " entries, but only erased " << i << " entries"
                 << std::endl;
      return false;
    }
    if (entry->state) {
      LOG(ERROR) << "Expected to erase " << number_of_entries
                 << " requests in Log, but found a dirty state in " << i
                 << "th entry" << std::endl;
      return false;
    }
    nxt_entry = entry->conn_nxt;
    entry->Delink();
    --inserting_rate_;
    entry->~LogEntryInstance();
    log_entry_allocator_.deallocate_one(entry);
  }
  chr_lock.unlock();
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::Empty() {
  bip::scoped_lock<bip::interprocess_mutex> chr_lock(chr_mutex_);
  return chr_tail_.chr_pre == &chr_head_;
}

}  // namespace lite