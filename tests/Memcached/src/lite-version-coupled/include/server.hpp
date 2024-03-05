#pragma once

#include <event.h>

#include <memory>
#include <queue>

#include "connection.hpp"
#include "request_handler.hpp"
#include "worker.hpp"

namespace memcached {
namespace server {

/// The top-level class of the MEMCACHED server.
class Server {
 public:
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  /// Construct the server with the given thread pool size and maximum.
  explicit Server(const size_t& nthreads, const size_t& max_item_count);

  /// Listen on the specified TCP port.
  bool Run(const char* port);

  /// Dispatch a new connection to the next thread in round-robin order.
  void DispatchNewConnection(const evutil_socket_t sfd);

 private:
  /// The worker threads.
  std::vector<std::unique_ptr<Worker>> workers_;

  /// The next thread to use for a new connection.
  decltype(workers_)::iterator next_worker_;

  /// The event base for the server thread.
  struct event_base* main_base_;

  /// The queue of listening sockets.
  std::queue<std::unique_ptr<Connection>> conns_;

  /// The handler used to process the incoming request.
  RequestHandler request_handler_;

  /// Handle a new connection.
  static void EventHandler(const evutil_socket_t fd, const short which,
                           void* arg_conn);
};

extern Server* main_server_ptr;

}  // namespace server
}  // namespace memcached
