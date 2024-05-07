#pragma once

#include <chrono>

#include "cache.hpp"
#include "concept.hpp"
#include "daemon.hpp"
#include "logger.hpp"
#include "network_utils.hpp"
#include "thread_safe_set.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<CacheKey, CacheEntry>
class LiteCore : public Daemon {
  using LoggerInstance = Logger<Request, CacheKey, CacheEntry>;
  using LoggerInnerInstance = LoggerInner<Request>;
  using CacheInstance = Cache<CacheKey, CacheEntry, Request>;
  using CacheInnerInstance = CacheInner<CacheKey, CacheEntry>;

 public:
  LiteCore(Application &app, const size_t &max_item_count,
           std::string &backend_addr, std::string &backend_port,
           const char pipe_path[],
           std::function<void(ThreadSafeSet<void *> &live_connections)>
               ReconnectToBackend,
           std::function<void(ThreadSafeSet<void *> &live_connections)>
               DisconnectFromBackend,
           std::function<evutil_socket_t(void *)> GetBackendFdFromConnPtr,
           std::function<void(void *, std::shared_ptr<Request>)>
               PushBackPendingRequestIntoConnPtr,
           std::function<void()> WaitForAllInFlightConnections,
           std::function<void()> UnblockWorkerThreads);

  bool HandleRequest(
      std::shared_ptr<Request> req, ConnectionInfo &conn_info,
      std::deque<std::pair<std::shared_ptr<Request>, bool>> &pending_requests,
      const evutil_socket_t client_fd, const evutil_socket_t backend_fd,
      CacheInstance *cache, LoggerInstance *logger);

  bool HandleResponse(
      std::shared_ptr<Response> resp, ConnectionInfo &conn_info,
      std::deque<std::pair<std::shared_ptr<Request>, bool>> &pending_requests,
      const evutil_socket_t client_fd, CacheInstance *cache);

  std::string &backend_addr_, &backend_port_;

  bool is_replaying_ = false;

  ThreadSafeSet<void *> live_connections_;

  CacheInnerInstance cache_inner_;

  LoggerInnerInstance logger_inner_;

 private:
  Application &app_;

  std::function<void(ThreadSafeSet<void *> &live_connections)>
      ReconnectToBackend_;

  std::function<void(ThreadSafeSet<void *> &live_connections)>
      DisconnectFromBackend_;

  std::function<evutil_socket_t(void *)> GetBackendFdFromConnPtr_;

  std::function<void(void *, std::shared_ptr<Request>)>
      PushBackPendingRequestIntoConnPtr_;

  std::function<void()> WaitForAllInFlightConnections_;

  std::function<void()> UnblockWorkerThreads_;

  bool Replay();
};

}  // namespace lite
