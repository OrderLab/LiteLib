#pragma once

#include <chrono>

#include "core.hpp"
#include "network_utils.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
               IsCacheEntry<Request, CacheKey, CacheEntry>
LiteCore<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>::
    LiteCore(Application &app, const size_t &max_item_count,
             std::string &backend_addr, std::string &backend_port,
             const char pipe_path[],
             std::barrier<std::function<void()>> &barrier,
             std::vector<std::unique_ptr<WorkerInstance>> &workers)
    : Daemon([&] { return Replay(); },
             [&] {
               LOG(INFO) << "Disconnect all from backend" << std::endl;
               live_connections_.visit_all([&](ConnectionInstance *const &c) {
                 if (c->backend_fd_ > 0) {
                   close(c->backend_fd_);
                   c->backend_fd_ = -1;
                 }
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
           IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::
    HandleRequest(std::shared_ptr<Request> req, ConnectionInfo &conn_info,
                  ThreadSafeQueue<std::pair<std::shared_ptr<Request>, bool>>
                      &pending_requests,
                  const evutil_socket_t client_fd,
                  const evutil_socket_t backend_fd, CacheInstance *cache,
                  LoggerInstance *logger) {
  if (!emergency_mode_ && backend_fd <= 0) {
    LOG(WARNING) << "Fallback to emergency mode" << std::endl;
    emergency_mode_ = true;
  }

  if (emergency_mode_) {
    auto packet = app_.EmergencyServe(std::move(req), conn_info, cache, logger);
    const auto buffer = packet.Serialize();
    if (!network::Write(client_fd, buffer)) {
      LOG(ERROR) << "Failed to write response to client" << std::endl;
      return false;
    }
  } else {
    const auto buffer = req->Serialize();
    if (!network::Write(backend_fd, buffer)) {
      LOG(ERROR) << "Failed to write request to backend" << std::endl;
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
           IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::
    HandleResponse(std::shared_ptr<Response> resp, ConnectionInfo &conn_info,
                   ThreadSafeQueue<std::pair<std::shared_ptr<Request>, bool>>
                       &pending_requests,
                   const evutil_socket_t client_fd, CacheInstance *cache) {
  const auto [related_stateful_request, forward_resp] =
      app_.Match(resp, conn_info, pending_requests);
  if (forward_resp) {
    const auto buffer = resp->Serialize();
    if (!network::Write(client_fd, buffer)) {
      LOG(ERROR) << "Failed to write response to client" << std::endl;
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
           IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::Replay() {
  const auto start_time = std::chrono::high_resolution_clock::now();

  is_replaying_ = true;

  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    c->pending_requests_
        .clear();  // clear pending requests left by aborted connections
    c->ConnectBackend();
    LOG(INFO) << "Connect backend " << c->backend_fd_ << " to " << c->client_fd_
              << std::endl;
  });

  auto replay_base = event_base_new();
  ConnectionInstance replay_conn(0, EV_READ | EV_PERSIST, replay_base,
                                 ConnectionInstance::ClientHandler, nullptr,
                                 *this, false);
  replay_conn.ConnectBackend();
  size_t tries = 0;
  while (replay_conn.backend_fd_ == -1) {
    if (tries++ > 100) {
      LOG(ERROR) << "Replay failed to connect to backend\n";
      return false;
    }
    replay_conn.ConnectBackend();
  }
  LOG(INFO) << "Replay connected to backend in " << tries << " tries\n";

#ifndef NDEBUG
  auto backend_fd = network::TryConnectBackend(backend_addr_, backend_port_);
  std::vector<uint8_t> debug_message = {
      '*', '2',  '\r', '\n', '$', '4', '\r', '\n', 'P', 'I',  'N',
      'G', '\r', '\n', '$',  '1', '2', '\r', '\n', 'r', 'e',  'p',
      'l', 'a',  'y',  ' ',  's', 't', 'a',  'r',  't', '\r', '\n'};
  auto _ = network::Write(backend_fd, std::move(debug_message));
#endif

  LogEntryInstance *entry;
  std::set<ConnectionInstance *> dead_conns;

  for (int i = 0; i < 2; i++) {  // Double flush to ensure the consistency of
                                 // in-flight connections
    size_t log_cnt = 0, dirty_cnt = 0;
    while (LoggerInstance::Pop(logger_inner_, entry)) {
      if (entry->state) {
        dirty_cnt++;
        const auto req = entry->state->value.ToRequest(entry->state->key);
        const auto buffer = req->Serialize();
        replay_conn.pending_requests_.push_back(std::make_pair(req, false));
        if (!network::Write(replay_conn.backend_fd_, buffer)) {
          LOG(ERROR) << "Replay failed to write dirty to backend\n";
          return false;
        }
        // Wait for the full to handle it
        // TODO: use batched requests
        while (!replay_conn.pending_requests_.empty()) {
          event_base_loop(replay_base, EVLOOP_ONCE);
          // TODO: what if there're errors
        }
      } else {
        log_cnt++;
        const auto buffer = entry->req->Serialize();
        if (!*entry->backend_conn_ptr) {  // log belongs to a closed connection
          auto conn_ptr = new ConnectionInstance(
              0, EV_READ | EV_PERSIST, replay_base,
              ConnectionInstance::ClientHandler, nullptr, *this, false);
          dead_conns.insert(conn_ptr);
          *entry->backend_conn_ptr = conn_ptr;
          conn_ptr->ConnectBackend();
          conn_ptr->pending_requests_.push_back(
              std::make_pair(entry->req, false));
          if (!network::Write(conn_ptr->backend_fd_, buffer)) {
            LOG(ERROR) << "Replay failed to write to backend\n";
            return false;
          }
          // Wait for the full to handle it
          // TODO: use batched requests
          while (!conn_ptr->pending_requests_.empty()) {
            event_base_loop(replay_base, EVLOOP_ONCE);
            // TODO: what if there're errors
          }
        } else {
          // TODO: use batched requests
          (*entry->backend_conn_ptr)
              ->pending_requests_.push_back(std::make_pair(entry->req, false));
          if (!network::Write((*entry->backend_conn_ptr)->backend_fd_,
                              buffer)) {
            LOG(ERROR) << "Replay failed to write to backend\n";
            // TODO: push back entry
            return false;
          }
          if (dead_conns.count(
                  (*entry->backend_conn_ptr))) {  // a reestablished dead
                                                  // connection
            while (!replay_conn.pending_requests_.empty()) {
              event_base_loop(replay_base, EVLOOP_ONCE);
              // TODO: what if there're errors
            }
          } else {
            if (!i) {
              (*entry->backend_conn_ptr)->pending_requests_.wait_for_empty();
            } else {
              // TODO: transfer the backend connection from its worker to the
              // replay thread, process the response, and transfer it back
            }
          }
        }
      }
      delete entry;
    }
#ifndef NDEBUG
    debug_message = {'*', '2', '\r', '\n', '$',  '4', '\r', '\n', 'P',
                     'I', 'N', 'G',  '\r', '\n', '$', '6',  '\r', '\n',
                     'r', 'e', 'p',  'l',  'a',  'y', '\r', '\n'};
    auto _ = network::Write(backend_fd, std::move(debug_message));
#endif
    LOG(INFO) << "Replay i = " << i << " finished with " << log_cnt
              << " log entries and " << dirty_cnt << " dirty entries\n";

    if (!i) {  // Wait for all inflight connections
      LOG(INFO) << "Replay barrier initialized" << std::endl;
      for (auto &worker : workers_) {
        worker->notify_queue_.enqueue(-1);
        uint64_t buf = 1;
        PLOG_IF(ERROR, write(worker->notify_event_fd, &buf, sizeof(uint64_t)) !=
                           sizeof(uint64_t))
            << "failed writing to worker eventfd";
      }
      barrier_.arrive_and_wait();
    }
  }

  is_replaying_ = false;
  emergency_mode_ = false;
#ifndef NDEBUG
  debug_message = {'*',  '2',  '\r', '\n', '$',  '4',  '\r', '\n',
                   'P',  'I',  'N',  'G',  '\r', '\n', '$',  '4',
                   '\r', '\n', 'm',  'o',  'd',  'e',  '\r', '\n'};
  _ = network::Write(backend_fd, std::move(debug_message));
  close(backend_fd);
#endif
  LOG(INFO) << "Daemon: Exiting emergency mode" << std::endl;

  barrier_.arrive_and_wait();  // unblock worker threads

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();
  LOG(INFO) << "Replay took " << duration << " ms\n";

  event_base_free(replay_base);

  for (auto c : dead_conns) delete c;
  while (!dead_connection_log_heads_.empty()) {
    delete dead_connection_log_heads_.pop_front();
  }

  return true;
}

}  // namespace lite