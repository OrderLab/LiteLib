#pragma once

#include "logger_inner.hpp"

namespace lite {

template <typename Request, typename Key, typename CacheEntry>
class Logger {
  using LoggerInnerInstance = LoggerInner<Request>;
  using CacheInnerInstance = CacheInner<Key, CacheEntry>;

 public:
  Logger(LoggerInnerInstance &logger_inner,
         LoggerInnerInstance::LogEntry *const conn_head)
      : logger_inner_(logger_inner), conn_head_(conn_head) {}

  void Log(const std::shared_ptr<Request> &req);

  static bool Pop(LoggerInnerInstance &logger_inner,
                  LoggerInnerInstance::LogEntry *&entry);

  bool EraseConnectionLogs(const size_t number_of_entries);

  bool Empty();

 private:
  LoggerInnerInstance &logger_inner_;

  typename LoggerInnerInstance::LogEntry *const conn_head_;
};

}  // namespace lite