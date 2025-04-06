#pragma once

#include <pthread.h>

#include <barrier>

#include "connection.hpp"
#include "core.hpp"
#include "thread_safe_queue.hpp"
#include "thread_safe_set.hpp"

namespace lite {

struct WorkerMessage {
  enum class Type {
    kNewClientConnection,
    kBarrier,
  };

  Type type;
  evutil_socket_t fd;
};

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

  ~Worker();

  /// Create the worker thread and start running the event loop.
  void Run(const char name[] = "lite-worker");

  /// The file descriptor used to signal the worker thread.
  evutil_socket_t notify_event_fd;

  /// The queue used to store the notification.
  ThreadSafeQueue<WorkerMessage> notify_queue_;

  /// The connections managed by the worker thread.
  ThreadSafeSet<ConnectionInstance *> conns_;

  void RemoveAllConnections();

  ConnectionInstance *NewReplayConnection();

 private:
  /// PID of the worker thread.
  pthread_t thread_id_;

  /// The event base for the worker thread.
  struct event_base *base_;

  /// The event used to notify the worker thread.
  struct event notify_event_;

  /// The underlying service implementation.
  LiteCoreInstance &lite_core_;

  std::barrier<std::function<void()>> &barrier_;

  /// The entry point for the worker thread.
  static void *ThreadBody(void *arg_self);

  /// Handle a new notification.
  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self);
};

}  // namespace lite