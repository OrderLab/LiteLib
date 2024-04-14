#pragma once

#include <event.h>
#include <pthread.h>
#include <readerwriterqueue.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <queue>
#include <string>

#include "connection.hpp"
#include "core.hpp"

namespace lite {

template <typename Packet, typename Application, typename CacheKey,
          typename CacheEntry, typename LogEntry>
class Worker {
  using ConnectionInstance =
      Connection<Packet, Application, CacheKey, CacheEntry, LogEntry>;
  using LiteCoreInstance =
      LiteCore<Application, std::shared_ptr<Packet>, ConnectionInstance,
               CacheKey, CacheEntry, LogEntry>;

 public:
  explicit Worker(LiteCoreInstance &lite_core) : lite_core_(lite_core) {
    notify_event_fd = eventfd(0, EFD_NONBLOCK);
    if (notify_event_fd == -1) {
      perror("failed creating eventfd for worker thread");
      exit(1);
    }

    struct event_config *ev_config;
    ev_config = event_config_new();
    event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
    base_ = event_base_new_with_config(ev_config);
    event_config_free(ev_config);

    event_set(&notify_event_, notify_event_fd, EV_READ | EV_PERSIST,
              NotifyHandler, this);

    event_base_set(base_, &notify_event_);

    if (event_add(&notify_event_, 0) == -1) {
      fprintf(stderr, "Can't monitor libevent notify pipe\n");
      exit(1);
    }
  }

  /// Create the worker thread and start running the event loop.
  void Run() {
    pthread_attr_t attr;
    int ret;

    pthread_attr_init(&attr);

    if ((ret = pthread_create(&thread_id_, &attr, ThreadBody, this)) != 0) {
      fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
      exit(1);
    }

    pthread_setname_np(thread_id_, "mc-worker");
  }

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

  /// The entry point for the worker thread.
  static void *ThreadBody(void *arg_self) {
    Worker *self = static_cast<Worker *>(arg_self);

    event_base_loop(self->base_, 0);
    event_base_free(self->base_);

    return NULL;
  }

  /// Handle a new notification.
  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self) {
    Worker *self = static_cast<Worker *>(arg_self);

    if (fd == self->notify_event_fd) {
      uint64_t counter = 0;
      if (read(fd, &counter, sizeof(uint64_t)) != sizeof(uint64_t)) {
        fprintf(stderr, "Worker can't read from libevent pipe\n");
        return;
      }
      while (counter--) {
        evutil_socket_t sfd;
        if (!self->notify_queue_.try_dequeue(sfd)) {
          fprintf(stderr, "Worker can't dequeue from notify_queue\n");
          return;
        }
        std::unique_ptr<ConnectionInstance> new_connection;
        if (!(new_connection = std::make_unique<ConnectionInstance>(
                  sfd, EV_READ | EV_PERSIST, self->base_, ConnectionHandler,
                  nullptr, self->lite_core_, true))) {
          fprintf(stderr, "failed to create listening connection\n");
          exit(EXIT_FAILURE);
        }
        self->conns_.push(std::move(new_connection));
      }
    } else {
    }
  }

  /// Handle a new requests from connections.
  static void ConnectionHandler(evutil_socket_t fd, short which,
                                void *arg_conn) {
    ConnectionInstance *c = static_cast<ConnectionInstance *>(arg_conn);
    if (!c->Read()) {
      delete c;
      // TODO: remove it from conns_
    }
  }
};

}  // namespace lite