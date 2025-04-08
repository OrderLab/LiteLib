#pragma once

#include <chrono>
#include <map>

#include "core.hpp"
#include "network_utils.hpp"
#include "server.hpp"
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
             std::string &backend_port, const char socket_path[],
             std::barrier<std::function<void()>> &barrier,
             ServerInstance *server_instance_ptr,
             std::vector<std::unique_ptr<WorkerInstance>> &workers,
             const std::chrono::milliseconds sliding_window_size,
             const size_t replay_expected_rps, const double flow_control_ratio,
             const size_t n_replay_threads, bool crash_recover)
    : Daemon(std::bind(&LiteCore::Replay, this, std::placeholders::_1),
             std::bind(&LiteCore::TakeOver, this, std::placeholders::_1,
                       std::placeholders::_2),
             backend_port, socket_path),
      app_(app),
      shared_memory_(bip::open_or_create, "lite_shared_memory",
                     shared_memory_size),
      crash_recover_(crash_recover),
      backend_addr_(backend_addr),
      barrier_(barrier),
      server_instance_ptr_(server_instance_ptr),
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
              CacheEntry>::HandleRequest(ShmSharedPtr<Request> req,
                                         ConnectionInfo &conn_info,
                                         const evutil_socket_t client_fd,
                                         const evutil_socket_t backend_fd,
                                         CacheInstance *cache,
                                         LoggerInstance *logger,
                                         const bool forwarded) {
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
    LOG(ERROR) << "Received request in normal mode" << std::endl;
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>
void LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::TakeOver(const std::vector<int> &fds,
                                    int connection_cnt) {
  emergency_mode_ptr_->store(true);

  logger_inner_ptr_->Init();  // virtual addresses changed, init again

  app_.NormalToEmergencyHook();

  // TODO: Remaining issue: MULTI -> (switch to emergency) ->
  // EXEC, service.cc will inject an illegal DISCARD

  // transfer client connections to workers
  for (int i = 0; i < connection_cnt; i++) {
    server_instance_ptr_->DispatchNewConnection(fds[i]);
  }

  // transfer listener connections to server
  for (int i = connection_cnt; i < fds.size(); i++) {
    std::unique_ptr<ConnectionInstance> new_connection;
    LOG_IF(FATAL,
           !(new_connection = std::make_unique<ConnectionInstance>(
                 fds[i], EV_READ | EV_PERSIST, server_instance_ptr_->main_base_,
                 ServerInstance::EventHandler, server_instance_ptr_, *this,
                 false, nullptr)))
        << "failed to create listening connection\n";
    server_instance_ptr_->conns_.push(std::move(new_connection));
  }

  if (!crash_recover_) {
    // add all cache nodes to the log
    size_t log_entry_cnt = 0;
    crash_conn_head_ = cache_inner_ptr_->log_entry_allocator_.allocate_one();
    new (crash_conn_head_.get()) LogEntryInstance(
        nullptr, ShmSharedPtr<Request>{}, shared_memory_.get_segment_manager());
    cache_inner_ptr_->VisitAllState(
        [&](CacheStateInstance *state) {
          if (!state->dirty_node) {
            auto dirty = cache_inner_ptr_->log_entry_allocator_.allocate_one();
            new (dirty.get())
                LogEntryInstance(state, {}, crash_conn_head_->backend_conn_ptr);
            logger_inner_ptr_->Log(dirty, crash_conn_head_);
            state->dirty_node = dirty;
            log_entry_cnt++;
          }
        },
        false);
    LOG(INFO) << "Cached nodes: " << log_entry_cnt << std::endl;
  }
  LOG(WARNING) << "Entered emergency mode " << GetUNIXTimeStamp() << std::endl;
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
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>
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
    while (LoggerInstance::Pop(*logger_inner_ptr_, entry)) {
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
      entry->~LogEntryInstance();
      logger_inner_ptr_->log_entry_allocator_.deallocate_one(entry);
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

  if (!TransferConnectionsToServer(full_fd)) {
    LOG(ERROR) << "Failed to transfer connections to server" << std::endl;
    return false;
  }

  while (emergency_mode_ptr_->load()) {
    std::this_thread::yield();
  }

  LOG(WARNING) << "Daemon: Exited emergency mode " << GetUNIXTimeStamp()
               << std::endl;

  barrier_.arrive_and_wait();  // unblock worker threads

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();
  LOG(INFO) << "Replay took " << duration << " ms\n";

  while (!dead_connection_log_heads_.empty()) {
    auto head = dead_connection_log_heads_.pop_front();
    head->~LogEntryInstance();
    logger_inner_ptr_->log_entry_allocator_.deallocate_one(head);
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
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::TransferConnectionsToServer(const int full_fd) {
  std::array<int, 2> lens = {static_cast<int>(live_connections_.size()), 0};
  std::vector<int> fds;

  // client connections
  fds.resize(lens[0]);
  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    fds[c->replay_conn_id_] = c->client_fd_;
    c->Detach();
  });

  // listener connections
  lens[1] = lens[0];
  while (!server_instance_ptr_->conns_.empty()) {
    auto conn = std::move(server_instance_ptr_->conns_.front());
    fds.push_back(conn->client_fd_);
    lens[1]++;
    conn->Detach();
    server_instance_ptr_->conns_.pop();
  }

  if (!network::SendSockets(full_fd, fds, lens)) {
    LOG(ERROR) << "Failed to transfer sockets to the full process";
    close(full_fd);
    return false;
  }

  return true;
}

}  // namespace lite