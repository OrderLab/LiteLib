#pragma once

#include <barrier>

#include "cache.hpp"
#include "concept.hpp"
#include "daemon.hpp"
#include "logger.hpp"
#include "thread_safe_set.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Connection;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Worker;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
class LiteCore : public Daemon {
  using LoggerInstance = Logger<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>;
  using LoggerInnerInstance = LoggerInner<Application, Request, Response,
                                          ConnectionInfo, CacheKey, CacheEntry>;
  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using CacheInstance = Cache<Application, Request, Response, ConnectionInfo,
                              CacheKey, CacheEntry>;
  using CacheInnerInstance = CacheInner<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using ConnectionInstance = Connection<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using WorkerInstance = Worker<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>;
  using CacheStateInstance = CacheState<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
 public:
  LiteCore(Application &app, const size_t &max_item_count,
           std::string &backend_addr, std::string &backend_port,
           const char pipe_path[], std::barrier<std::function<void()>> &barrier,
           std::vector<std::unique_ptr<WorkerInstance>> &workers, bool crash_recover = true);

  bool HandleRequest(std::shared_ptr<Request> req, ConnectionInfo &conn_info,
                     ThreadSafeQueue<std::pair<std::shared_ptr<Request>, bool>>
                         &pending_requests,
                     const evutil_socket_t client_fd,
                     const evutil_socket_t backend_fd, CacheInstance *cache,
                     LoggerInstance *logger);

  bool HandleResponse(std::shared_ptr<Response> resp, ConnectionInfo &conn_info,
                      ThreadSafeQueue<std::pair<std::shared_ptr<Request>, bool>>
                          &pending_requests,
                      const evutil_socket_t client_fd, CacheInstance *cache);

  std::string &backend_addr_, &backend_port_;

  bool is_replaying_ = false;

  ThreadSafeSet<ConnectionInstance *> live_connections_;

  CacheInnerInstance cache_inner_;

  LoggerInnerInstance logger_inner_;

  ThreadSafeQueue<LogEntryInstance *> dead_connection_log_heads_;

  LogEntryInstance * crash_conn_head_ = nullptr;

 private:
  bool crash_recover_;

  Application &app_;

  std::barrier<std::function<void()>> &barrier_;

  std::vector<std::unique_ptr<WorkerInstance>> &workers_;

  bool Replay();

  bool Crash();
};

}  // namespace lite
