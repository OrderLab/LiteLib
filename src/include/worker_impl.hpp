#pragma once

#include <event.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <syncstream>

#include "worker.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
Worker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::Worker(LiteCoreInstance &lite_core,
                           std::barrier<std::function<void()>> &barrier)
    : lite_core_(lite_core), barrier_(barrier) {
  PCHECK(notify_event_fd = eventfd(0, EFD_NONBLOCK))
      << "failed creating eventfd for worker thread";

  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);
  event_base_priority_init(base_, 2);

  event_set(&notify_event_, notify_event_fd, EV_READ | EV_PERSIST,
            NotifyHandler, this);

  event_base_set(base_, &notify_event_);
  event_priority_set(&notify_event_, 0);  // highest priority

  LOG_IF(FATAL, event_add(&notify_event_, 0) == -1)
      << "Can't monitor libevent notify pipe\n";
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
Worker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::~Worker() {
  event_del(&notify_event_);
  event_base_free(base_);
  close(notify_event_fd);

  // conns_.visit_all([](const auto &conn) { delete conn; });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Run(const char name[]) {
  pthread_attr_t attr;

  pthread_attr_init(&attr);

  PCHECK(!pthread_create(&thread_id_, &attr, ThreadBody, this))
      << "Can't create thread: " << name << std::endl;

  pthread_setname_np(thread_id_, name);
  pthread_attr_destroy(&attr);
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void *Worker<Application, Request, Response, ConnectionInfo, CacheKey,
             CacheEntry>::ThreadBody(void *arg_self) {
  Worker *self = static_cast<Worker *>(arg_self);

  event_base_loop(self->base_, 0);
  event_base_free(self->base_);

  return NULL;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::RemoveAllConnections() {
  std::vector<ConnectionInstance *> conns_to_be_deleted;
  conns_.visit_all(
      [&](const auto &conn) { conns_to_be_deleted.push_back(conn); });
  for (auto conn : conns_to_be_deleted) delete conn;
  conns_.clear();
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
typename Worker<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::ConnectionInstance *
Worker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::NewReplayConnection() {
  auto new_connection = new ConnectionInstance(
      0, EV_READ | EV_PERSIST, base_, ConnectionInstance::ClientHandler,
      nullptr, lite_core_, false, this);
  if (!new_connection) {
    LOG(ERROR) << "failed to create replay connection\n";
    return nullptr;
  }
  new_connection->ConnectBackend();
  conns_.insert(new_connection);
  return new_connection;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::NotifyHandler(evutil_socket_t fd, short which,
                                       void *arg_self) {
  Worker *self = static_cast<Worker *>(arg_self);

  if (fd == self->notify_event_fd) {
    uint64_t counter = 0;
    if (read(fd, &counter, sizeof(uint64_t)) != sizeof(uint64_t)) {
      LOG(ERROR) << "Worker can't read from libevent pipe\n";
      return;
    }
    while (counter--) {
      const WorkerMessage msg = self->notify_queue_.pop_front();
      if (msg.type == WorkerMessage::Type::kNewClientConnection) {
        auto new_connection =
            new ConnectionInstance(msg.fd, EV_READ | EV_PERSIST, self->base_,
                                   ConnectionInstance::ClientHandler, nullptr,
                                   self->lite_core_, true, self);
        if (!new_connection) {
          LOG(ERROR) << "failed to create listening connection\n";
          return;
        }
        self->lite_core_.live_connections_.insert(new_connection);
        self->conns_.insert(new_connection);
      } else if (msg.type == WorkerMessage::Type::kBarrier) {
        LOG(INFO) << "Thread " << self->thread_id_ << " reaches sync point"
                  << std::endl;
        self->barrier_.arrive_and_wait();
        self->barrier_.arrive_and_wait();
        LOG(INFO) << "Thread " << self->thread_id_ << " exits sync point"
                  << std::endl;
      }
    }
  } else {
  }
}

}  // namespace lite