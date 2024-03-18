#pragma once

#include <event.h>

#include <lite.hpp>
#include <memory>
#include <queue>
#include <string>

#include "connection.hpp"
#include "service.hpp"
#include "worker.hpp"

class MemcachedServer {
 public:
  MemcachedServer& operator=(const MemcachedServer&) = delete;

  /// Construct the server with the given thread pool size and maximum.
  explicit MemcachedServer(const size_t& nthreads, const size_t& max_item_count,
                           std::string& backend_addr,
                           std::string& backend_port);

  /// Listen on the specified TCP port.
  bool Run(const char* port);

  /// Dispatch a new connection to the next thread in round-robin order.
  void DispatchNewConnection(const evutil_socket_t sfd);

 private:
  std::string &backend_addr_, &backend_port_;

  /// The internal service implementation.
  MemcachedService service_;

  /// The internal lite server
  MemcachedLiteServer lite_server_;

  /// The worker threads.
  std::vector<std::unique_ptr<Worker>> workers_;

  /// The next thread to use for a new connection.
  decltype(workers_)::iterator next_worker_;

  /// The event base for the server thread.
  struct event_base* main_base_;

  /// The queue of listening sockets.
  std::queue<std::unique_ptr<Connection>> conns_;

  /// Handle a new connection.
  static void EventHandler(const evutil_socket_t fd, const short which,
                           void* arg_conn);
};

extern MemcachedServer* main_server_ptr;