#pragma once

#include <barrier>
#include <boost/interprocess/sync/named_mutex.hpp>

#include "cache.hpp"
#include "concept.hpp"
#include "daemon.hpp"
#include "logger.hpp"
#include "thread_safe_queue.hpp"
#include "thread_safe_set.hpp"

namespace lite {

namespace bip = boost::interprocess;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Connection;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Worker;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
class LiteServer;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsCacheKey<CacheKey> && IsCacheEntry<Request, CacheKey, CacheEntry>

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
  using ServerInstance = LiteServer<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using CacheStateInstance = CacheState<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using ConnectionStateStorageInstance =
      ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                             CacheKey, CacheEntry>;

 public:
  LiteCore(Application &app, const size_t &max_item_count,
           const size_t &shared_memory_size, std::string &backend_addr,
           std::string &backend_port, const char socket_path[],
           std::barrier<std::function<void()>> &barrier,
           ServerInstance *server_instance_ptr,
           std::vector<std::unique_ptr<WorkerInstance>> &workers,
           const std::chrono::milliseconds sliding_window_size,
           const size_t replay_expected_rps, const double flow_control_ratio,
           const size_t n_replay_threads, bool crash_recover = true);

  bool HandleRequest(ShmSharedPtr<Request> req, ConnectionInfo &conn_info,
                     ShmThreadSafeQueue<std::pair<ShmSharedPtr<Request>, bool>>
                         &pending_requests,
                     const evutil_socket_t client_fd,
                     const evutil_socket_t backend_fd, CacheInstance *cache,
                     LoggerInstance *logger, const bool forwarded);

  //   NOTE: we don't need to handle responses in emebedded mode
  //   bool HandleResponse(ShmSharedPtr<Response> resp, ConnectionInfo
  //   &conn_info,
  //                       ShmThreadSafeQueue<std::pair<ShmSharedPtr<Request>,
  //                       bool>>
  //                           &pending_requests,
  //                       const evutil_socket_t client_fd, CacheInstance
  //                       *cache, const bool forwarded);

  bip::managed_shared_memory shared_memory_;

  std::string &backend_addr_;

  bool is_replaying_ = false;

  ShmAtomic<bool> *emergency_mode_ptr_;

  ThreadSafeSet<ConnectionInstance *> live_connections_;

  CacheInnerInstance *cache_inner_ptr_;

  LoggerInnerInstance *logger_inner_ptr_;

  ConnectionStateStorageInstance *connection_state_storage_ptr_;

  ThreadSafeQueue<LogEntryInstance *>
      dead_connection_log_heads_;  // TODO: don't need to be in shared memory

  LogEntryInstance *crash_conn_head_ = nullptr;

  Application &app_;

 private:
  bool crash_recover_;

  std::barrier<std::function<void()>> &barrier_;

  ServerInstance *server_instance_ptr_;

  std::vector<std::unique_ptr<WorkerInstance>> &workers_;

  void TakeOver(const std::vector<int> &fds, int connection_cnt);

  SlidingWindow replay_rate_;

  const size_t replay_expected_rps_;  // TODO: configurable by lite_cli

  const double flow_control_ratio_;  // TODO: configurable by lite_cli

  std::vector<std::unique_ptr<WorkerInstance>> replay_workers_;
  typename decltype(replay_workers_)::iterator next_replay_worker_;

  bool Replay(const int full_fd);

  bool TransferConnectionsToServer(const int full_fd);
};

}  // namespace lite
