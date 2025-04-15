#pragma once

#include "logger_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Logger {
  using LoggerInnerInstance = LoggerInner<Application, Request, Response,
                                          ConnectionInfo, CacheKey, CacheEntry>;
  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using CacheInnerInstance = CacheInner<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;

 public:
  Logger(LoggerInnerInstance &logger_inner, LogEntryInstance *const conn_head)
      : logger_inner_(logger_inner), conn_head_(conn_head) {}

  void Log(const std::shared_ptr<Request> &req);

  static bool Pop(LoggerInnerInstance &logger_inner, LogEntryInstance *&entry);

  bool EraseConnectionLogs(const size_t number_of_entries);

  bool Empty();

 private:
  LoggerInnerInstance &logger_inner_;

  LogEntryInstance *const conn_head_;
};

}  // namespace lite