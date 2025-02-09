#pragma once

#include <barrier>
#include <memory>
#include <queue>
#include <string>

#include "connection.hpp"
#include "core.hpp"
#include "worker.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
class LiteServer {
  using ConnectionInstance = Connection<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LiteCoreInstance = LiteCore<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using WorkerInstance = Worker<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>;
  using CacheInstance = Cache<Application, Request, Response, ConnectionInfo,
                              CacheKey, CacheEntry>;

 public:
  LiteServer& operator=(const LiteServer&) = delete;

  /// Construct the server with the given thread pool size and maximum.
  /// when backend_addr is empty, backend_port is treated as a unix socket path
  explicit LiteServer(const size_t& nthreads, const size_t& max_item_count,
                      Application& app, std::string& backend_addr,
                      std::string& backend_port,
                      const std::chrono::milliseconds sliding_window_size,
                      const size_t replay_expected_rps,
                      const double flow_control_ratio = 0.9,
                      const size_t n_replay_threads = 1,
                      const char pipe_path[] = "/tmp/lite",
                      bool crash_recover = true);

  /// Listen on the specified TCP port.
  bool Run(const char* port);

  /// Dispatch a new connection to the next thread in round-robin order.
  void DispatchNewConnection(const evutil_socket_t sfd);

  CacheInstance* GetCacheDecoupledFromAnyConnection();

  /// The internal lite server
  LiteCoreInstance lite_core_;

 private:
  static int NewSocket(struct addrinfo* addr_info);

  /// The worker threads.
  std::vector<std::unique_ptr<WorkerInstance>> workers_;

  /// sync point for replaying
  std::barrier<std::function<void()>> barrier_;

  /// The next thread to use for a new connection.
  typename decltype(workers_)::iterator next_worker_;

  /// The event base for the server thread.
  struct event_base* main_base_;

  /// The queue of listening sockets.
  std::queue<std::unique_ptr<ConnectionInstance>> conns_;

  /// Handle a new connection.
  static void EventHandler(const evutil_socket_t fd, const short which,
                           void* arg_conn);
};

}  // namespace lite