#include "worker.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>

Worker::Worker(MemcachedLiteServer &lite_server, std::string &backend_addr,
               std::string &backend_port)
    : lite_server_(lite_server),
      backend_addr_(backend_addr),
      backend_port_(backend_port) {
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

void Worker::Run() {
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);

  if ((ret = pthread_create(&thread_id_, &attr, ThreadBody, this)) != 0) {
    fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
    exit(1);
  }

  pthread_setname_np(thread_id_, "mc-worker");
}

void *Worker::ThreadBody(void *arg_self) {
  Worker *self = static_cast<Worker *>(arg_self);

  event_base_loop(self->base_, 0);
  event_base_free(self->base_);

  return NULL;
}

void Worker::NotifyHandler(evutil_socket_t fd, short which, void *arg_self) {
  Worker *self = static_cast<Worker *>(arg_self);

  if (fd == self->notify_event_fd) {
    uint64_t sfd = 0;
    if (read(fd, &sfd, sizeof(uint64_t)) != sizeof(uint64_t)) {
      fprintf(stderr, "Worker can't read from libevent pipe\n");
      return;
    }
    std::unique_ptr<Connection> new_connection;
    if (!(new_connection = std::make_unique<Connection>(
              sfd, EV_READ | EV_PERSIST, self->base_, ConnectionHandler,
              self->lite_server_, true, self->backend_addr_,
              self->backend_port_))) {
      fprintf(stderr, "failed to create listening connection\n");
      exit(EXIT_FAILURE);
    }
    self->conns_.push(std::move(new_connection));
  } else {
  }
}

void Worker::ConnectionHandler(evutil_socket_t fd, short which,
                               void *arg_conn) {
  Connection *c = static_cast<Connection *>(arg_conn);
  if (!c->Read()) {
    delete c;
    // TODO: remove it from conns_
  }
}
