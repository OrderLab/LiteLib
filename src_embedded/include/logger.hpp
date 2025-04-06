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
  Logger(bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr,
         bip::offset_ptr<LogEntryInstance> conn_head)
      : logger_inner_ptr_(logger_inner_ptr), conn_head_(conn_head) {}

  void Log(const ShmSharedPtr<Request> &req);

  static bool Pop(LoggerInnerInstance &logger_inner, LogEntryInstance *&entry);

  bool EraseConnectionLogs(const size_t number_of_entries);

  bool Empty();

 private:
  bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr_;

  bip::offset_ptr<LogEntryInstance> conn_head_;
};

}  // namespace lite