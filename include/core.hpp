#pragma once

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
           std::function<void()> WaitForAllInFlightConnections)
      : Daemon([&] { return Replay(); },
               [&] { DisconnectFromBackend_(live_connections_); }, backend_port,
               pipe_path),
        app_(app),
        cache_inner_(max_item_count, emergency_mode_),
        backend_addr_(backend_addr),
        backend_port_(backend_port),
        DisconnectFromBackend_(DisconnectFromBackend),
        ReconnectToBackend_(ReconnectToBackend),
        GetBackendFdFromConnPtr_(GetBackendFdFromConnPtr),
        PushBackPendingRequestIntoConnPtr_(PushBackPendingRequestIntoConnPtr),
        WaitForAllInFlightConnections_(WaitForAllInFlightConnections) {}

  bool HandleRequest(
      std::shared_ptr<Request> req, ConnectionInfo &conn_info,
      std::deque<std::pair<std::shared_ptr<Request>, bool>> &pending_requests,
      const evutil_socket_t client_fd, const evutil_socket_t backend_fd,
      CacheInstance *cache, LoggerInstance *logger) {
    if (!emergency_mode_ && backend_fd <= 0) {
      std::cerr << "Fallback to emergency mode" << std::endl;
      emergency_mode_ = true;
    }

    if (emergency_mode_) {
      auto packet =
          app_.EmergencyServe(std::move(req), conn_info, cache, logger);
      const auto buffer = packet.Serialize();
      if (!network::Write(client_fd, buffer)) {
        std::cerr << "Failed to write response to client" << std::endl;
        return false;
      }
    } else {
      const auto buffer = req->Serialize();
      if (!network::Write(backend_fd, buffer)) {
        std::cerr << "Failed to write request to backend" << std::endl;
        return false;
      }
      // TODO: enable application to filter/modify requests before pushing back
      pending_requests.push_back(std::make_pair(req, true));
    }
    return true;
  }

  bool HandleResponse(
      std::shared_ptr<Response> resp, ConnectionInfo &conn_info,
      std::deque<std::pair<std::shared_ptr<Request>, bool>> &pending_requests,
      const evutil_socket_t client_fd, CacheInstance *cache) {
    if (emergency_mode_) {
      std::cerr << "Trying to handle a response in emergency mode" << std::endl;
      return false;
    }

    const auto [related_stateful_request, forward_resp] =
        app_.Match(resp, conn_info, pending_requests);
    if (forward_resp) {
      const auto buffer = resp->Serialize();
      if (!network::Write(client_fd, buffer)) {
        std::cerr << "Failed to write response to client" << std::endl;
        return false;
      }
      // TODO: in parallel with network::Write MSG_DONTWAIT? O_NONBLOCK?
      app_.NormalUpdate(resp, std::move(related_stateful_request), conn_info,
                        cache);
    } else {
      app_.HandleReplayResponse(resp, std::move(related_stateful_request),
                                conn_info, cache);
    }

    return true;
  }

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

  bool Replay() {
    is_replaying_ = true;
    ReconnectToBackend_(live_connections_);

    evutil_socket_t backend_fd;
    size_t tries = 0;
    while ((backend_fd = network::TryConnectBackend(backend_addr_,
                                                    backend_port_)) == -1) {
      if (tries++ > 100) {
        std::cerr << "Replay failed to connect to backend\n";
        return false;
      }
    }
    std::cerr << "Replay connected to backend in " << tries << " tries\n";

    typename LoggerInner<Request>::LogEntry *entry;
    size_t log_cnt = 0, dirty_cnt = 0;

    for (int i = 0; i < 2; i++) {  // Double flush to ensure the consistency of
                                   // in-flight connections
      while (LoggerInstance::Pop(logger_inner_, entry)) {  // TODO: less writes
        if (entry->state) {
          dirty_cnt++;
          auto state =
              static_cast<typename CacheInner<CacheKey, CacheEntry>::State *>(
                  entry->state);
          const auto buffer = state->value.ToRequests(state->key);
          if (!network::Write(backend_fd, buffer)) {
            std::cerr << "Replay failed to write dirty to backend\n";
            return false;
          }
        } else {
          log_cnt++;
          const auto buffer = entry->req->Serialize();
          if (!*entry->backend_conn_ptr) {
            // TODO: what if there're errors? nothing will receive the response
            if (!network::Write(backend_fd, buffer)) {
              std::cerr << "Replay failed to write to backend\n";
              return false;
            }
          } else {
            // TODO: lock the connection here
            PushBackPendingRequestIntoConnPtr_(*entry->backend_conn_ptr,
                                               entry->req);
            if (!network::Write(
                    GetBackendFdFromConnPtr_(*entry->backend_conn_ptr),
                    buffer)) {
              std::cerr << "Replay failed to write to backend\n";
              // TODO: push back entry
              return false;
            }
          }
        }
        delete entry;
      }
      if (!i) WaitForAllInFlightConnections_();
    }

    // TODO: in-flight requests after this?
    close(backend_fd);
    std::cerr << "Replay finished with " << log_cnt << " log entries and "
              << dirty_cnt << " dirty entries\n";

    is_replaying_ = false;

    return true;
  }
};

}  // namespace lite
