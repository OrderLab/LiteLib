#pragma once

#include <chrono>

#include "core.hpp"
#include "network_utils.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<CacheKey, CacheEntry>
LiteCore<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>::
    LiteCore(Application &app, const size_t &max_item_count,
             std::string &backend_addr, std::string &backend_port,
             const char pipe_path[],
             std::barrier<std::function<void()>> &barrier,
             std::vector<std::unique_ptr<WorkerInstance>> &workers)
    : Daemon([&] { return Replay(); },
             [&] {
               std::cerr << "Disconnect from backend" << std::endl;
               live_connections_.visit_all([&](ConnectionInstance *const &c) {
                 close(c->backend_fd_);
                 c->backend_fd_ = -1;
               });
             },
             backend_port, pipe_path),
      app_(app),
      cache_inner_(max_item_count, emergency_mode_),
      backend_addr_(backend_addr),
      backend_port_(backend_port),
      barrier_(barrier),
      workers_(workers) {}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::
    HandleRequest(
        std::shared_ptr<Request> req, ConnectionInfo &conn_info,
        std::deque<std::pair<std::shared_ptr<Request>, bool>> &pending_requests,
        const evutil_socket_t client_fd, const evutil_socket_t backend_fd,
        CacheInstance *cache, LoggerInstance *logger) {
  if (!emergency_mode_ && backend_fd <= 0) {
    std::cerr << "Fallback to emergency mode" << std::endl;
    emergency_mode_ = true;
  }

  if (emergency_mode_) {
    auto packet = app_.EmergencyServe(std::move(req), conn_info, cache, logger);
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

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::
    HandleResponse(
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

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::Replay() {
  const auto start_time = std::chrono::high_resolution_clock::now();

  is_replaying_ = true;

  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    c->ConnectBackend();
    std::cerr << "Connect backend " << c->backend_fd_ << " to " << c->client_fd_
              << std::endl;
  });

  evutil_socket_t backend_fd;
  size_t tries = 0;
  while ((backend_fd =
              network::TryConnectBackend(backend_addr_, backend_port_)) == -1) {
    if (tries++ > 100) {
      std::cerr << "Replay failed to connect to backend\n";
      return false;
    }
  }
  std::cerr << "Replay connected to backend in " << tries << " tries\n";

  LogEntry<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>
      *entry;

  for (int i = 0; i < 2; i++) {  // Double flush to ensure the consistency of
                                 // in-flight connections
    size_t log_cnt = 0, dirty_cnt = 0;
    while (LoggerInstance::Pop(logger_inner_, entry)) {  // TODO: less writes
      if (entry->state) {
        dirty_cnt++;
        const auto buffer = entry->state->value.ToRequests(entry->state->key);
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
          static_cast<ConnectionInstance *>(*entry->backend_conn_ptr)
              ->pending_requests_.push_back(std::make_pair(entry->req, false));
          if (!network::Write(
                  static_cast<ConnectionInstance *>(*entry->backend_conn_ptr)
                      ->backend_fd_,
                  buffer)) {
            std::cerr << "Replay failed to write to backend\n";
            // TODO: push back entry
            return false;
          }
        }
      }
      delete entry;
    }
    std::cerr << "Replay i = " << i << " finished with " << log_cnt
              << " log entries and " << dirty_cnt << " dirty entries\n";
    if (!i) {  // Wait for all inflight connections
      std::cerr << "Replay barrier initialized" << std::endl;
      for (auto &worker : workers_) {
        worker->notify_queue_.enqueue(-1);
        uint64_t buf = 1;
        if (write(worker->notify_event_fd, &buf, sizeof(uint64_t)) !=
            sizeof(uint64_t)) {
          perror("failed writing to worker eventfd");
        }
      }
      barrier_.arrive_and_wait();
    }
  }

  close(backend_fd);

  is_replaying_ = false;

  barrier_.arrive_and_wait();  // unblock worker threads

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();
  std::cerr << "Replay took " << duration << " ms\n";

  return true;
}

}  // namespace lite