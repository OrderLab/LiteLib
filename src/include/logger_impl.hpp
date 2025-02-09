#pragma once

#include "logger.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Log(const std::shared_ptr<Request> &req) {
  LogEntryInstance *entry =
      logger_inner_ptr_->segment_mgr_->template construct<LogEntryInstance>(
          bip::anonymous_instance)(nullptr, req, conn_head_->backend_conn_ptr);
  logger_inner_ptr_->Log(entry, conn_head_.get());
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Pop(LoggerInnerInstance &logger_inner,
                             LogEntryInstance *&entry) {
  auto ret = logger_inner.Pop(entry);
  return ret;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::EraseConnectionLogs(const size_t number_of_entries) {
  return logger_inner_ptr_->EraseConnectionLogs(conn_head_.get(),
                                                number_of_entries);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Empty() {
  return logger_inner_ptr_->Empty();
}

}  // namespace lite