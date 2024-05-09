#pragma once

#include <pthread.h>
#include <readerwriterqueue.h>

#include <barrier>
#include <queue>

#include "connection.hpp"
#include "core.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Worker {
  using ConnectionInstance = Connection<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LiteCoreInstance = LiteCore<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;

 public:
  explicit Worker(LiteCoreInstance &lite_core,
                  std::barrier<std::function<void()>> &barrier);

  /// Create the worker thread and start running the event loop.
  void Run();

  /// The file descriptor used to signal the worker thread.
  evutil_socket_t notify_event_fd;

  /// The queue used to store the notification.
  moodycamel::ReaderWriterQueue<evutil_socket_t> notify_queue_;

 private:
  /// PID of the worker thread.
  pthread_t thread_id_;

  /// The event base for the worker thread.
  struct event_base *base_;

  /// The event used to notify the worker thread.
  struct event notify_event_;

  /// The underlying service implementation.
  LiteCoreInstance &lite_core_;

  /// The connections managed by the worker thread.
  std::queue<std::unique_ptr<ConnectionInstance>> conns_;

  std::barrier<std::function<void()>> &barrier_;

  /// The entry point for the worker thread.
  static void *ThreadBody(void *arg_self);

  /// Handle a new notification.
  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self);
};

}  // namespace lite