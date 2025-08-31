#pragma once

#include "logger.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Log(const std::shared_ptr<Request> &req) {
  auto *entry =
      new LogEntryInstance(nullptr, req, conn_head_->backend_conn_ptr);
  logger_inner_.Log(entry, conn_head_);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Pop(LoggerInnerInstance &logger_inner,
                             LogEntryInstance *&entry, bool &last_one_in_connection) {
  auto ret = logger_inner.Pop(entry, last_one_in_connection);
  return ret;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::EraseConnectionLogs(const size_t number_of_entries) {
  return logger_inner_.EraseConnectionLogs(conn_head_, number_of_entries);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Logger<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Empty() {
  return logger_inner_.Empty();
}

}  // namespace lite