#pragma once

#include "logger.hpp"

namespace lite {

template <typename Request, typename Key, typename CacheEntry>
void Logger<Request, Key, CacheEntry>::Log(
    const std::shared_ptr<Request> &req) {
  auto *entry = new (typename LoggerInnerInstance::LogEntry)(
      nullptr, req, conn_head_->backend_conn_ptr);
  logger_inner_.Log(entry, conn_head_);
}

template <typename Request, typename Key, typename CacheEntry>
bool Logger<Request, Key, CacheEntry>::Pop(
    LoggerInnerInstance &logger_inner, LoggerInnerInstance::LogEntry *&entry) {
  auto ret = logger_inner.Pop(entry);

  if (ret && entry->state) {
    static_cast<typename CacheInnerInstance::State *>(entry->state)
        ->dirty_node = nullptr;
  }

  return ret;
}

template <typename Request, typename Key, typename CacheEntry>
bool Logger<Request, Key, CacheEntry>::EraseConnectionLogs(
    const size_t number_of_entries) {
  return logger_inner_.EraseConnectionLogs(conn_head_, number_of_entries);
}

template <typename Request, typename Key, typename CacheEntry>
bool Logger<Request, Key, CacheEntry>::Empty() {
  return logger_inner_.Empty();
}

}  // namespace lite