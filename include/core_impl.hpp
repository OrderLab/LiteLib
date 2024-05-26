#pragma once

#include <chrono>

#include "core.hpp"
#include "network_utils.hpp"
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
             const char pipe_path[],
             std::barrier<std::function<void()>> &barrier,
             std::vector<std::unique_ptr<WorkerInstance>> &workers,
             const std::chrono::milliseconds sliding_window_size,
             const size_t replay_expected_rps, const double flow_control_ratio,
             const size_t n_replay_threads)
    : Daemon([&] { return Replay(); }, [&] { TakeOver(); }, backend_port,
             pipe_path),
      app_(app),
      cache_inner_(max_item_count, emergency_mode_),
      logger_inner_(sliding_window_size),
      backend_addr_(backend_addr),
      backend_port_(backend_port),
      barrier_(barrier),
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
                  LoggerInstance *logger) {
  if (!emergency_mode_ && backend_fd <= 0) {
    LOG(WARNING) << "Core: Fall back and entering emergency mode "
                 << GetUNIXTimeStamp() << std::endl;
    TakeOver();
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
void LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::TakeOver() {
  emergency_mode_ = true;
  LOG(INFO) << "Disconnect all from backend" << std::endl;
  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    if (c->backend_fd_ > 0) {
      close(c->backend_fd_);
      c->backend_fd_ = -1;
    }
  });
  LOG(WARNING) << "Entered emergency mode " << GetUNIXTimeStamp() << std::endl;
}

#define SendReplayReq(conn, req, buffer)                               \
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
           IsCacheEntry<Request, CacheKey, CacheEntry>
bool LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
              CacheEntry>::Replay() {
  const auto start_time = std::chrono::high_resolution_clock::now();

  replay_rate_.Reset(replay_expected_rps_);  // Reset the sliding window
  is_replaying_ = true;

  live_connections_.visit_all([&](ConnectionInstance *const &c) {
    c->pending_requests_
        .clear();  // clear pending requests left by aborted connections
    c->ConnectBackend();
    LOG(INFO) << "Connect backend " << c->backend_fd_ << " to " << c->client_fd_
              << std::endl;
  });

  for (auto &replay_worker_ : replay_workers_) {
    replay_worker_->notify_queue_.push_back(
        {.type = WorkerMessage::Type::kReplayStart, .fd = 0});
    uint64_t buf = 1;
    PLOG_IF(ERROR, write(replay_worker_->notify_event_fd, &buf,
                         sizeof(uint64_t)) != sizeof(uint64_t))
        << "failed writing to worker eventfd";
  }
  next_replay_worker_ = replay_workers_.begin();

  LogEntryInstance *entry;

  for (int i = 0; i < 2; i++) {  // Double flush to ensure the consistency of
                                 // in-flight connections
    size_t log_cnt = 0, dirty_cnt = 0;
    while (LoggerInstance::Pop(logger_inner_, entry)) {
      if (entry->state) {
        dirty_cnt++;
        const auto req = entry->state->value.ToRequest(entry->state->key);
        const auto buffer = req->Serialize();
        // Wait for the initialization of the replay connection
        while ((*next_replay_worker_)->conns_.empty()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto &replay_conn = (*next_replay_worker_)->conns_.front();
        while (replay_conn->backend_fd_ <= 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        replay_conn->pending_requests_.wait_for_empty();
        SendReplayReq(replay_conn, req, buffer);
        next_replay_worker_++;
        if (next_replay_worker_ == replay_workers_.end())
          next_replay_worker_ = replay_workers_.begin();
      } else {
        log_cnt++;
        const auto buffer = entry->req->Serialize();
        if (!*entry->backend_conn_ptr) {  // log belongs to a closed connection
          const auto &old_conn_back = (*next_replay_worker_)->conns_.back();
          (*next_replay_worker_)
              ->notify_queue_.push_back(
                  {.type = WorkerMessage::Type::kNewReplayConnection, .fd = 0});
          uint64_t buf = 1;
          PLOG_IF(ERROR, write((*next_replay_worker_)->notify_event_fd, &buf,
                               sizeof(uint64_t)) != sizeof(uint64_t))
              << "failed writing to worker eventfd";
          // Wait for the initialization of the replay connection
          while ((*next_replay_worker_)->conns_.back() == old_conn_back) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          auto replay_conn = (*next_replay_worker_)->conns_.back().get();
          while (replay_conn->backend_fd_ <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          *entry->backend_conn_ptr = replay_conn;
          SendReplayReq(replay_conn, entry->req, buffer);
          next_replay_worker_++;
          if (next_replay_worker_ == replay_workers_.end())
            next_replay_worker_ = replay_workers_.begin();
        } else {
          if (!i) {
            (*entry->backend_conn_ptr)->pending_requests_.wait_for_empty();
          } else {
            // TODO: transfer the backend connection from its worker to a
            // non-blocking thread, process the response, and transfer it back
          }
          SendReplayReq(*entry->backend_conn_ptr, entry->req, buffer);
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
    }
  }

  LOG(INFO) << "Waiting for all replay connections to finish\n";
  for (auto &replay_worker : replay_workers_) {
    replay_worker->conns_.front()->pending_requests_.wait_for_empty();
  }
  // TODO: wait for all live connections to receive replay responses

  is_replaying_ = false;
  emergency_mode_ = false;
  LOG(WARNING) << "Daemon: Exited emergency mode " << GetUNIXTimeStamp()
               << std::endl;

  barrier_.arrive_and_wait();  // unblock worker threads

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();
  LOG(INFO) << "Replay took " << duration << " ms\n";

  while (!dead_connection_log_heads_.empty()) {
    delete dead_connection_log_heads_.pop_front();
  }

  for (auto &replay_worker_ : replay_workers_) {
    replay_worker_->notify_queue_.push_back(
        {.type = WorkerMessage::Type::kReplayEnd, .fd = 0});
    uint64_t buf = 1;
    PLOG_IF(ERROR, write(replay_worker_->notify_event_fd, &buf,
                         sizeof(uint64_t)) != sizeof(uint64_t))
        << "failed writing to worker eventfd";
  }

  return true;
}

}  // namespace lite