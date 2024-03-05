#pragma once

#include <event.h>
#include <pthread.h>

#include <memory>
#include <queue>

#include "connection.hpp"

namespace memcached {
namespace server {
class Worker {
 public:
  explicit Worker(RequestHandler &request_handler);

  /// Create the worker thread and start running the event loop.
  void Run();

  /// The file descriptor used to signal the worker thread.
  evutil_socket_t notify_event_fd;

 private:
  /// PID of the worker thread.
  pthread_t thread_id_;

  /// The event base for the worker thread.
  struct event_base *base_;

  /// The event used to notify the worker thread.
  struct event notify_event_;

  /// The handler for all incoming requests.
  RequestHandler &request_handler_;

  /// The connections managed by the worker thread.
  std::queue<std::unique_ptr<Connection>> conns_;

  /// The entry point for the worker thread.
  static void *ThreadBody(void *arg);

  /// Handle a new notification.
  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self);

  /// Handle a new requests from connections.
  static void ConnectionHandler(evutil_socket_t fd, short which,
                                void *arg_conn);
};
}  // namespace server
}  // namespace memcached