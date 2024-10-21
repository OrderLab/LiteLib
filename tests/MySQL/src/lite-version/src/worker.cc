#include "worker.hpp"

#include "service.hpp"

MySQLWorker::MySQLWorker(MySQL &mysql) : mysql_(mysql) {
  PCHECK(notify_event_fd_ = eventfd(0, EFD_NONBLOCK))
      << "failed creating eventfd for mysql thread";

  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);

  event_set(&notify_event_, notify_event_fd_, EV_READ | EV_PERSIST,
            NotifyHandler, this);

  event_base_set(base_, &notify_event_);

  LOG_IF(FATAL, event_add(&notify_event_, 0) == -1)
      << "Can't monitor libevent notify pipe\n";

  pthread_attr_t attr;

  pthread_attr_init(&attr);

  PCHECK(!pthread_create(&thread_id_, &attr, ThreadBody, this))
      << "Can't create thread: mysql_task_queue_worker" << std::endl;

  pthread_setname_np(thread_id_, "MySQL-worker");
  pthread_attr_destroy(&attr);
}

MySQLWorker::~MySQLWorker() {
  event_del(&notify_event_);
  event_base_free(base_);
  close(notify_event_fd_);
}

void *MySQLWorker::ThreadBody(void *arg_self) {
  MySQLWorker *self = static_cast<MySQLWorker *>(arg_self);

  event_base_loop(self->base_, 0);
  event_base_free(self->base_);

  return NULL;
}

void MySQLWorker::NotifyHandler(evutil_socket_t fd, short which,
                                void *arg_self) {
  MySQLWorker *self = static_cast<MySQLWorker *>(arg_self);

  if (fd == self->notify_event_fd_) {
    uint64_t counter = 0;
    if (read(fd, &counter, sizeof(uint64_t)) != sizeof(uint64_t)) {
      LOG(ERROR) << "MySQL can't read from libevent pipe\n";
      return;
    }
    while (counter--) {
      NormalTask tsk = self->notify_queue_.pop_front();
      if (tsk.type == NormalTask::Type::kInsertCache) {
        self->mysql_.query_cache_.HandleInvalidatedQueryBlockFromFull(
            tsk.query_cache_block_full_ptr, self->mysql_.table_cache_,
            self->mysql_.dangling_cache_);
      } else if (tsk.type == NormalTask::Type::kUpdateQuery) {
        self->mysql_.NormalUpdateQuery(tsk.query, tsk.conn, tsk.cache);
      }
    }
  } else {
  }
}
