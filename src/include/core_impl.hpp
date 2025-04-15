#pragma once

#include <chrono>
#include <map>
#include <fcntl.h>

#include "core.hpp"
#include "ebpf_worker.hpp"
#include "network_utils.hpp"
#include "server.hpp"
#include "worker.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
               IsCacheEntry<Request, CacheKey, CacheEntry>
LiteCore<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>::
    LiteCore(Application &app, const size_t &max_item_count,
             std::string &backend_addr, std::string &backend_port,
             const std::string socket_path,
             std::barrier<std::function<void()>> &barrier,
             LiteServerInstance *server_instance_ptr,
             std::unique_ptr<EbpfWorkerInstance> &ebpf_worker,
             std::vector<std::unique_ptr<WorkerInstance>> &workers,
             const std::chrono::milliseconds sliding_window_size,
             const size_t replay_expected_rps, const double flow_control_ratio,
             const size_t n_replay_threads, bool crash_recover)
    : Daemon(std::bind(&LiteCore::Replay, this, std::placeholders::_1),
             std::bind(&LiteCore::TakeOver, this, std::placeholders::_1,
                       std::placeholders::_2),
             backend_port, socket_path),
      app_(app),
      crash_recover_(crash_recover),
      cache_inner_(max_item_count, emergency_mode_),
      logger_inner_(sliding_window_size),
      backend_addr_(backend_addr),
      barrier_(barrier),
      server_instance_ptr_(server_instance_ptr),
      ebpf_worker_(ebpf_worker),
      workers_(workers),
      replay_rate_(sliding_window_size),
      replay_expected_rps_(replay_expected_rps),
      flow_control_ratio_(flow_control_ratio) {
  for (int i = 0; i < n_replay_threads; i++) {
    replay_workers_.emplace_back(new WorkerInstance(*this, barrier_));
    (**replay_workers_.rbegin()).Run("lite-replay-worker");
  }
  next_replay_worker_ = replay_workers_.begin();
}

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
                  LoggerInstance *logger, const bool forwarded) {
  if (!emergency_mode_ && backend_fd <= 0 && !is_ebpf_) {
    LOG(FATAL) << "Core: Fall back and entering emergency mode "
               << GetUNIXTimeStamp() << std::endl;
    // TakeOver();
  }

  if (emergency_mode_) {
    const bool flow_control =
        is_replaying_ &
        (flow_control_ratio_ * replay_rate_ < logger_inner_.inserting_rate_);
    // if (flow_control) {
    //   LOG(INFO) << "Flow control triggered, replay rate: " << replay_rate_
    //             << ", inserting rate: " << logger_inner_.inserting_rate_
    //             << std::endl;
    // }
    auto [packet, shutdown] = app_.EmergencyServe(std::move(req), conn_info,
                                                  cache, logger, flow_control);
    const auto buffer = packet.Serialize();
    if (!network::Write(client_fd, buffer)) {
      LOG(ERROR) << "Failed to write response to client" << std::endl;
      return false;
    }
    if (shutdown) {
      return false;
    }
  } else {
    if (!forwarded && !is_ebpf_) {
      const auto buffer = req->Serialize();
      if (!network::Write(backend_fd, buffer)) {
        LOG(ERROR) << "Failed to write request to backend" << std::endl;
        return false;
      }
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
                   const evutil_socket_t client_fd, CacheInstance *cache,
                   const bool forwarded) {
  const auto [related_stateful_request, forward_resp] =
      app_.Match(resp, conn_info, pending_requests);
  if (forward_resp) {
    if (!forwarded && !is_ebpf_) {
      const auto buffer = resp->Serialize();
      if (!network::Write(client_fd, buffer)) {
        LOG(ERROR) << "Failed to write response to client" << std::endl;
        return false;
      }
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
void LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::TakeOver(const std::vector<int> &fds,
                                    int connection_cnt) {
  ebpf_worker_->SetEmergencyMode(true);
  ebpf_worker_->ClearAllInFlightTraffic();

  emergency_mode_ = true;

  app_.NormalToEmergencyHook();

  // TODO: Remaining issue: MULTI -> (switch to emergency) ->
  // EXEC, service.cc will inject an illegal DISCARD

  // transfer client connections to workers
  for (int i = 0; i < connection_cnt; i++) {
    auto tcp_id = network::GetTCPID(fds[i]);
    auto conn =
        ebpf_worker_
            ->source_to_conn_[std::make_pair(tcp_id.dst_ip, tcp_id.dst_port)];
    if (!conn) {
      LOG(ERROR) << "Failed to find connection for client fd " << fds[i]
                 << " dst_ip: " << tcp_id.dst_ip
                 << " dst_port: " << tcp_id.dst_port << std::endl;
      continue;
    }
    conn->client_fd_ = fds[i];
    ebpf_worker_->source_to_conn_.erase(
        std::make_pair(tcp_id.dst_ip, tcp_id.dst_port));
    ebpf_worker_->conns_.erase(conn);

    // TODO: may not support quite protocol very well (e.g. Memcached setq)
    if (!conn->pending_requests_.empty()) {
      LOG(WARNING) << "Connection " << conn
                   << " pending requests size: " << conn->pending_requests_.size()
                   << ". Close the connection";
      delete conn;
      continue;
    }

    if (fcntl(fds[i], F_GETFD) == -1) {
      LOG(ERROR) << "Invalid FD before dispatching to worker: " << fds[i];
      delete conn;
      continue;
    }

    server_instance_ptr_->DispatchNewConnection(conn);
  }

  // transfer listener connections to server
  for (int i = connection_cnt; i < fds.size(); i++) {
    std::unique_ptr<ConnectionInstance> new_connection;
    LOG_IF(FATAL,
           !(new_connection = std::make_unique<ConnectionInstance>(
                 fds[i], EV_READ | EV_PERSIST, server_instance_ptr_->main_base_,
                 LiteServerInstance::EventHandler, server_instance_ptr_, *this,
                 false, nullptr)))
        << "failed to create listening connection\n";
    server_instance_ptr_->conns_.push(std::move(new_connection));
  }

  if (!crash_recover_) {
    // add all cache nodes to the log
    crash_conn_head_ = new LogEntryInstance(
        nullptr, nullptr, std::shared_ptr<ConnectionInstance *>());
    cache_inner_.VisitAllState(
        [&](CacheStateInstance *state) {
          if (!state->dirty_node) {
            LogEntryInstance *dirty = new LogEntryInstance(
                state, nullptr, crash_conn_head_->backend_conn_ptr);
            logger_inner_.Log(dirty, crash_conn_head_);
            state->dirty_node = dirty;
          }
        },
        false);
  }
  LOG(WARNING) << "Entered emergency mode " << GetUNIXTimeStamp() << std::endl;

  size_t cnt = 0;
  cache_inner_.VisitAllState([&](CacheStateInstance *state) { cnt++; }, false);
  LOG(INFO) << "Cache count: " << cnt << std::endl;
}

#define SendReplayReq(conn, req, buffer)                                     \
  do {                                                                       \
    /* assert((conn)->pending_requests_.empty()); */                         \
    /* (conn)->pending_requests_.push_back(std::make_pair((req), false)); */ \
    if (!network::Write((conn)->backend_fd_, (buffer))) {                    \
      LOG(ERROR) << "line#" << __LINE__                                      \
                 << " Replay failed to write to backend\n";                  \
      return false;                                                          \
    }                                                                        \
  } while (0)

#define SendReplayReqWithoutAssertion(conn, req, buffer)                     \
  do {                                                                       \
    /* (conn)->pending_requests_.push_back(std::make_pair((req), false)); */ \
    if (!network::Write((conn)->backend_fd_, (buffer))) {                    \
      LOG(ERROR) << "line#" << __LINE__                                      \
                 << " Replay failed to write to backend\n";                  \
      return false;                                                          \
    }                                                                        \
  } while (0)

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::Replay(const int full_fd) {
  const auto start_time = std::chrono::high_resolution_clock::now();

  replay_rate_.Reset(replay_expected_rps_);  // Reset the sliding window
  is_replaying_ = true;
  LOG(INFO) << "replay start, live connections: " << live_connections_.size()
            << std::endl;
  int replay_id = 0;
  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    if (!c->ConnectBackend()) {
      LOG(ERROR) << "Failed to connect to backend" << std::endl;
    } else {
      LOG(INFO) << "Connect backend " << c->backend_fd_ << " to "
                << c->client_fd_ << std::endl;
      c->replay_conn_id_ = replay_id++;
    }
  });
  // TODO: handle new client connections/client connection closeing after this

  // replay
  std::map<WorkerInstance *, ConnectionInstance *>
      replay_worker_sync_state_conns;

  for (auto &replay_worker_ : replay_workers_) {
    replay_worker_sync_state_conns[replay_worker_.get()] =
        replay_worker_->NewReplayConnection();
  }
  next_replay_worker_ = replay_workers_.begin();

  LogEntryInstance *entry;

  for (int i = 0; i < 2;
       i++) {  // Double flush to process in-flight connections
    size_t log_cnt = 0, dirty_cnt = 0;
    while (logger_inner_.Pop(entry)) {
      if (entry->state) {
        dirty_cnt++;
        const auto req = entry->state->value.ToRequest(entry->state->key);
        const auto buffer = req->Serialize();
        auto &replay_conn =
            replay_worker_sync_state_conns[next_replay_worker_->get()];
        // replay_conn->pending_requests_.wait_for_empty();
        SendReplayReq(replay_conn, req, buffer);
        next_replay_worker_++;
        if (next_replay_worker_ == replay_workers_.end())
          next_replay_worker_ = replay_workers_.begin();
      } else {
        log_cnt++;
        const auto buffer = entry->req->Serialize();
        if (!*entry->backend_conn_ptr) {  // log belongs to a closed connection
          auto replay_conn = (*next_replay_worker_)->NewReplayConnection();
          *entry->backend_conn_ptr = replay_conn;
          SendReplayReq(replay_conn, entry->req, buffer);
          next_replay_worker_++;
          if (next_replay_worker_ == replay_workers_.end())
            next_replay_worker_ = replay_workers_.begin();
        } else {
          // if (!i) {
          //   (*entry->backend_conn_ptr)->pending_requests_.wait_for_empty();
          // } else {
          //   // TODO: how to disable the reading from client event, instead of
          //   // blocking the worker. So that we can wait for the server's
          //   // responses
          // }
          SendReplayReqWithoutAssertion(*entry->backend_conn_ptr, entry->req,
                                        buffer);
        }
      }
      delete entry;
      ++replay_rate_;
    }
    LOG(INFO) << "Replay i = " << i << " finished with " << log_cnt
              << " log entries and " << dirty_cnt << " dirty entries\n";

    if (!i) {  // Wait for all inflight connections
      LOG(INFO) << "Replay barrier initialized" << std::endl;
      for (auto &worker : workers_) {
        worker->notify_queue_.push_back(
            {.type = WorkerMessage::Type::kBarrier, .fd = 0});
        uint64_t buf = 1;
        PLOG_IF(ERROR, write(worker->notify_event_fd, &buf, sizeof(uint64_t)) !=
                           sizeof(uint64_t))
            << "failed writing to worker eventfd";
      }
      barrier_.arrive_and_wait();
      ebpf_worker_->SetEmergencyMode(false);
    }
  }

  LOG(INFO) << "Waiting for all replay connections to finish\n";
  // for (auto &replay_worker : replay_workers_) {
  //   replay_worker->conns_.visit_all([&](ConnectionInstance *const &c) {
  //     c->pending_requests_.wait_for_empty();
  //   });
  // }
  // TODO: wait for all live connections to receive replay responses

  is_replaying_ = false;

  app_.EmergencyToNormalHook();

  emergency_mode_ = false;

  if (!TransferConnectionsToServer(full_fd)) {
    LOG(ERROR) << "Failed to transfer connections to server" << std::endl;
    return false;
  }

  LOG(WARNING) << "Daemon: Exited emergency mode " << GetUNIXTimeStamp()
               << std::endl;

  barrier_.arrive_and_wait();  // unblock worker threads

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();
  LOG(INFO) << "Replay took " << duration << " ms\n";

  sleep(10); // TODO: it's better to wait for a signal from the full version

  while (!dead_connection_log_heads_.empty()) {
    auto head = dead_connection_log_heads_.pop_front();
    delete head;
  }

  for (auto &replay_worker_ : replay_workers_) {
    replay_worker_->RemoveAllConnections();
  }

  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::TransferConnectionsToServer(const int full_fd) {
  std::array<int, 2> lens = {static_cast<int>(live_connections_.size()), 0};
  std::vector<int> fds;

  // client connections
  fds.resize(lens[0]);
  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    fds[c->replay_conn_id_] = c->client_fd_;
    c->DetachFromWorker();
    ebpf_worker_->conns_.insert(c);
    auto tcp_id = network::GetTCPID(c->client_fd_);
    ebpf_worker_
        ->source_to_conn_[std::make_pair(tcp_id.dst_ip, tcp_id.dst_port)] = c;
    // LOG(INFO) << "Transfer " << c << " " << tcp_id.dst_ip << ":" << tcp_id.dst_port;
  });

  std::queue<std::unique_ptr<ConnectionInstance>> conns;

  // listener connections
  lens[1] = lens[0];
  while (!server_instance_ptr_->conns_.empty()) {
    auto conn = std::move(server_instance_ptr_->conns_.front());
    fds.push_back(conn->client_fd_);
    lens[1]++;
    conn->DetachFromWorker();
    server_instance_ptr_->conns_.pop();
    conns.push(std::move(conn));
  }

  if (!network::SendSockets(full_fd, fds, lens)) {
    LOG(ERROR) << "Failed to transfer sockets to the full process";
    close(full_fd);
    return false;
  }

  while (!conns.empty()) conns.pop();

  return true;
}

}  // namespace lite