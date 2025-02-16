#pragma once

#include <chrono>
#include <map>

#include "core.hpp"
#include "network_utils.hpp"
#include "worker.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
               IsCacheKey<CacheKey> &&
               IsCacheEntry<Request, CacheKey, CacheEntry>
LiteCore<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>::
    LiteCore(Application &app, const size_t &max_item_count,
             const size_t &shared_memory_size, std::string &backend_addr,
             std::string &backend_port, const char pipe_path[],
             std::barrier<std::function<void()>> &barrier,
             std::vector<std::unique_ptr<WorkerInstance>> &workers,
             const std::chrono::milliseconds sliding_window_size,
             const size_t replay_expected_rps, const double flow_control_ratio,
             const size_t n_replay_threads, bool crash_recover)
    : Daemon([&] { return Replay(); }, [&] { TakeOver(); }, backend_port,
             pipe_path),
      app_(app),
      shared_memory_(bip::open_or_create, "lite_shared_memory",
                     shared_memory_size),
      crash_recover_(crash_recover),
      backend_addr_(backend_addr),
      barrier_(barrier),
      workers_(workers),
      emergency_mode_ptr_(shared_memory_.find_or_construct<ShmAtomic<bool>>(
          "emergency_mode")(false)),
      replay_rate_(sliding_window_size),
      replay_expected_rps_(replay_expected_rps),
      flow_control_ratio_(flow_control_ratio) {
  InitEmergencyModePtr(emergency_mode_ptr_);

  cache_inner_ptr_ = shared_memory_.find_or_construct<CacheInnerInstance>(
      "cache_inner")(max_item_count, emergency_mode_ptr_,
                     shared_memory_.get_segment_manager());

  logger_inner_ptr_ =
      shared_memory_.find_or_construct<LoggerInnerInstance>("logger_inner")(
          sliding_window_size, shared_memory_.get_segment_manager());

  connection_state_storage_ptr_ =
      shared_memory_.find_or_construct<ConnectionStateStorageInstance>(
          "connection_state_storage")(cache_inner_ptr_, logger_inner_ptr_,
                                      shared_memory_.get_segment_manager());

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
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::
    HandleRequest(ShmSharedPtr<Request> req, ConnectionInfo &conn_info,
                  ShmThreadSafeQueue<bip::pair<ShmSharedPtr<Request>, bool>>
                      &pending_requests,
                  const evutil_socket_t client_fd,
                  const evutil_socket_t backend_fd, CacheInstance *cache,
                  LoggerInstance *logger, const bool forwarded) {
  if (!emergency_mode_ptr_->load() && backend_fd <= 0) {
    LOG(WARNING) << "Core: Fall back and entering emergency mode "
                 << GetUNIXTimeStamp() << std::endl;
    TakeOver();
  }

  if (emergency_mode_ptr_->load()) {
    const bool flow_control =
        is_replaying_ & (flow_control_ratio_ * replay_rate_ <
                         logger_inner_ptr_->inserting_rate_);
    // if (flow_control) {
    //   LOG(INFO) << "Flow control triggered, replay rate: " << replay_rate_
    //             << ", inserting rate: " << logger_inner_.inserting_rate_
    //             << std::endl;
    // }
    auto [packet, shutdown] =
        app_.EmergencyServe(req, conn_info, cache, logger, flow_control);
    const auto buffer = packet.Serialize();
    if (!network::Write(client_fd, buffer)) {
      LOG(ERROR) << "Failed to write response to client" << std::endl;
      return false;
    }
    if (shutdown) {
      return false;
    }
  } else {
    if (!forwarded) {
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
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::
    HandleResponse(ShmSharedPtr<Response> resp, ConnectionInfo &conn_info,
                   ShmThreadSafeQueue<bip::pair<ShmSharedPtr<Request>, bool>>
                       &pending_requests,
                   const evutil_socket_t client_fd, CacheInstance *cache,
                   const bool forwarded) {
  const auto [related_stateful_request, forward_resp] =
      app_.Match(resp, conn_info, pending_requests);
  if (forward_resp) {
    if (!forwarded) {
      const auto buffer = resp->Serialize();
      if (!network::Write(client_fd, buffer)) {
        LOG(ERROR) << "Failed to write response to client" << std::endl;
        return false;
      }
    }
    // TODO: in parallel with network::Write MSG_DONTWAIT? O_NONBLOCK?
    app_.NormalUpdate(resp, boost::move(related_stateful_request), conn_info,
                      cache);
  } else {
    app_.HandleReplayResponse(resp, boost::move(related_stateful_request),
                              conn_info, cache);
  }

  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>
void LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::TakeOver() {
  emergency_mode_ptr_->store(true);
  LOG(INFO) << "Disconnect all from backend" << std::endl;
  LOG(INFO) << "Emergency barrier initialized" << std::endl;
  for (auto &worker : workers_) {
    worker->notify_queue_.push_back(
        {.type = WorkerMessage::Type::kBarrier, .fd = 0});
    uint64_t buf = 1;
    PLOG_IF(ERROR, write(worker->notify_event_fd, &buf, sizeof(uint64_t)) !=
                       sizeof(uint64_t))
        << "failed writing to worker eventfd";
  }
  barrier_.arrive_and_wait();

  std::set<ConnectionInstance *> connections_to_be_closed;
  LOG(INFO) << "live connections: " << live_connections_.size() << std::endl;
  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    if (c->backend_fd_ > 0) {
      close(c->backend_fd_);
      c->backend_fd_ = -1;
    }
    if (!c->connection_state_entry_ptr_->pending_requests_.empty()) {
      // TODO: serve them using EmergencyServe
      // Remaining issue: MULTI -> (switch to emergency) ->
      // EXEC, service.cc will inject an illegal DISCARD
      connections_to_be_closed.insert(c);
    }
  });
  for (auto &conn : connections_to_be_closed) {
    live_connections_.erase(conn);
    delete conn;
  }

  app_.NormalToEmergencyHook();

  barrier_.arrive_and_wait();  // unblock worker threads
  if (!crash_recover_) {
    // add all cache nodes to the log
    crash_conn_head_ =
        new LogEntryInstance(nullptr, ShmSharedPtr<Request>{},
                             std::shared_ptr<ConnectionInstance *>());
    cache_inner_ptr_->VisitAllState(
        [&](CacheStateInstance *state) {
          if (!state->dirty_node) {
            LogEntryInstance *dirty = new LogEntryInstance(
                state, {}, crash_conn_head_->backend_conn_ptr);
            logger_inner_ptr_->Log(dirty, crash_conn_head_);
            state->dirty_node = dirty;
          }
        },
        false);
  }
  LOG(WARNING) << "Entered emergency mode " << GetUNIXTimeStamp() << std::endl;
}

#define SendReplayReq(conn, req, buffer)                               \
  do {                                                                 \
    assert((conn)->pending_requests_.empty());                         \
    (conn)->pending_requests_.push_back(std::make_pair((req), false)); \
    if (!network::Write((conn)->backend_fd_, (buffer))) {              \
      LOG(ERROR) << "line#" << __LINE__                                \
                 << " Replay failed to write to backend\n";            \
      return false;                                                    \
    }                                                                  \
  } while (0)

#define SendReplayReqWithoutAssertion(conn, req, buffer)               \
  do {                                                                 \
    (conn)->pending_requests_.push_back(std::make_pair((req), false)); \
    if (!network::Write((conn)->backend_fd_, (buffer))) {              \
      LOG(ERROR) << "line#" << __LINE__                                \
                 << " Replay failed to write to backend\n";            \
      return false;                                                    \
    }                                                                  \
  } while (0)

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::Replay() {
  // TODO: implement replay
  // const auto start_time = std::chrono::high_resolution_clock::now();

  // replay_rate_.Reset(replay_expected_rps_);  // Reset the sliding window
  // is_replaying_ = true;
  // LOG(INFO) << "replay start, live connections: " << live_connections_.size()
  //           << std::endl;
  // live_connections_.visit_all([&](ConnectionInstance *const &c) {
  //   if (!c->ConnectBackend()) {
  //     LOG(ERROR) << "Failed to connect to backend" << std::endl;
  //   } else {
  //     LOG(INFO) << "Connect backend " << c->backend_fd_ << " to "
  //               << c->client_fd_ << std::endl;
  //   }
  // });

  // std::map<WorkerInstance *, ConnectionInstance *>
  //     replay_worker_sync_state_conns;

  // for (auto &replay_worker_ : replay_workers_) {
  //   replay_worker_->RemoveAllConnections();
  //   replay_worker_sync_state_conns[replay_worker_.get()] =
  //       replay_worker_->NewReplayConnection();
  // }
  // next_replay_worker_ = replay_workers_.begin();

  // LogEntryInstance *entry;

  // for (int i = 0; i < 2;
  //      i++) {  // Double flush to process in-flight connections
  //   size_t log_cnt = 0, dirty_cnt = 0;
  //   while (LoggerInstance::Pop(*logger_inner_ptr_, entry)) {
  //     if (entry->state) {
  //       dirty_cnt++;
  //       const auto req = entry->state->value.ToRequest(entry->state->key);
  //       const auto buffer = req->Serialize();
  //       auto &replay_conn =
  //           replay_worker_sync_state_conns[next_replay_worker_->get()];
  //       replay_conn->pending_requests_.wait_for_empty();
  //       SendReplayReq(replay_conn, req, buffer);
  //       next_replay_worker_++;
  //       if (next_replay_worker_ == replay_workers_.end())
  //         next_replay_worker_ = replay_workers_.begin();
  //     } else {
  //       log_cnt++;
  //       const auto buffer = entry->req->Serialize();
  //       if (!*entry->backend_conn_ptr) {  // log belongs to a closed
  //       connection
  //         auto replay_conn = (*next_replay_worker_)->NewReplayConnection();
  //         *entry->backend_conn_ptr = replay_conn;
  //         SendReplayReq(replay_conn, entry->req, buffer);
  //         next_replay_worker_++;
  //         if (next_replay_worker_ == replay_workers_.end())
  //           next_replay_worker_ = replay_workers_.begin();
  //       } else {
  //         if (!i) {
  //           (*entry->backend_conn_ptr)->pending_requests_.wait_for_empty();
  //         } else {
  //           // TODO: how to disable the reading from client event, instead of
  //           // blocking the worker. So that we can wait for the server's
  //           // responses
  //         }
  //         SendReplayReqWithoutAssertion(*entry->backend_conn_ptr, entry->req,
  //                                       buffer);
  //       }
  //     }
  //     delete entry;
  //     ++replay_rate_;
  //   }
  //   LOG(INFO) << "Replay i = " << i << " finished with " << log_cnt
  //             << " log entries and " << dirty_cnt << " dirty entries\n";

  //   if (!i) {  // Wait for all inflight connections
  //     LOG(INFO) << "Replay barrier initialized" << std::endl;
  //     for (auto &worker : workers_) {
  //       worker->notify_queue_.push_back(
  //           {.type = WorkerMessage::Type::kBarrier, .fd = 0});
  //       uint64_t buf = 1;
  //       PLOG_IF(ERROR, write(worker->notify_event_fd, &buf, sizeof(uint64_t))
  //       !=
  //                          sizeof(uint64_t))
  //           << "failed writing to worker eventfd";
  //     }
  //     barrier_.arrive_and_wait();
  //   }
  // }

  // LOG(INFO) << "Waiting for all replay connections to finish\n";
  // for (auto &replay_worker : replay_workers_) {
  //   replay_worker->conns_.visit_all([&](ConnectionInstance *const &c) {
  //     c->pending_requests_.wait_for_empty();
  //   });
  // }
  // // TODO: wait for all live connections to receive replay responses

  // is_replaying_ = false;
  // emergency_mode_ = false;
  // LOG(WARNING) << "Daemon: Exited emergency mode " << GetUNIXTimeStamp()
  //              << std::endl;

  // app_.EmergencyToNormalHook();

  // barrier_.arrive_and_wait();  // unblock worker threads

  // const auto end_time = std::chrono::high_resolution_clock::now();
  // const auto duration =
  // std::chrono::duration_cast<std::chrono::milliseconds>(
  //                           end_time - start_time)
  //                           .count();
  // LOG(INFO) << "Replay took " << duration << " ms\n";

  // while (!dead_connection_log_heads_.empty()) {
  //   delete dead_connection_log_heads_.pop_front();
  // }

  // for (auto &replay_worker_ : replay_workers_) {
  //   replay_worker_->RemoveAllConnections();
  // }

  // return true;
}

}  // namespace lite