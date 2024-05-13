#pragma once

#include "logger_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::LoggerInner()
    : chr_head_(nullptr, nullptr, nullptr),
      chr_tail_(nullptr, nullptr, nullptr) {
  chr_head_.chr_nxt = &chr_tail_;
  chr_tail_.chr_pre = &chr_head_;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::
    Log(LogEntryInstance *entry,
        LogEntryInstance *conn_head) {  // TODO: deal with capacity issues
  std::unique_lock<std::mutex> chr_lock(chr_mutex_);
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
                 CacheEntry>::Pop(LogEntryInstance *&entry) {
  std::unique_lock<std::mutex> chr_lock(chr_mutex_);
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
                 CacheEntry>::EraseConnectionLogs(LogEntryInstance *conn_head,
                                                  const size_t
                                                      number_of_entries) {
  std::unique_lock<std::mutex> chr_lock(chr_mutex_);
  LogEntryInstance *entry = conn_head->conn_nxt, *nxt_entry;
  for (size_t i = 0; i < number_of_entries; ++i, entry = nxt_entry) {
    if (!entry) {
      std::cerr << "Expected to erase " << number_of_entries
                << " entries, but only erased " << i << " entries" << std::endl;
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

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool LoggerInner<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::Empty() {
  std::unique_lock<std::mutex> chr_lock(chr_mutex_);
  return chr_tail_.chr_pre == &chr_head_;
}

}  // namespace lite