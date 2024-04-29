#pragma once

#include "logger_inner.hpp"

namespace lite {

template <typename Request, typename Key, typename CacheEntry>
class Logger {
  using LoggerInnerInstance = LoggerInner<Request>;
  using CacheInnerInstance = CacheInner<Key, CacheEntry>;

 public:
  Logger(LoggerInnerInstance &logger_inner, LoggerInnerInstance::LogEntry *const conn_head)
      : logger_inner_(logger_inner), conn_head_(conn_head) {}

  void Log(const std::shared_ptr<Request> &req) {
    auto *entry = new (typename LoggerInnerInstance::LogEntry)(
        nullptr, req, conn_head_->backend_conn_ptr);
    logger_inner_.Log(entry, conn_head_);
  }

  static bool Pop(LoggerInnerInstance &logger_inner,
                  LoggerInnerInstance::LogEntry *&entry) {
    auto ret = logger_inner.Pop(entry);

    if (ret && entry->state) {
      static_cast<typename CacheInnerInstance::State *>(entry->state)->dirty_node =
          nullptr;
    }

    return ret;
  }

  bool EraseConnectionLogs(const size_t number_of_entries) {
    return logger_inner_.EraseConnectionLogs(conn_head_, number_of_entries);
  }

  bool Empty() { return logger_inner_.Empty(); }

 private:
  LoggerInnerInstance &logger_inner_;

  typename LoggerInnerInstance::LogEntry *const conn_head_;
};

}  // namespace lite